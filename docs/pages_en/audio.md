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

## 3D sound {#audio_3d}

In a 3D game a sound comes from a place in the world and is heard from a **listener**
(njin::audio_listener3d). The engine recomputes, for every sound, every frame:

- **Volume** from the distance between the listener and the source, from the speaker's direction
  (when it has a cone) and from anything blocking the way.
- **Left and right** from the source's direction relative to where the listener faces.
- **Pitch** from the Doppler effect: a source and listener closing in sound higher, moving apart
  lower (a car rushing past).

@include audio3d.cpp

### The listener

By default the listener **follows the camera** of the last njin::begin_3d() drawn to the screen:
same position, same facing, and a velocity the engine measures from how far the camera moves each
frame. A 3D game usually has nothing to call.

When the ears are not at the camera (a third-person view that should hear from the character), set
them by hand with njin::audio_set_listener3d(); from then on the listener stops following the
camera until njin::audio_listener3d_follow_camera() is called. A begin_3d() into a render texture
(a model turning in a menu) does not move the listener.

### Playing

| Function | Behaviour |
|---|---|
| njin::sound_play3d() | Plays **once** at a point, or following an entity with njin::transform3d |
| njin::sound_loop3d() | Plays **in a loop** until njin::voice3d_stop(): an engine, a fire, a waterfall |

Both return a njin::voice3d_handle for that one sound. Use it to move the sound
(njin::voice3d_set_position(), njin::voice3d_attach()), set its velocity for Doppler
(njin::voice3d_set_velocity()), change how it is heard while it plays (njin::voice3d_set_desc():
pitch from engine speed, volume from throttle) and stop it. When the sound ends its handle stops
working by itself; the slot is reused, but an old handle **never** points at the new sound.

A sound following an entity: if the entity is destroyed, a one-shot plays out where it last was and
a loop stops. njin::sound_stop() on a sound also stops all of that sound's 3D voices.

At most **64** 3D sounds at once. Past that the one-shot that has been playing longest is cut;
loops are never cut.

### Distance

njin::sound3d_desc says how far a sound carries:

| Field | Meaning |
|---|---|
| `min_distance` | Closer than this is full volume. Default 1 |
| `max_distance` | Farther than this is silent. Default 40 |
| `rolloff` | How it falls off in between (table below) |
| `rolloff_factor` | Steepness: 2 dies off faster, 0.5 carries farther |

| `rolloff` | Volume at distance `d` | Good for |
|---|---|---|
| njin::rolloff_inverse | `min / (min + factor * (d - min))`: as in real life | The default, most sounds |
| njin::rolloff_linear | Falls evenly from 1 to 0 between `min_distance` and `max_distance` | When you need to know exactly how far a sound is heard |
| njin::rolloff_exponential | `(d / min)^-factor`: dies off faster | Quiet sounds, only heard up close |

Inverse and exponential never reach 0 on their own, so over the last 10% before `max_distance`
the engine fades them to 0: a sound moving out of range does not cut off.

### Panning, Doppler, cones

- `spread` (0..1) is how far left and right go: 1 puts a sound on the right fully in the right
  speaker, 0 keeps it centred. Closer than `min_distance` it drifts back to the centre, so a sound
  right overhead does not jump from one speaker to the other.
- `doppler` (0 is off, 1 is real life). A sound's velocity is measured by the engine from how far it
  moves each frame, smoothed over about 0.1 seconds; when you have the real velocity
  (njin::body3d_velocity()) pass it with njin::voice3d_set_velocity(). The speed of sound defaults
  to 343 units per second (one unit is one metre); change it with njin::audio_set_speed_of_sound().
- **Cone**: a directional speaker (a car horn, a megaphone) sets `cone_direction`. Within the
  `cone_inner` angle around it the sound is at full volume, outside `cone_outer` it is down to
  `cone_outer_volume`, and in between it blends. A zero `cone_direction` (the default) plays
  equally in every direction.

### Occlusion

With `occlusion` on, every frame the engine casts a physics ray (njin::physics3d_raycast()) from the
listener to the source. If the ray hits a body before the source, the sound drops to
`occlusion_volume`, easing over about 0.15 seconds. A hit within `occlusion_margin` of the source
does not count: a car's chassis does not block its own engine.

@note Occlusion only makes a sound **quieter**, not **muffled** (no high-frequency filter). raylib
mixes each sound by volume, pan and pitch, with no filter of its own per sound.

### The final volume

The volume of a 3D sound is the product of: the sound's own volume (njin::sound_set_volume()), its
bus and `bus_master` (njin::audio_set_bus_volume(), see @ref settings), `volume` in
njin::sound3d_desc, distance, cone and occlusion. Changing a bus volume in a settings menu reaches
3D sounds already playing on the next frame.

njin::voice3d_state() returns what the engine just computed for a sound (volume, pan, pitch,
distance, whether it is blocked), for debugging or to draw a sound indicator on screen.

## The engine's audio module

A core module named `njin.audio` is always registered. Every frame, in `phase_post_update`,
it does three things:

- **Feeds the music streams.** Music is decoded in small pieces and goes silent right after
  the first piece without this.
- **Restarts looping sounds** that have finished. That is why njin::sound_play_loop() only needs to be called
  once.
- **Updates 3D sounds**: moves the listener with the camera and sounds with their entities,
  recomputes volume, pan and pitch, restarts 3D loops and frees one-shots that have finished.

You do not need to call anything to turn it on. See @ref architecture to learn how core modules work.

@note Because a looping sound is restarted **after** it finishes, the join has a gap of about one
frame. For short background sounds it is negligible. For long background music, use njin::music_load().

## Releasing

Resources are freed automatically when njin::destroy() is called. Only call njin::sound_unload()
or njin::music_unload() when you want to free something early. The audio device is opened along with the window and
closed after all resources have been freed.
