# Video {#video}

This page plays video in a game: an opening cutscene, an ending film, a TV screen or a billboard in a 3D world.
Everything is declared in `njin_video.h`; decoding is done by pl_mpeg, inside the engine.

Read first: @ref rendering (textures) and @ref audio (volume buses).

## MPEG-1 only

njin only reads **MPEG-1**: a `.mpg` file with an MPEG-1 picture and MP2 sound. Convert any other video (mp4, mov,
webm...) to it with ffmpeg:

```sh
ffmpeg -i in.mp4 -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 192k -f mpeg out.mpg
```

`-q:v` goes from 2 (best, large file) to 31; 4 is a good balance. Add `-vf scale=1280:-2` to make the picture
smaller, `-an` to drop the sound. MPEG-1 is old but very light to decode, needs no outside library, and carries no
codec licensing.

## Opening and playing

```cpp
const njin::video_handle intro = njin::video_open(ctx, {.path = "videos/intro.mpg"});
// every frame, in phase_render:
njin::video_draw_fit(ctx, intro, {{0, 0}, njin::window_size(ctx)});
if (njin::video_finished(ctx, intro)) { /* into the game */ }
```

video_open() opens it and plays at once (set `play = false` to open it and hold the first frame). The engine
decodes video in `phase_post_update` in **real time**: not by time_set_scale(), and it keeps playing while the
game is paused, so a cutscene does not slow down with slow motion. Picture and sound stay in step.

| Function | Does |
|---|---|
| video_play(), video_pause() | Play on, pause. Playing a video that has ended plays it again from the start |
| video_seek() | Jumps to a second (the picture shown is the nearest frame before it) |
| video_set_loop(), video_set_volume() | Looping, its own volume |
| video_time(), video_duration(), video_finished() | Where it is, how long it lasts, whether it has ended |
| video_size(), video_framerate(), video_has_audio() | Picture size, frames per second, whether it has sound |
| video_close() | Closes it, freeing picture and sound |

The sound goes through `video_desc::bus` (`bus_music` by default) and `bus_master`, so the volume sliders of a
settings menu apply to it like any other sound.

## Drawing

- **2D:** video_draw() stretches the picture into a rectangle; video_draw_fit() fits it inside an area keeping
  its aspect ratio and fills the spare strips (the black bars of a cutscene).
- **3D:** video_texture() is a texture holding the current frame, used like any texture: on a shape through
  `material3d::texture` (turn on `unlit` so the screen is not darkened by the lighting), or on a model through
  `model_material`. The texture belongs to the video: do not texture_unload() it.

## Full example

A full-screen cutscene, Enter to skip; then a room with a TV playing a looping video.

@include video.cpp

## Limits

- MPEG-1/MP2 only. mp4, webm or mkv cannot be read directly: convert them with ffmpeg as above.
- Decoding is on the CPU: 720p plays well, 1080p at 60 frames per second takes a good share of one CPU core on a
  slow machine.
- A seek (video_seek()) stops at the nearest keyframe before the time asked for, so it may be a few tenths of a
  second early.
- No subtitles: draw text by video_time() with the UI.
