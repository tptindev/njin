#include "njin_video_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_ecs.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio> // pl_mpeg.h uses FILE without including it
#include <pl_mpeg.h>

namespace njin {
namespace {
// Stream buffers of this many frames (about 46 ms at 44.1 kHz); the ring holds
// at most two seconds before the oldest samples are dropped.
constexpr i32 stream_buffer_frames = 2048;
constexpr f32 ring_seconds = 2.0f;
// After a long stall (a breakpoint, a loading hitch) decode at most this much
// at once instead of every frame in between.
constexpr f32 max_step = 0.25f;

video_slot *video_of(video_store &store, video_handle h) {
  if (h.id == 0 || h.id > store.videos.size())
    return nullptr;
  video_slot &v = *store.videos[h.id - 1];
  return v.alive ? &v : nullptr;
}

const video_slot *video_of(const video_store &store, video_handle h) {
  return video_of(const_cast<video_store &>(store), h);
}

void on_video(plm_t *, plm_frame_t *frame, void *user) {
  video_slot &v = *(video_slot *)user;
  if ((i32)frame->width != v.width || (i32)frame->height != v.height)
    return;
  plm_frame_to_rgba(frame, v.pixels.data(), v.width * 4);
  v.fresh = true;
  v.frames++;
}

void on_audio(plm_t *, plm_samples_t *samples, void *user) {
  video_slot &v = *(video_slot *)user;
  if (!v.has_audio || v.ring.empty())
    return;
  const usize n = (usize)samples->count * 2;
  const usize cap = v.ring.size();
  for (usize i = 0; i < n; i++) {
    if (v.ring_count == cap) {
      v.ring_read = (v.ring_read + 1) % cap;
      v.ring_count--;
    }
    v.ring[(v.ring_read + v.ring_count) % cap] = samples->interleaved[i];
    v.ring_count++;
  }
}

void clear_ring(video_slot &v) {
  v.ring_read = 0;
  v.ring_count = 0;
}

f32 bus_factor(const audio_store &a, audio_bus bus) {
  const auto one = [&](audio_bus b) { return a.bus_muted[b] ? 0.0f : a.bus_volume[b]; };
  return bus == bus_master ? one(bus_master) : one(bus) * one(bus_master);
}

void feed_stream(video_slot &v, std::vector<f32> &chunk) {
  chunk.resize((usize)stream_buffer_frames * 2);
  const usize cap = v.ring.size();
  while (IsAudioStreamProcessed(v.stream)) {
    const usize take = std::min(chunk.size(), v.ring_count);
    for (usize i = 0; i < take; i++)
      chunk[i] = v.ring[(v.ring_read + i) % cap];
    std::fill(chunk.begin() + (std::ptrdiff_t)take, chunk.end(), 0.0f);
    v.ring_read = (v.ring_read + take) % cap;
    v.ring_count -= take;
    UpdateAudioStream(v.stream, chunk.data(), stream_buffer_frames);
  }
}

void upload(context &ctx, video_slot &v) {
  if (!v.fresh)
    return;
  const texture_slot *t = texture_slot_of(ctx.texture, v.texture);
  if (t != nullptr)
    UpdateTexture(t->texture, v.pixels.data());
  v.fresh = false;
}

void free_video(video_slot &v) {
  if (v.has_audio && IsAudioDeviceReady())
    UnloadAudioStream(v.stream);
  if (v.plm != nullptr)
    plm_destroy(v.plm);
  v.plm = nullptr;
  v.has_audio = false;
}

void set_stream_running(video_slot &v, bool run) {
  if (!v.has_audio)
    return;
  if (run) {
    if (!IsAudioStreamPlaying(v.stream))
      PlayAudioStream(v.stream);
    ResumeAudioStream(v.stream);
  } else {
    PauseAudioStream(v.stream);
  }
}
} // namespace

video_store::~video_store() {
  for (std::unique_ptr<video_slot> &v : videos)
    free_video(*v);
}

video_handle video_open(context &ctx, const video_desc &desc) {
  if (desc.path == nullptr) {
    NJIN_WARN("video_open: no path");
    return {};
  }
  const std::string path = asset_path(desc.path);
  plm_t *plm = plm_create_with_filename(path.c_str());
  if (plm == nullptr) {
    NJIN_WARN("video_open: cannot open %s", desc.path);
    return {};
  }
  if (!plm_has_headers(plm) || plm_get_width(plm) <= 0 || plm_get_height(plm) <= 0) {
    NJIN_WARN("video_open: %s is not an MPEG-1 video (.mpg); convert it with: ffmpeg -i in.mp4 -c:v mpeg1video "
              "-q:v 4 -c:a mp2 -b:a 192k -f mpeg out.mpg",
              desc.path);
    plm_destroy(plm);
    return {};
  }
  ctx.video.videos.push_back(std::make_unique<video_slot>());
  const u32 id = (u32)ctx.video.videos.size();
  video_slot &v = *ctx.video.videos.back();
  v.alive = true;
  v.plm = plm;
  v.width = plm_get_width(plm);
  v.height = plm_get_height(plm);
  // pl_mpeg's RGBA conversion writes red, green and blue only: alpha is set once
  // here and never touched again.
  v.pixels.assign((usize)v.width * (usize)v.height * 4, 255);
  v.loop = desc.loop;
  v.bus = desc.bus;
  v.volume = std::isfinite(desc.volume) ? clamp(desc.volume, 0.0f, 1.0f) : 1.0f;

  Image blank = GenImageColor(v.width, v.height, BLACK);
  ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  texture_slot tex{};
  tex.texture = LoadTextureFromImage(blank);
  UnloadImage(blank);
  tex.alive = tex.texture.id != 0;
  SetTextureFilter(tex.texture, TEXTURE_FILTER_BILINEAR);
  ctx.texture.slots.push_back(tex);
  v.texture = texture_handle{(u32)ctx.texture.slots.size()};

  const i32 rate = plm_get_samplerate(plm);
  v.has_audio = desc.audio && IsAudioDeviceReady() && plm_get_num_audio_streams(plm) > 0 && rate > 0;
  plm_set_audio_enabled(plm, v.has_audio ? 1 : 0);
  if (v.has_audio) {
    SetAudioStreamBufferSizeDefault(stream_buffer_frames);
    v.stream = LoadAudioStream((unsigned int)rate, 32, 2);
    SetAudioStreamBufferSizeDefault(0);
    v.stream_frames = stream_buffer_frames;
    v.ring.assign((usize)((f32)rate * ring_seconds) * 2, 0.0f);
    // Decode the sound this far ahead of the picture: what the stream's two
    // buffers hold, so a sample is heard about when its frame shows.
    plm_set_audio_lead_time(plm, (f64)(stream_buffer_frames * 2) / (f64)rate);
  }
  plm_set_loop(plm, desc.loop ? 1 : 0);
  plm_set_video_decode_callback(plm, on_video, &v);
  plm_set_audio_decode_callback(plm, on_audio, &v);
  // The first frame now, so the texture is never blank.
  plm_seek(plm, 0.0, 0);
  upload(ctx, v);
  v.playing = desc.play;
  set_stream_running(v, v.playing);
  return video_handle{id};
}

void video_close(context &ctx, video_handle handle) {
  video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr)
    return;
  free_video(*v);
  texture_store_unload(ctx.texture, v->texture);
  v->alive = false;
  v->pixels = {};
  v->ring = {};
}

void video_play(context &ctx, video_handle handle) {
  video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr)
    return;
  if (v->finished) {
    clear_ring(*v);
    plm_seek(v->plm, 0.0, 0);
    upload(ctx, *v);
    v->finished = false;
  }
  v->playing = true;
  set_stream_running(*v, true);
}

void video_pause(context &ctx, video_handle handle) {
  video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr)
    return;
  v->playing = false;
  set_stream_running(*v, false);
}

bool video_playing(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr && v->playing;
}

bool video_finished(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr && v->finished;
}

void video_seek(context &ctx, video_handle handle, f32 seconds) {
  video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr || !std::isfinite(seconds))
    return;
  clear_ring(*v);
  plm_seek(v->plm, (f64)clamp(seconds, 0.0f, (f32)plm_get_duration(v->plm)), 0);
  upload(ctx, *v);
  v->finished = false;
}

f32 video_time(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? (f32)plm_get_time(v->plm) : 0.0f;
}

f32 video_duration(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? (f32)plm_get_duration(v->plm) : 0.0f;
}

void video_set_loop(context &ctx, video_handle handle, bool loop) {
  video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr)
    return;
  v->loop = loop;
  plm_set_loop(v->plm, loop ? 1 : 0);
}

void video_set_volume(context &ctx, video_handle handle, f32 volume) {
  video_slot *v = video_of(ctx.video, handle);
  if (v != nullptr && std::isfinite(volume))
    v->volume = clamp(volume, 0.0f, 1.0f);
}

vec2 video_size(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? vec2{(f32)v->width, (f32)v->height} : vec2{};
}

f32 video_framerate(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? (f32)plm_get_framerate(v->plm) : 0.0f;
}

bool video_has_audio(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr && v->has_audio;
}

i32 video_frame_count(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? v->frames : 0;
}

texture_handle video_texture(const context &ctx, video_handle handle) {
  const video_slot *v = video_of(ctx.video, handle);
  return v != nullptr ? v->texture : texture_handle{};
}

void video_draw(const context &ctx, video_handle handle, rect dest, rgba tint) {
  const video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr || v->width == 0 || v->height == 0)
    return;
  texture_draw_desc d{};
  d.pos = dest.pos;
  d.scale = {dest.size.x / (f32)v->width, dest.size.y / (f32)v->height};
  d.tint = tint;
  texture_draw_ex(ctx, v->texture, d);
}

void video_draw_fit(const context &ctx, video_handle handle, rect area, rgba bars) {
  const video_slot *v = video_of(ctx.video, handle);
  if (v == nullptr || v->width == 0 || v->height == 0)
    return;
  const f32 s = std::min(area.size.x / (f32)v->width, area.size.y / (f32)v->height);
  const vec2 size{(f32)v->width * s, (f32)v->height * s};
  const vec2 pos = area.pos + (area.size - size) * 0.5f;
  if (bars.a > 0.0f) {
    if (pos.x > area.pos.x) {
      draw_rect(ctx, {area.pos, {pos.x - area.pos.x, area.size.y}}, bars);
      draw_rect(ctx, {{pos.x + size.x, area.pos.y}, {area.pos.x + area.size.x - pos.x - size.x, area.size.y}}, bars);
    }
    if (pos.y > area.pos.y) {
      draw_rect(ctx, {area.pos, {area.size.x, pos.y - area.pos.y}}, bars);
      draw_rect(ctx, {{area.pos.x, pos.y + size.y}, {area.size.x, area.pos.y + area.size.y - pos.y - size.y}}, bars);
    }
  }
  video_draw(ctx, handle, {pos, size}, {1.0f, 1.0f, 1.0f, 1.0f});
}

namespace {
void update_videos(context &ctx) {
  const f32 dt = std::min(ctx.time.dt_real, max_step);
  std::vector<f32> chunk;
  for (std::unique_ptr<video_slot> &boxed : ctx.video.videos) {
    video_slot &v = *boxed;
    if (!v.alive)
      continue;
    if (v.playing && dt > 0.0f) {
      plm_decode(v.plm, (f64)dt);
      if (!v.loop && plm_has_ended(v.plm)) {
        v.playing = false;
        v.finished = true;
      }
    }
    upload(ctx, v);
    if (v.has_audio) {
      SetAudioStreamVolume(v.stream, v.volume * bus_factor(ctx.audio, v.bus));
      // A finished video still plays out the sound it decoded ahead.
      if (v.playing || v.finished)
        feed_stream(v, chunk);
      if (v.finished && v.ring_count == 0)
        PauseAudioStream(v.stream);
    }
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, update_videos, "video"); }
} // namespace

mod_desc video_module() { return mod_desc{.name = "njin.video", .setup = setup}; }
} // namespace njin
