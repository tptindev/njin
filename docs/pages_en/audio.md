# Audio {#audio}

njin has two kinds of audio, for two different needs:

| | **Sound** | **Music** |
|---|---|---|
| Used for | Short effects: gunshots, clicks, impacts | Background music, long ambient sound |
| Stored | Kept **entirely in memory** | **Streamed from disk**, decoded piece by piece |
| Overlapping playback | Yes (up to 8) | No, one track per stream |
| Looping | Yes, but the join has a gap of about one frame | Yes, **no** gap |
| Functions | `sound_*` | `music_*` |

Both use handles just like textures and shaders: see @ref rendering.

## Example

@include audio.cpp

## Loading and failing

njin::sound_load() and njin::music_load() return a handle with `id == 0` when:

- The machine has **no audio device** (no speakers, driver error). The game still runs normally,
  it just has no sound.
- The file **does not exist**.
- The file **has no extension** (like `Doxyfile`). raylib picks the decoder by file extension, so
  it cannot guess the format.
- The file **cannot be decoded**.

Every case logs the reason along with the path. Every function that receives an invalid or
already unloaded handle **does nothing**, so you do not need to check at every call site.

The supported formats are whatever raylib can decode, including wav, ogg, mp3, flac. Paths are
resolved from the working directory when the game runs (see @ref getting_started).

## Sound

### Playing

Four ways to play, for four needs:

| Function | Behavior | Good for |
|---|---|---|
| njin::sound_play_once() | **Overlaps** the copies already playing, up to 8. Past that, the longest-running copy is cut | Sounds that many things trigger at once: a crowd is heard as a crowd |
| njin::sound_play_once_at() | Same as above, but adjusts the **pitch** and **gain** for this copy only | One recording used for many objects of different sizes |
| njin::sound_play_restart() | **Cuts** every copy that is playing and plays again from the start | UI clicks, warning sounds: when mashed, each press is clearly heard |
| njin::sound_play_loop() | Plays on a loop until njin::sound_stop() | Engine sounds, short background sounds |

With njin::sound_play_once_at(), a `pitch` of 1 is like the recording and 2 is twice as high, and `gain` multiplies
the sound's volume. Both **apply only to that copy**: the next normal playback goes back to
1 and 1.

### Positional sound

njin::sound_play_at() plays like njin::sound_play_once() with a position in the world:

- **Volume** falls off with the distance to the point the camera is looking at: loud enough at close range,
  fading evenly, fully silent at long range. Adjust with njin::audio_set_range() (defaults 200 and 1200).
- **Left and right**: the sound pans toward the left or right speaker depending on its position on screen.

### Volume and muting

- njin::sound_set_volume(): 1 is the original volume, 0 is silence. It applies even to copies that are
  already playing.
- njin::sound_set_muted(): brings the playing volume to 0 **without touching the stored
  volume**, so unmuting restores it exactly as it was.

### Generated sound

Not every sound is read from a file. njin::sound_load_samples() creates a sound from samples
in memory:

@include audio_procedural.cpp

## Music

njin::music_load() does not load the whole file but opens a stream. Loops and background music tracks usually
use this.

| Function | What it does |
|---|---|
| njin::music_play() | Plays **from the start**, even when paused or already playing |
| njin::music_stop() | Stops and returns to the start of the track |
| njin::music_pause() / njin::music_resume() | Pauses and resumes, **keeping the position** |
| njin::music_set_looping() | Turns looping on or off. On by default, takes effect immediately mid-track |
| njin::music_set_volume() / njin::music_set_muted() | Same as sound |

## The engine's audio module

A core module named `njin.audio` is always registered. Every frame, in `phase_post_update`,
it does two things:

- **Feeds the music streams.** Music is decoded in small pieces and goes silent right after
  the first piece without this.
- **Restarts looping sounds** that have finished. That is why njin::sound_play_loop() only needs to be called
  once.

You do not need to call anything to turn it on. See @ref architecture to learn how core modules work.

@note Because a looping sound is restarted **after** it finishes, the join has a gap of about one
frame. For short background sounds it is negligible. For long background music, use njin::music_load().

## Releasing

Resources are freed automatically when njin::destroy() is called. Only call njin::sound_unload()
or njin::music_unload() when you want to free something early. The audio device is opened along with the window and
closed after all resources have been freed.
