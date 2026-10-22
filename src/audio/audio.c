/* A small PCM mixer. The callback never allocates, frees, or enters Goose.
   SDL's stream lock protects all shared state. PCM copying happens outside
   that lock so loading a sound does not block playback for its duration. */
#include "audio_api.h"
#include <SDL3/SDL.h>
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SOUNDS GS_AUDIO_MAX_SOUNDS
#define AUDIO_VOICES GS_AUDIO_MAX_VOICES

typedef struct {
    uint64_t id;
    float *samples;
    int64_t frames;
    int rate, channels;
} audio_sound;

typedef struct {
    uint64_t id;
    audio_sound *sound;
    double position;
    float volume, pan, pitch;
    bool loop;
} audio_voice;

static struct {
    bool open, offline;
    bool paused;   /* every voice held where it is; the output silent */
    float volume;  /* the master volume, applied after the voices are mixed */
    int rate;
    SDL_AudioStream *stream;
    audio_sound sounds[AUDIO_SOUNDS];
    audio_voice voices[AUDIO_VOICES];
    char error[1024];
    uint64_t next_id;  /* Never reset: handles stay stale across close/open. */
} audio;

static void audio_lock(void) {
    if (audio.stream) SDL_LockAudioStream(audio.stream);
}
static void audio_unlock(void) {
    if (audio.stream) SDL_UnlockAudioStream(audio.stream);
}
static uint8_t audio_fail(const char *message) {
    snprintf(audio.error, sizeof audio.error, "%s", message);
    return 0;
}
static uint8_t audio_sdl_fail(void) { return audio_fail(SDL_GetError()); }
static bool audio_rate(int64_t rate) { return rate >= 8000 && rate <= 192000; }

static bool audio_params(float volume, float pan, float pitch) {
    // Ordered comparisons reject NaNs and infinities too.
    if (!(volume >= 0 && volume <= 1 && pan >= -1 && pan <= 1 &&
          pitch >= 0.125f && pitch <= 8)) {
        audio_fail("volume must be 0..1, pan -1..1, and pitch 0.125..8 (all finite)");
        return false;
    }
    return true;
}

static uint64_t audio_id(void) {
    if (audio.next_id == UINT64_MAX) {
        audio_fail("audio handle ids exhausted");
        return 0;
    }
    return ++audio.next_id;
}

static audio_sound *audio_find_sound(gs_audio_sound handle) {
    for (int i = 0; handle.id && i < AUDIO_SOUNDS; ++i)
        if (audio.sounds[i].id == handle.id) return &audio.sounds[i];
    audio_fail("a sound that is null, released, or was never created");
    return NULL;
}

static audio_voice *audio_find_voice(gs_audio_voice handle) {
    for (int i = 0; handle.id && i < AUDIO_VOICES; ++i)
        if (audio.voices[i].id == handle.id) return &audio.voices[i];
    return NULL;
}

/* Called under the stream lock, or synchronously in offline mode. */
static void audio_mix(float *out, int64_t frames) {
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    if (audio.paused) return;
    for (int v = 0; v < AUDIO_VOICES; ++v) {
        audio_voice *voice = &audio.voices[v];
        if (!voice->id) continue;
        audio_sound *sound = voice->sound;
        double step = (double)sound->rate / audio.rate * voice->pitch;
        float left = voice->volume * (voice->pan > 0 ? 1 - voice->pan : 1);
        float right = voice->volume * (voice->pan < 0 ? 1 + voice->pan : 1);
        for (int64_t f = 0; f < frames; ++f) {
            int64_t a = (int64_t)voice->position;
            int64_t b = a + 1;
            if (b == sound->frames) b = voice->loop ? 0 : a;
            float fraction = (float)(voice->position - a);
            int channels = sound->channels;
            float l0 = sound->samples[a * channels];
            float l1 = sound->samples[b * channels];
            float r0 = sound->samples[a * channels + channels - 1];
            float r1 = sound->samples[b * channels + channels - 1];
            out[f * 2] += (l0 + (l1 - l0) * fraction) * left;
            out[f * 2 + 1] += (r0 + (r1 - r0) * fraction) * right;
            voice->position += step;
            if (voice->position >= sound->frames) {
                if (voice->loop) voice->position = fmod(voice->position, (double)sound->frames);
                else { voice->id = 0; break; }
            }
        }
    }
    for (int64_t i = 0; i < frames * 2; ++i) {
        out[i] *= audio.volume;
        if (out[i] > 1) out[i] = 1;
        if (out[i] < -1) out[i] = -1;
    }
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream,
                                   int additional, int total) {
    (void)userdata;
    (void)total;
    float block[512];
    // SDL holds its stream lock. Round up to whole stereo frames.
    int64_t frames = ((int64_t)additional + 7) / 8;
    while (frames > 0) {
        int count = frames > 256 ? 256 : (int)frames;
        audio_mix(block, count);
        if (!SDL_PutAudioStreamData(stream, block, count * 8)) {
            audio_sdl_fail();
            return;
        }
        frames -= count;
    }
}

uint8_t gs_audio_available(void) { return 1; }

uint8_t gs_audio_open(int64_t sample_rate, uint8_t offline) {
    audio_lock();
    if (audio.open) {
        audio_fail("audio is already open; close it before opening again");
        audio_unlock();
        return 0;
    }
    audio_unlock();
    if (!audio_rate(sample_rate)) return audio_fail("sample rate must be 8000..192000 Hz");
    audio.rate = (int)sample_rate;
    audio.offline = offline != 0;
    audio.paused = false;
    audio.volume = 1;
    if (offline) { audio.open = true; return 1; }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return audio_sdl_fail();
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, (int)sample_rate };
    audio.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &spec, audio_callback, NULL);
    if (!audio.stream) {
        audio_sdl_fail();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    audio.open = true;
    if (!SDL_ResumeAudioStreamDevice(audio.stream)) {
        audio_sdl_fail();
        gs_audio_close();
        return 0;
    }
    return 1;
}

void gs_audio_close(void) {
    // Destroy waits for the callback and closes the device before samples free.
    if (audio.stream) {
        SDL_DestroyAudioStream(audio.stream);
        audio.stream = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    for (int i = 0; i < AUDIO_SOUNDS; ++i) free(audio.sounds[i].samples);
    memset(audio.sounds, 0, sizeof audio.sounds);
    memset(audio.voices, 0, sizeof audio.voices);
    audio.open = false;
}

int64_t gs_audio_error(gs_audio_bytes out) {
    audio_lock();
    int64_t n = (int64_t)strlen(audio.error);
    int64_t count = n < out.len ? n : out.len;
    if (out.data && count > 0) memcpy(out.data, audio.error, (size_t)count);
    audio_unlock();
    return n;
}

// Puts owned samples (malloc'd, checked) in a free slot, or frees them.
static gs_audio_sound audio_add_sound(float *owned, int64_t frames, int rate, int channels) {
    gs_audio_sound result = { 0 };
    audio_lock();
    audio_sound *slot = NULL;
    for (int i = 0; i < AUDIO_SOUNDS; ++i)
        if (!audio.sounds[i].id) { slot = &audio.sounds[i]; break; }
    if (!slot) audio_fail("all 256 sound slots are in use");
    else if ((result.id = audio_id()) != 0)
        *slot = (audio_sound){ result.id, owned, frames, rate, channels };
    audio_unlock();
    if (!result.id) free(owned);
    return result;
}

static bool audio_is_open(void) {
    audio_lock();
    bool open = audio.open;
    if (!open) audio_fail("audio is not open");
    audio_unlock();
    return open;
}

static void audio_failf(const char *what, const char *path, const char *why) {
    audio_lock();
    snprintf(audio.error, sizeof audio.error, "%s %s%s%s", what, path, why ? ": " : "", why ? why : "");
    audio_unlock();
}

gs_audio_sound gs_audio_create_sound(gs_audio_f32_slice samples, int64_t sample_rate,
                                     int64_t channels) {
    gs_audio_sound result = { 0 };
    if (!audio_is_open()) return result;
    audio_lock();
    bool ok = false;
    if (!audio_rate(sample_rate)) audio_fail("sample rate must be 8000..192000 Hz");
    else if (channels != 1 && channels != 2) audio_fail("a sound must have one or two channels");
    else if (!samples.data || samples.len <= 0 || samples.len % channels ||
             (uint64_t)samples.len > SIZE_MAX / sizeof(float))
        audio_fail("PCM must contain nonempty complete frames of mono or stereo samples");
    else ok = true;
    audio_unlock();
    if (!ok) return result;
    // Goose slices can originate in packed records. Copy before reading
    // float values so validation never assumes source alignment.
    float *copy = (float *)malloc((size_t)samples.len * sizeof(float));
    if (!copy) {
        audio_lock();
        audio_fail("out of memory copying PCM samples");
        audio_unlock();
        return result;
    }
    memcpy(copy, samples.data, (size_t)samples.len * sizeof(float));
    for (int64_t i = 0; i < samples.len; ++i) {
        if (!(copy[i] >= -1 && copy[i] <= 1)) {
            free(copy);
            audio_lock();
            audio_fail("PCM samples must be finite and within [-1, 1]");
            audio_unlock();
            return result;
        }
    }
    return audio_add_sound(copy, samples.len / channels, (int)sample_rate, (int)channels);
}

static void *audio_malloc(size_t n, void *user) { (void)user; return malloc(n); }
static void *audio_realloc(void *p, size_t n, void *user) { (void)user; return realloc(p, n); }
static void audio_free(void *p, void *user) { (void)user; free(p); }

gs_audio_sound gs_audio_load_sound(gs_audio_bytes path, double from, double to) {
    gs_audio_sound result = { 0 };
    if (!audio_is_open()) return result;
    char name[1024];
    size_t n = path.len < 0 || !path.data ? 0 : (size_t)path.len;
    if (n >= sizeof name) n = sizeof name - 1;
    if (n) memcpy(name, path.data, n);
    name[n] = 0;
    // SDL_LoadFile reads app bundles and, on Android, the APK's assets.
    size_t size = 0;
    void *file = SDL_LoadFile(name, &size);
    if (!file) { audio_failf("cannot read", name, SDL_GetError()); return result; }
    drmp3_allocation_callbacks heap = { NULL, audio_malloc, audio_realloc, audio_free };
    drmp3_config config = { 0 };
    drmp3_uint64 frames = 0;
    float *pcm = drmp3_open_memory_and_read_pcm_frames_f32(file, size, &config, &frames, &heap);
    SDL_free(file);
    if (!pcm || !frames) { free(pcm); audio_failf("cannot decode", name, "not an MP3"); return result; }
    int channels = (int)config.channels, rate = (int)config.sampleRate;
    if ((channels != 1 && channels != 2) || !audio_rate(rate)) {
        free(pcm);
        audio_failf("cannot play", name, "not mono or stereo at 8000..192000 Hz");
        return result;
    }
    int64_t first = from > 0 ? (int64_t)(from * rate) : 0;
    int64_t end = to > 0 ? (int64_t)(to * rate) : (int64_t)frames;
    if (end > (int64_t)frames) end = (int64_t)frames;
    if (first >= end) { free(pcm); audio_failf("nothing to play in", name, "from is at or past to"); return result; }
    int64_t count = (end - first) * channels;
    if (first) memmove(pcm, pcm + first * channels, (size_t)count * sizeof(float));
    // A decoder can overshoot full scale a little; the mixer takes [-1, 1].
    for (int64_t i = 0; i < count; ++i) pcm[i] = pcm[i] > 1 ? 1 : pcm[i] < -1 ? -1 : pcm[i];
    return audio_add_sound(pcm, end - first, rate, channels);
}

uint8_t gs_audio_release_sound(gs_audio_sound sound) {
    if (!sound.id) return 1;
    audio_lock();
    audio_sound *slot = audio_find_sound(sound);
    float *samples = NULL;
    if (slot) {
        for (int i = 0; i < AUDIO_VOICES; ++i)
            if (audio.voices[i].sound == slot) audio.voices[i].id = 0;
        samples = slot->samples;
        memset(slot, 0, sizeof *slot);
    }
    audio_unlock();
    free(samples);
    return slot != NULL;
}

gs_audio_voice gs_audio_play(gs_audio_sound sound, float volume, float pan,
                             float pitch, uint8_t loop) {
    gs_audio_voice result = { 0 };
    audio_lock();
    audio_sound *source = audio_find_sound(sound);
    if (source && audio_params(volume, pan, pitch)) {
        audio_voice *slot = NULL;
        for (int i = 0; i < AUDIO_VOICES; ++i)
            if (!audio.voices[i].id) { slot = &audio.voices[i]; break; }
        if (!slot) audio_fail("all 64 voice slots are playing");
        else {
            result.id = audio_id();
            if (result.id)
                *slot = (audio_voice){ result.id, source, 0, volume, pan, pitch, loop != 0 };
        }
    }
    audio_unlock();
    return result;
}

uint8_t gs_audio_playing(gs_audio_voice voice) {
    audio_lock();
    bool playing = audio_find_voice(voice) != NULL;
    audio_unlock();
    return playing;
}

void gs_audio_stop(gs_audio_voice voice) {
    audio_lock();
    audio_voice *slot = audio_find_voice(voice);
    if (slot) slot->id = 0;
    audio_unlock();
}

void gs_audio_stop_all(void) {
    audio_lock();
    memset(audio.voices, 0, sizeof audio.voices);
    audio_unlock();
}

void gs_audio_set_paused(uint8_t paused) {
    audio_lock();
    audio.paused = paused != 0;
    audio_unlock();
}

uint8_t gs_audio_paused(void) { return audio.paused; }

uint8_t gs_audio_set_volume(float volume) {
    if (!(volume >= 0 && volume <= 1)) return audio_fail("volume must be 0..1");
    audio_lock();
    audio.volume = volume;
    audio_unlock();
    return 1;
}

float gs_audio_volume(void) { return audio.open ? audio.volume : 1; }

uint8_t gs_audio_set_voice(gs_audio_voice voice, float volume, float pan, float pitch) {
    audio_lock();
    audio_voice *slot = audio_find_voice(voice);
    bool ok = false;
    if (!slot) audio_fail("voice is no longer playing");
    else if (audio_params(volume, pan, pitch)) {
        slot->volume = volume;
        slot->pan = pan;
        slot->pitch = pitch;
        ok = true;
    }
    audio_unlock();
    return ok;
}

uint8_t gs_audio_render(gs_audio_f32_slice out) {
    audio_lock();
    bool ok = false;
    if (!audio.open || !audio.offline) audio_fail("render requires an offline mixer");
    else if (out.len < 0 || out.len % 2 || (out.len && !out.data) ||
             (uint64_t)out.len > SIZE_MAX / sizeof(float))
        audio_fail("render requires complete stereo frames");
    else {
        // Output slices can also be unaligned (a packed Goose struct field).
        // Mix into aligned blocks, then copy their object representation.
        float block[512];
        uint8_t *destination = (uint8_t *)out.data;
        int64_t remaining = out.len / 2;
        while (remaining) {
            int count = remaining > 256 ? 256 : (int)remaining;
            audio_mix(block, count);
            memcpy(destination, block, (size_t)count * 2 * sizeof(float));
            destination += count * 2 * sizeof(float);
            remaining -= count;
        }
        ok = true;
    }
    audio_unlock();
    return ok;
}
