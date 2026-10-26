# PCM audio

`audio.goose` exposes a small sound-effects API. It owns no PCM in Goose:
`create_sound` validates and copies a slice to the native layer, which keeps it
until `release_sound` or `close`. `load_sound` decodes an MP3 there, and
`load_sound_lowpass` decodes it and runs Web Audio's lowpass BiquadFilter over
it (cut Hz, Q in dB) there too, so a filtered sound never passes through Goose. Releasing a sound stops every voice referring
to it. This makes source-array lifetimes independent of asynchronous playback.

`load_sound_later` and `load_sound_lowpass_later` do the same decoding on the
layer's own loading thread, so the call returns at once and no frame waits for
an MP3. The sound is `SOUND_LOADING` until its samples land, then
`SOUND_READY` (`sound_state`); one that fails to load becomes `SOUND_GONE` and
`error()` says why. It can be played, seeked and released while it loads: a
voice on it waits silent at its place (a seek is kept, in seconds) and plays
once the samples land. One thread decodes the sounds in the order they were
asked for, skipping one released before its turn. A program whose voice must
start on a beat checks `sound_state` and waits for it in its own time, as the
Goose books' player holds a page's time.

The implementation is `src/audio/audio.c`; `audio_api.h` is its packed extern
boundary and JIT symbol list. `test/api_check.py audio` checks the C and Goose
declarations. The compiler tracks native audio calls and rejects them from
`thread_fn`, as it does for gfx, physics and ui. The native callback is C only.

## Mixing and ownership

The mixer has fixed tables of 256 sounds and 64 voices. IDs increase monotonically
and never reset, including across close/open. Exhaustion is explicit: sound or
voice creation returns zero. A completed voice frees its table slot, without
freeing PCM on the audio thread. A stale voice is harmless to stop or query.
There is no voice stealing, callback into user code, or unbounded callback work.

Each voice has a fractional source position, volume, stereo balance, pitch and
looping flag. The step is `source_rate / output_rate * pitch`. Linear
interpolation blends adjacent source frames; a loop blends the end into its
first frame, while a one-shot holds its final sample through the final fractional
frame. Mono duplicates into stereo. Pan attenuates the opposite side. All voices
sum into stereo f32 and the result clips to `[-1, 1]`. This is deliberately a
sound-effects mixer, not a band-limited music resampler or an effects engine.

`open` initializes only SDL's audio subsystem and opens a stereo f32 stream to
the default playback device. SDL performs the conversion to the physical device
format. Its get callback fills requests in blocks of at most 256 frames. The
stream lock protects tables and error text. Main-thread PCM allocation/copy and
release happen outside that lock; only table publication/removal occurs inside.
The loading thread decodes outside every lock and takes the stream lock and the
table mutex only to publish a sound's samples into its slot; offline, where
there is no stream, the table mutex alone guards the tables. Closing first
drops the queued sounds and joins the loading thread (after the sound it is
decoding), then destroys the stream (synchronizing with its callback), then frees PCM
and decrements only SDL's audio subsystem reference. gfx can close independently.

`open_offline` starts no SDL subsystem, device or thread. `render` advances the
same mixer synchronously, giving tests exact samples and exact completion
boundaries. It also permits a program to generate PCM for its own file writer.

## Builds and validation

Audio is a module of the gfx package (`audio.goose`, its layer
in `src/audio/`), which shares one SDL build between gfx, audio
and ui (`docs/gfx.md`); `-DGOOSE_BUILTIN_LIBS=all` compiles it into
the compiler, and `--audio-link msvc|cc` then prints the package's response
file.

The normal test runner includes `test/audio/audio_mixer.goose` and
`audio_errors.goose` in JIT and generated-C runs. They exercise PCM ownership,
stereo, rate conversion, interpolation, gain, pan, pitch, loop boundaries,
overlap, clipping, exhaustion, stale handles, close/open, and bad input.
`audio_err_thread.goose` tests the main-thread restriction.

The explicit `goose_audio_test` CMake target uses SDL's dummy driver to exercise
the actual callback, device-open failure and recovery, nonfinite inputs,
packed unaligned slices, concurrent table access, release during playback, close/reopen, and
sounds loaded later (`test/tone.mp3`): the same samples as loaded at once, filtered or not, a
voice played and seeked while its sound loads, a missing file, release while loading, and close
with sounds queued. Run, for
example, `cmake --build build --config Release --target goose_audio_test`, then
`build/Release/goose_audio_test.exe` on Windows (or `build/goose_audio_test` with
a single-config generator). It needs no window, GPU, speakers, or audio session.
Physical-device behavior still depends on the platform backend; dummy and offline
tests do not establish audible quality, device-loss recovery, or real latency.
