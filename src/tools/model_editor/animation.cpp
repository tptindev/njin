#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace model_editor {
static bool key_less(const Keyframe &a, const Keyframe &b) {
  return a.bone == b.bone ? a.time < b.time : a.bone < b.bone;
}
bool set_key(AnimationClip &clip, Keyframe key) {
  key.time = std::clamp(key.time, 0.0f, clip.duration);
  for (auto &existing : clip.keys) {
    if (existing.bone == key.bone && std::abs(existing.time-key.time) < 0.0001f) {
      existing = key; return true;
    }
  }
  if (clip.keys.size() >= 10000) return false;
  clip.keys.insert(std::lower_bound(clip.keys.begin(),clip.keys.end(),key,key_less),key);
  return true;
}
bool remove_key(AnimationClip &clip, int bone, float time) {
  return std::erase_if(clip.keys,[&](const Keyframe &k) {
    return k.bone == bone && std::abs(k.time-time) < 0.0001f;
  }) > 0;
}
static Quaternion rotation(Vector3 euler) {
  return QuaternionFromMatrix(MatrixRotateXYZ(Vector3Scale(euler,DEG2RAD)));
}
std::vector<BonePose> sample_animation(const Document &doc, const AnimationClip &clip, float time) {
  // Sampling clamps, so the last key remains editable even for looping clips.
  time = std::clamp(time,0.0f,clip.duration);
  std::vector<BonePose> result;
  for (int i=0;i<(int)doc.bones.size();++i) {
    BonePose pose{doc.bones[i].offset,rotation(doc.bones[i].pose)};
    auto begin=std::lower_bound(clip.keys.begin(),clip.keys.end(),Keyframe{i,0},key_less);
    auto end=std::lower_bound(begin,clip.keys.end(),Keyframe{i+1,0},key_less);
    if (begin!=end) {
      auto next=std::upper_bound(begin,end,time,[](float t,const Keyframe &k) { return t<k.time; });
      const auto &a=next==begin ? *begin : *(next-1);
      const auto &b=next==end ? *(end-1) : *next;
      float alpha=b.time>a.time ? std::clamp((time-a.time)/(b.time-a.time),0.0f,1.0f) : 0;
      pose.translation=Vector3Lerp(a.translation,b.translation,alpha);
      pose.rotation=QuaternionSlerp(rotation(a.rotation),rotation(b.rotation),alpha);
    }
    result.push_back(pose);
  }
  return result;
}
float advance_animation(float time, float delta, const AnimationClip &clip, bool &playing) {
  if (!playing) return std::clamp(time,0.0f,clip.duration);
  time += std::max(0.0f,delta);
  if (clip.loop) return std::fmod(time,clip.duration);
  if (time>=clip.duration) { time=clip.duration; playing=false; }
  return time;
}
void add_demo_animation(Document &doc) {
  if (doc.clips.size()>=64) return;
  AnimationClip clip; clip.name="Wave (demo)"; clip.duration=2;
  for (int i=0;i<(int)doc.bones.size();++i) {
    set_key(clip,{i,0,doc.bones[i].offset,doc.bones[i].pose});
    set_key(clip,{i,2,doc.bones[i].offset,doc.bones[i].pose});
    if (doc.bones[i].name=="Forearm.R") {
      for (int frame=1;frame<4;++frame)
        set_key(clip,{i,frame*0.5f,{}, {0,0,frame==2 ? 35.0f : 95.0f}});
    }
  }
  doc.clips.push_back(std::move(clip));
}
}
