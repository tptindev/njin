#include "person.h"
#include "city/city.h"
#include "view.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sandtable {
namespace {
struct motion { const char *name; f32 seconds; bool loop; };
constexpr motion motions[] = {
    {"idle",2.4f,true}, {"talk",2.4f,true}, {"walk",1.0f,true}, {"jog",.73f,true},
    {"sprint",.57f,true}, {"jab",.6f,false}, {"cross",.75f,false}, {"hit",.65f,false},
    {"death",1.5f,false}, {"sit",2.4f,true}, {"crouch",2.4f,true}, {"aim",2.0f,true}, {"shoot",.4f,false}};
static_assert(std::size(motions)==static_cast<size_t>(act::count));
bool ready = false;
// FK rig in metres, Y-up/+Z-forward. Blend angles to preserve limb lengths.
struct pose {
  f32 hip=1.06f, lean=0, twist=0, fall=0, breath=0;
  std::array<f32,2> thigh{}, knee{}, arm{}, elbow{}, spread{.23f,-.23f};
};
f32 ease(f32 x) { x=clamp(x,0.0f,1.0f); return x*x*(3-2*x); }
pose sample(act action, f32 time) {
  pose p;
  const auto &m=motions[static_cast<size_t>(action)];
  const f32 u=m.loop ? time/m.seconds-std::floor(time/m.seconds) : clamp(time/m.seconds,0.0f,1.0f);
  const f32 phase=u*2*pi, w=std::sin(phase), pulse=std::sin(pi*u);
  p.breath=.006f*w;
  if (action==act::walk || action==act::jog || action==act::sprint) {
    const f32 amplitude=action==act::walk?.30f:action==act::jog?.48f:.65f;
    p.lean=amplitude*.13f; p.twist=.07f*w;
    for (size_t i=0;i<2;++i) {
      const f32 t=phase+(i==0?0:pi), s=std::sin(t);
      p.thigh[i]=amplitude*s; p.knee[i]=std::max(0.0f,-s)*amplitude*1.5f;
      p.arm[i]=-amplitude*.65f*std::sin(t-.20f);
      p.elbow[i]=.12f+amplitude*.40f+.10f*std::sin(t-.45f);
    }
    p.hip=.085f+std::max(.48f*std::cos(p.thigh[0])+.49f*std::cos(p.thigh[0]-p.knee[0]),
                        .48f*std::cos(p.thigh[1])+.49f*std::cos(p.thigh[1]-p.knee[1]));
  } else if (action==act::talk) {
    p.arm[1]=.25f+.12f*w; p.elbow[1]=.75f+.20f*std::sin(phase-.4f); p.twist=.07f*w;
  } else if (action==act::sit || action==act::crouch) {
    const bool sit=action==act::sit;
    p.thigh.fill(sit?pi*.5f:1.0f); p.knee.fill(sit?pi*.5f:1.65f);
    p.hip=.085f+.48f*std::cos(p.thigh[0])+.49f*std::cos(p.thigh[0]-p.knee[0]);
    p.lean=sit?.02f:.25f; p.arm.fill(.28f); p.elbow.fill(.65f);
  } else if (action==act::jab || action==act::cross) {
    const size_t hand=action==act::jab?0:1;
    const f32 strike=ease(u/.25f)*(1-ease((u-.43f)/.57f));
    p.arm[hand]=1.5f*strike; p.elbow[hand]=.8f*pulse*(1-strike);
    p.elbow[1-hand]=.85f*pulse; p.twist=(hand==0?-.25f:.25f)*pulse; p.lean=.10f*strike;
  } else if (action==act::hit) {
    p.lean=-.30f*pulse; p.elbow.fill(.45f*pulse); p.hip-=.05f*pulse;
  } else if (action==act::death) {
    p.fall=ease(u/.85f); p.spread={.23f+.2f*p.fall,-.23f-.2f*p.fall};
  } else if (action==act::aim || action==act::shoot) {
    const f32 kick=action==act::shoot?.18f*pulse:0;
    p.arm={1.30f,1.48f-kick}; p.elbow={.28f,.08f+kick}; p.spread={.10f,-.12f};
  }
  return p;
}
pose blend_pose(pose a, const pose &b, f32 t) {
  const auto mix=[t](f32 x,f32 y){return x+(y-x)*t;};
  a.hip=mix(a.hip,b.hip); a.lean=mix(a.lean,b.lean); a.twist=mix(a.twist,b.twist);
  a.fall=mix(a.fall,b.fall); a.breath=mix(a.breath,b.breath);
  for(size_t i=0;i<2;++i) {
    a.thigh[i]=mix(a.thigh[i],b.thigh[i]); a.knee[i]=mix(a.knee[i],b.knee[i]);
    a.arm[i]=mix(a.arm[i],b.arm[i]); a.elbow[i]=mix(a.elbow[i],b.elbow[i]); a.spread[i]=mix(a.spread[i],b.spread[i]);
  }
  return a;
}
vec3 mix3(vec3 a,vec3 b,f32 t) { return a+(b-a)*t; }
vec3 limb(f32 angle,f32 len,f32 spread=0) {
  return {std::sin(spread)*len,-std::cos(angle)*std::cos(spread)*len,std::sin(angle)*std::cos(spread)*len};
}
u32 hash(u32 n) { n^=n>>16; n*=0x7feb352du; n^=n>>15; n*=0x846ca68bu; return n^(n>>16); }
struct clay_batch {
  std::array<sdf_part,32> parts{}; u32 count=0;
  void add(vec3 a,vec3 b,f32 ra,f32 rb,f32 softness) { parts[count++]={a,b,ra,rb,softness}; }
};
} // namespace
const char *act_name(act a) { return motions[static_cast<size_t>(a)].name; }
bool act_loops(act a) { return motions[static_cast<size_t>(a)].loop; }
f32 act_duration(act a) { return motions[static_cast<size_t>(a)].seconds; }
void person_init(context &) { ready=true; }
void person_cleanup(context &) { ready=false; }
bool person_ready() { return ready; }

void draw_person(context &ctx,const person_draw &p) {
  if(!ready) return;
  pose k=sample(p.now,p.time);
  if(p.blend>0 && p.was!=p.now) k=blend_pose(k,sample(p.was,p.was_time),clamp(p.blend,0.0f,1.0f));
  const u32 id=hash(p.identity);
  const f32 width=p.identity==0?1.0f:.86f+static_cast<f32>(id&255u)/255.0f*.34f;
  const f32 scale=city::person_height*unit3d/1.83f;
  const vec2 facing=from_angle(p.facing);
  const auto world=[&](vec3 v) {
    const f32 a=k.fall*pi*.5f, y=v.y*std::cos(a)+v.z*std::sin(a), z=-v.y*std::sin(a)+v.z*std::cos(a);
    v={v.x,y+.075f*k.fall,z};
    return to3d(p.at,p.lift)+vec3{v.x*facing.y+v.z*facing.x,v.y,-v.x*facing.x+v.z*facing.y}*scale;
  };
  const auto body=[&](vec3 v) {
    const f32 y=v.y-1.06f;
    v.y=k.hip+y*std::cos(k.lean); v.z+=y*std::sin(k.lean);
    const f32 x=v.x*std::cos(k.twist)+v.z*std::sin(k.twist);
    v.z=-v.x*std::sin(k.twist)+v.z*std::cos(k.twist); v.x=x;
    return v;
  };
  clay_batch skin,shirt,pants,shoes;
  const auto add=[&](clay_batch &batch,vec3 a,vec3 b,f32 ra,f32 rb,f32 soft) {
    batch.add(world(a),world(b),ra*scale,rb*scale,soft*scale);
  };
  for(int i=-1;i<=1;++i) {
    const f32 x=static_cast<f32>(i)*.037f*width;
    add(shirt,body({x,1.105f,0}),body({x,1.53f+k.breath,0}),.065f*width,.064f*width,.030f);
  }
  add(skin,body({0,1.57f,0}),body({0,1.69f,0}),.024f,.026f,.022f);
  add(skin,body({0,1.748f,0}),body({.002f,1.769f,0}),.069f,.061f,.030f);
  add(pants,{-.062f*width,k.hip,0},{.062f*width,k.hip,0},.052f,.052f,.018f);
  // Quadratic paths round the joint itself; smooth-min fuses adjacent cones.
  const auto curve=[&](clay_batch &batch,vec3 a,vec3 joint,vec3 b,f32 r0,f32 r1,f32 r2,f32 softness) {
    const vec3 enter=mix3(a,joint,.78f), leave=mix3(joint,b,.22f);
    add(batch,a,enter,r0,r1,softness);
    vec3 prev=enter;
    for(int j=1;j<=3;++j) {
      const f32 t=static_cast<f32>(j)/3.0f;
      const vec3 next=enter*((1-t)*(1-t))+joint*(2*t*(1-t))+leave*(t*t);
      add(batch,prev,next,r1,r1,softness); prev=next;
    }
    add(batch,leave,b,r1,r2,softness);
  };
  for(size_t i=0;i<2;++i) {
    const f32 sign=i==0?1.0f:-1.0f;
    const vec3 hip{sign*.064f*width,k.hip,0};
    const vec3 knee=hip+limb(k.thigh[i],.48f,sign*.045f);
    const vec3 ankle=knee+limb(k.thigh[i]-k.knee[i],.49f,sign*.045f);
    curve(pants,hip,knee,ankle,.044f*width,.030f*width,.025f*width,.012f);
    add(shoes,ankle+vec3{0,-.043f,-.015f},ankle+vec3{0,-.046f,.060f},.034f,.029f,.013f);
    const vec3 shoulder=body({sign*.105f*width,1.53f,0});
    const vec3 elbow=shoulder+limb(k.arm[i],.285f,k.spread[i]);
    const vec3 hand=elbow+limb(k.arm[i]+k.elbow[i],.285f,k.spread[i]*.7f);
    const vec3 cuff=mix3(shoulder,elbow,.40f);
    add(shirt,shoulder,cuff,.048f*width,.036f*width,.034f);
    curve(skin,cuff,elbow,hand,.018f,.016f,.013f,.013f);
    add(skin,hand,hand+limb(k.arm[i]+k.elbow[i],.025f),.020f,.015f,.012f);
  }
  const auto draw=[&](const clay_batch &batch,rgba color) {
    draw_sdf_blend(ctx,batch.parts.data(),batch.count,.015f*scale,color);
  };
  material3d_set(ctx,{.specular=.12f,.shininess=9.0f,.rim={.62f,.43f,.27f,.10f},.clay=.85f,.clay_detail=7.0f});
  const f32 tone=p.identity==0?1.0f:.82f+static_cast<f32>((id>>8)&255u)/255.0f*.30f;
  draw(skin,{.68f*tone,.41f*tone,.23f*tone,1});
  draw(shirt,p.tint); draw(pants,rgb(61,100,122)); draw(shoes,rgb(57,49,44));
  material3d_set(ctx,{});
}
} // namespace sandtable
