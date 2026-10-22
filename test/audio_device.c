/* SDL dummy-device coverage. Build target goose_audio_test, then run it.
   Does not need a window, GPU, speakers, or an interactive audio session. */
#include "../src/audio/audio_api.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        char error[1024] = { 0 }; \
        gs_audio_error((gs_audio_bytes){ (uint8_t *)error, sizeof error - 1 }); \
        fprintf(stderr, "line %d: %s: %s\n", __LINE__, #condition, error); \
        exit(1); \
    } \
} while (0)

int main(void) {
    // Packed Goose fields need not align their f32 slices.
    CHECK(gs_audio_open(48000, 1));
    unsigned char packed[9] = { 0 };
    float sample = 0.5f;
    memcpy(packed + 1, &sample, sizeof sample);
    gs_audio_sound unaligned = gs_audio_create_sound(
        (gs_audio_f32_slice){ (float *)(packed + 1), 1 }, 48000, 1);
    CHECK(unaligned.id);
    CHECK(gs_audio_play(unaligned, 1, 0, 1, 0).id);
    CHECK(gs_audio_render((gs_audio_f32_slice){ (float *)(packed + 1), 2 }));
    float pair[2];
    memcpy(pair, packed + 1, sizeof pair);
    CHECK(pair[0] == 0.5f && pair[1] == 0.5f);
    gs_audio_close();

    // Paused, a voice holds its place and the output is silent; the master volume scales it.
    CHECK(gs_audio_open(48000, 1));
    float ramp[4] = { 0.125f, 0.25f, 0.375f, 0.5f }, two[2];
    gs_audio_sound steps = gs_audio_create_sound((gs_audio_f32_slice){ ramp, 4 }, 48000, 1);
    CHECK(steps.id && gs_audio_volume() == 1 && !gs_audio_paused());
    CHECK(gs_audio_play(steps, 1, 0, 1, 0).id);
    CHECK(gs_audio_render((gs_audio_f32_slice){ two, 2 }) && two[0] == 0.125f);
    gs_audio_set_paused(1);
    CHECK(gs_audio_paused());
    CHECK(gs_audio_render((gs_audio_f32_slice){ two, 2 }) && two[0] == 0 && two[1] == 0);
    gs_audio_set_paused(0);
    CHECK(gs_audio_render((gs_audio_f32_slice){ two, 2 }) && two[0] == 0.25f);
    CHECK(gs_audio_set_volume(0.5f) && gs_audio_volume() == 0.5f);
    CHECK(gs_audio_render((gs_audio_f32_slice){ two, 2 }) && two[0] == 0.1875f);
    CHECK(!gs_audio_set_volume(1.5f) && gs_audio_volume() == 0.5f);
    CHECK(gs_audio_set_volume(1));
    gs_audio_stop_all();
    gs_audio_voice back = gs_audio_play(steps, 1, 0, 1, 0);
    CHECK(gs_audio_seek(back, 1.0 / 48000) && gs_audio_render((gs_audio_f32_slice){ two, 2 }) && two[0] == 0.25f);
    CHECK(gs_audio_seek(back, 1) && !gs_audio_playing(back) && !gs_audio_seek(back, 0));
    gs_audio_close();
    CHECK(gs_audio_open(48000, 1) && gs_audio_volume() == 1);
    gs_audio_close();

    CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "goose-nonexistent-driver"));
    CHECK(!gs_audio_open(48000, 0));
    CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    CHECK(SDL_InitSubSystem(SDL_INIT_EVENTS));
    CHECK(gs_audio_open(48000, 0));
    CHECK(!gs_audio_open(48000, 0));
    float output[2];
    CHECK(!gs_audio_render((gs_audio_f32_slice){ output, 2 }));

    float pcm[480] = { 0.25f };
    gs_audio_sound sound = gs_audio_create_sound((gs_audio_f32_slice){ pcm, 480 }, 48000, 1);
    CHECK(sound.id);
    gs_audio_voice voice = gs_audio_play(sound, 1, 0, 1, 0);
    CHECK(voice.id);
    // Observe actual callback progress, with a generous bounded deadline.
    Uint64 deadline = SDL_GetTicks() + 3000;
    while (gs_audio_playing(voice) && SDL_GetTicks() < deadline) SDL_Delay(1);
    CHECK(!gs_audio_playing(voice));

    float invalid[] = { NAN, INFINITY, -INFINITY };
    for (int i = 0; i < 3; ++i) {
        CHECK(!gs_audio_create_sound((gs_audio_f32_slice){ &invalid[i], 1 }, 48000, 1).id);
        CHECK(!gs_audio_play(sound, invalid[i], 0, 1, 0).id);
        CHECK(!gs_audio_play(sound, 1, invalid[i], 1, 0).id);
        CHECK(!gs_audio_play(sound, 1, 0, invalid[i], 0).id);
    }
    CHECK(!gs_audio_create_sound((gs_audio_f32_slice){ NULL, 1 }, 48000, 1).id);
    CHECK(!gs_audio_create_sound((gs_audio_f32_slice){ pcm, -1 }, 48000, 1).id);
    CHECK(!gs_audio_create_sound((gs_audio_f32_slice){ pcm, INT64_MAX }, 48000, 1).id);

    // Concurrent callback reads versus main-thread updates, stop and release.
    for (int i = 0; i < 200; ++i) {
        gs_audio_sound effect = gs_audio_create_sound((gs_audio_f32_slice){ pcm, 480 }, 44100, 1);
        CHECK(effect.id);
        gs_audio_voice active = gs_audio_play(effect, 0.5f, -0.5f, 0.5f, 1);
        CHECK(active.id);
        CHECK(gs_audio_set_voice(active, 0.25f, 0.5f, 2));
        if (i % 8 == 0) SDL_Delay(1);
        CHECK(gs_audio_release_sound(effect));
        CHECK(!gs_audio_playing(active));
    }
    voice = gs_audio_play(sound, 1, 0, 1, 1);
    CHECK(voice.id);
    gs_audio_close();  // Must synchronize with an active callback before free.
    CHECK(SDL_WasInit(SDL_INIT_EVENTS) != 0);
    CHECK(!gs_audio_playing(voice));
    CHECK(gs_audio_open(44100, 0));
    CHECK(!gs_audio_play(sound, 1, 0, 1, 0).id);
    gs_audio_close();
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    puts("dummy device: callback, errors, resource churn, close/reopen passed");
    return 0;
}
