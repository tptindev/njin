#include <njin.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <array>
#include <cmath>
#include <string>
#include <filesystem>
namespace {
int frames=0, limit=120;
bool resize_grew=false,resize_shrank=false;
const unsigned char pixels[]{255,0,0,255, 0,255,0,180, 0,0,255,255, 255,255,255,100};
njin::RenderTextureHandle texture;
njin::RenderMeshHandle mesh;
njin::RenderTargetHandle render_target;
njin::RenderTargetHandle unused_target,hdr_target;
njin::RenderTextureHandle target_texture;
njin::body3d_handle physics_body;
njin::scene_handle transition_scene;
njin::texture_handle legacy_texture;
njin::texture_handle tile_texture;
entt::entity tile_entity,tile_overlay;
entt::entity particle_entity;
entt::entity sprite_entity, camera_entity;
bool portable=false;
std::string backend_label;
bool audio_smoke=false;
njin::sound_handle silent_sound;
std::string capture_path;
void startup(njin::context &ctx) {
  resize_grew=resize_shrank=false;
  if(audio_smoke) {
    const std::vector<float> silence(22050,0);
    silent_sound=njin::sound_load_samples(ctx,silence.data(),(int)silence.size(),22050);
    if(!silent_sound.id) throw std::runtime_error("audio device/sample upload failed");
    njin::sound_play_loop(ctx,silent_sound);
  }
  njin::window_set_title(ctx,portable ? "Njin renderer slice - bgfx":"Njin RHI slice - OpenGL");
  const auto position=njin::window_position(ctx);
  njin::window_set_position(ctx,position);
  njin::window_center(ctx);
  const auto monitor=njin::monitor_size(ctx);
  if(monitor.x<=0 || monitor.y<=0 || !njin::window_native_handle(ctx)) throw std::runtime_error("window platform query failed");
  njin::window_set_vsync(ctx,true);
  if(!njin::window_vsync(ctx)) throw std::runtime_error("vsync enable query failed");
  njin::window_set_vsync(ctx,false);
  if(njin::window_vsync(ctx)) throw std::runtime_error("vsync disable query failed");
  njin::cursor_set_visible(ctx,true);
  njin::cursor_set_locked(ctx,false);
  transition_scene=njin::scene_register(ctx,{.name="renderer-controls"});
  auto dialog_style=njin::dialog_default_style();
  dialog_style.font_size=16; dialog_style.padding=4; dialog_style.margin=8;
  dialog_style.max_width=120; dialog_style.lines=1; dialog_style.top=true;
  dialog_style.chars_per_second=0; dialog_style.box.normal={.color={0,1,0,1}};
  dialog_style.box.text={0,0,1,1};
  njin::dialog_set_style(ctx,dialog_style);
  njin::dialog_say(ctx,nullptr,"Njin");
  njin::ui_canvas_set(ctx,{.height=240});
  auto style=njin::ui_default_style();
  style.font_size=16; style.padding=4; style.spacing=2; style.widget_height=20;
  style.panel.normal={.color={0,1,0,1}};
  style.label.text={0,0,1,1}; style.button.normal={.color={0,0,1,1}};
  style.button.focused=style.button.pressed=style.button.normal;
  style.button.text=style.button.text_focused={1,1,1,1};
  njin::ui_style_set(ctx,style);
  njin::font_add_chars(ctx,{},"\xCE\xA9");
  const auto text_size=njin::text_measure(ctx,"Njin",16);
  const auto multiline=njin::text_measure(ctx,"Njin\nTi\xE1\xBA\xBFng Vi\xE1\xBB\x87t",16);
  if(text_size.x<=0 || text_size.y!=16 || multiline.x<text_size.x || multiline.y!=34)
    throw std::runtime_error("font metrics/UTF-8 multiline adapter mismatch");
  physics_body=njin::body3d_create(ctx,{.position={0,10,0},.motion=njin::body3d_dynamic});
  if(!physics_body.id) throw std::runtime_error("physics3d creation failed");
  if(!njin::script_run_string(ctx,"assert(type(njin) == 'table'); assert(2 + 3 == 5)","rhi-smoke"))
    throw std::runtime_error("script feature did not initialize with selected renderer");
  if(texture) {
    bool rejected=false;
    try { njin::renderer_draw_sprite(ctx,texture,0,0,1,1); }
    catch(const std::invalid_argument &) { rejected=true; }
    if(!rejected) throw std::runtime_error("foreign context texture was accepted");
  }
  texture=njin::renderer_create_texture(ctx,{2,2,pixels});
  render_target=njin::renderer_create_target(ctx,64,64);
  target_texture=njin::renderer_target_texture(ctx,render_target);
  // Keep an unrendered target live while drawing ordinary textures. Target
  // IDs must not be confused with independently allocated texture IDs.
  unused_target=njin::renderer_create_target(ctx,16,16);
  hdr_target=njin::renderer_create_hdr_target(ctx,4,4);
  bool target_owned=false;
  try { njin::renderer_destroy_texture(ctx,target_texture); }
  catch(const std::logic_error &) { target_owned=true; }
  if(!target_owned) throw std::runtime_error("render target color texture ownership was lost");
  bool outside_frame_rejected=false;
  try { njin::renderer_draw_sprite(ctx,texture,0,0,1,1); }
  catch(const std::logic_error &) { outside_frame_rejected=true; }
  if(!outside_frame_rejected) throw std::runtime_error("draw outside render phase was accepted");
  const njin::RenderVertex invalid_mesh[]{{0,0,0,0,0},{1,0,0,1,0},{0,1,0,0,1}};
  const std::uint16_t invalid_indices[]{0,1,3};
  bool invalid_index_rejected=false;
  try { (void)njin::renderer_create_mesh(ctx,invalid_mesh,invalid_indices); }
  catch(const std::invalid_argument &) { invalid_index_rejected=true; }
  if(!invalid_index_rejected) throw std::runtime_error("out-of-range mesh index was accepted");
  legacy_texture=njin::texture_from_pixels(ctx,pixels,2,2);
  if(njin::texture_load(ctx,"njin-rhi-missing-texture-8fd350e0.png").id)
    throw std::runtime_error("missing legacy texture unexpectedly loaded");
  njin::texture_set_filter(ctx,legacy_texture,njin::filter_nearest);
  std::vector<unsigned char> tile_pixels(64*64*4,255);
  for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
    auto *pixel=tile_pixels.data()+(y*64+x)*4;
    pixel[0]=y<32 && x<32 ? 255:0;
    pixel[1]=y<32 && x>=32 ? 255:0;
    pixel[2]=y>=32 ? 255:0;
  }
  tile_texture=njin::texture_from_pixels(ctx,tile_pixels.data(),64,64);
  njin::texture_set_filter(ctx,tile_texture,njin::filter_nearest);
  auto &registry=njin::world(ctx);
  tile_entity=registry.create(); registry.emplace<njin::transform>(tile_entity,njin::transform{.pos={350,370}});
  auto &map=registry.emplace<njin::tilemap>(tile_entity);
  map.tileset=tile_texture; map.tile_size={32,32};
  njin::tilemap_set(map,0,0,0); njin::tilemap_set(map,0,1,2);
  njin::tilemap_set(map,1,0,3); njin::tilemap_set(map,-1,0,2|njin::tile_flip_x);
  njin::tilemap_animate(map,3,{2,2},0.05f);
  tile_overlay=registry.create(); registry.emplace<njin::transform>(tile_overlay,njin::transform{.pos={355,370},.scale=4});
  njin::sprite top; top.texture=legacy_texture; top.source={{0,1},{1,1}}; top.origin={0,0};
  registry.emplace<njin::sprite>(tile_overlay,top);
  njin::draw_set_y_sort(ctx,0,true);
  particle_entity=registry.create(); registry.emplace<njin::transform>(particle_entity);
  auto &emitter=registry.emplace<njin::particle_emitter>(particle_entity);
  emitter.texture=legacy_texture; emitter.source={{0,1},{1,1}};
  emitter.size_start=emitter.size_end=12;
  emitter.color_start=emitter.color_end={1,1,1,1};
  emitter.emitting=false; emitter.destroy_when_done=false;
  emitter.particles.push_back({.pos={260,400},.life=1000});
  sprite_entity=njin::world(ctx).create();
  njin::world(ctx).emplace<njin::transform>(sprite_entity,njin::transform{.pos={160,120},.scale=32});
  njin::sprite spr; spr.texture=legacy_texture; spr.flip_x=true; spr.tint={1,0.5f,0.8f,0.7f};
  njin::world(ctx).emplace<njin::sprite>(sprite_entity,spr);
  camera_entity=njin::world(ctx).create();
  njin::world(ctx).emplace<njin::transform>(camera_entity);
  njin::world(ctx).emplace<njin::camera_2d>(camera_entity,njin::camera_2d{.offset={15,10},.zoom=1.1f});
  njin::world(ctx).emplace<njin::camera_on>(camera_entity);
  const auto point=njin::w2scr(ctx,{160,120});
  const auto world=njin::scr2w(ctx,point);
  if(std::abs(world.x-160)>0.001f || std::abs(world.y-120)>0.001f || std::abs(point.x-191)>0.001f || std::abs(point.y-142)>0.001f)
    throw std::runtime_error("camera coordinate adapter mismatch");
}
void shutdown(njin::context &ctx) {
  if(audio_smoke) { njin::sound_stop(ctx,silent_sound); njin::sound_unload(ctx,silent_sound); }
  const auto mesh_stats=njin::renderer_resource_stats(ctx);
  if(mesh_stats.live_meshes!=1 || mesh_stats.mesh_uploads!=1)
    throw std::runtime_error("persistent indexed mesh was not reused");
  if(limit>90 && njin::body3d_transform(ctx,physics_body).position.y>=9)
    throw std::runtime_error("physics3d fixed step did not run with the selected renderer");
  njin::body3d_destroy(ctx,physics_body);
  if(limit>20) {
    const auto sample=[&](const char *suffix,unsigned x,unsigned y) {
      const auto img=njin::renderer_load_image((capture_path+suffix).c_str());
      const auto index=(size_t(y)*img.width+x)*4;
      if(x>=img.width || y>=img.height) throw std::runtime_error("missing overlay control capture");
      return std::array<unsigned,3>{img.pixels[index],img.pixels[index+1],img.pixels[index+2]};
    };
    const auto dialog=sample(".dialog.png",262,10);
    const auto fade=sample(".fade.png",608,450);
    const auto flash=sample(".flash.png",608,450);
    if(dialog!=std::array<unsigned,3>{0,255,0} || fade!=std::array<unsigned,3>{0,0,255} ||
       flash[0]<250 || flash[1]>5 || flash[2]>5)
      throw std::runtime_error("dialog/fade/screen flash image control mismatch");
    if(njin::dialog_active(ctx) || njin::scene_current(ctx).id!=transition_scene.id || njin::scene_transitioning(ctx))
      throw std::runtime_error("dialog/scene lifecycle did not complete");
  }
  if(limit>90) {
  const auto capture=njin::renderer_load_image(capture_path.c_str());
  const auto loaded=njin::texture_load(ctx,capture_path.c_str());
  if(!loaded.id) throw std::runtime_error("legacy texture could not load the captured asset");
  const auto loaded_size=njin::texture_size(ctx,loaded);
  if(loaded_size.x!=capture.width || loaded_size.y!=capture.height)
    throw std::runtime_error("legacy decoded texture dimensions mismatch");
  njin::texture_unload(ctx,loaded);
  if(!capture.width || !capture.height || capture.width*480!=capture.height*640)
    throw std::runtime_error("capture aspect is wrong after resize");
  const auto check=[&](unsigned x,unsigned y,unsigned r,unsigned g,unsigned b,unsigned tolerance=0) {
    x=x*capture.width/640; y=y*capture.height/480;
    const auto offset=(size_t(y)*capture.width+x)*4;
    if(std::abs(int(capture.pixels[offset])-int(r))>int(tolerance) || std::abs(int(capture.pixels[offset+1])-int(g))>int(tolerance) || std::abs(int(capture.pixels[offset+2])-int(b))>int(tolerance))
      throw std::runtime_error("capture pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+
        ": got "+std::to_string(capture.pixels[offset])+","+std::to_string(capture.pixels[offset+1])+","+
        std::to_string(capture.pixels[offset+2]));
  };
  check(60,390,255,0,0); check(60,200,0,0,255);
  check(608,450,255,0,0); // near red survives a later far blue draw
  check(528,28,0,0,255); // screen-space legacy texture: negative scale/origin
  check(528,52,255,0,0); // 2D overlays the nearer 3D depth sample
  check(570,20,255,0,0); // texture_draw uses screen coordinates after world pass
  check(310,30,255,0,0); check(310,70,0,0,255); // render-to-texture orientation
  check(420,30,0,0,0); check(440,30,255,255,255); check(460,30,0,255,0); // shared material controls
  check(360,110,228,0,0); check(380,110,0,0,0); check(400,110,255,255,255); // normal transform, backlight, specular
  check(360,140,80,0,0); check(380,140,255,0,0); check(400,140,151,10,10); // GGX metal/roughness/dielectric
  check(500,140,0,0,0); check(520,140,255,0,0); // packed depth shadow and reset through the common Renderer API
  check(440,140,255,0,0); check(460,140,0,0,0); // local point reaches the surface; reversed spot does not
  check(360,180,255,155,85,1); check(380,180,255,119,119,1); check(400,180,253,150,150,1); // HDR radiance 4 through ACES / AgX / Neutral
  check(480,30,0,255,255); check(500,30,0,0,0); // additive emission and state isolation
  check(410,430,255,0,0); check(410,465,0,0,255); // orthogonal tilemap
  check(445,430,0,0,255); check(380,430,0,0,255); // animation / negative cell
  check(408,419,0,0,255); // same-layer sprite appears after the map
  check(301,450,0,0,255); // CPU simulated textured particle through the same camera
  check(46,30,0,0,255); check(54,30,255,0,0); // offset/scale plus scissor
  check(21,70,0,0,255); check(50,70,255,255,255); // rounded outline leaves its center
  check(120,30,0,0,255); check(120,70,255,0,0); check(145,50,0,0,255);
  check(304,80,0,255,0); check(312,80,0,0,255); // target-local scissor and shape orientation
  unsigned ink=0;
  for(unsigned y=20*capture.height/480;y<44*capture.height/480;++y)
    for(unsigned x=180*capture.width/640;x<270*capture.width/640;++x) {
      const auto i=(size_t(y)*capture.width+x)*4;
      if(capture.pixels[i+2]>capture.pixels[i]+32 && capture.pixels[i+2]>capture.pixels[i+1]+32) ++ink;
    }
  if(ink<10) throw std::runtime_error("font glyph draw produced no visible ink");
  unsigned cube_red=0,cube_green=0;
  for(unsigned y=345*capture.height/480;y<405*capture.height/480;++y)
    for(unsigned x=470*capture.width/640;x<550*capture.width/640;++x) {
      const auto i=(size_t(y)*capture.width+x)*4;
      cube_red+=capture.pixels[i]>220 && capture.pixels[i+1]<40 && capture.pixels[i+2]<40;
      cube_green+=capture.pixels[i+1]>170 && capture.pixels[i]<120 && capture.pixels[i+2]<120;
    }
  if(cube_red<10 || cube_green<10) throw std::runtime_error("perspective mesh produced no visible textured faces");

  check(182,62,0,255,0); // UI panel in a 2x canvas
  check(195,115,0,0,255); // button skin
  check(284,102,0,0,255); check(300,102,255,0,0); // canvas-space clip

  const auto alpha_offset=(size_t(30*capture.height/480)*capture.width+80*capture.width/640)*4;
  if(capture.pixels[alpha_offset]<126 || capture.pixels[alpha_offset]>129 ||
     capture.pixels[alpha_offset+1]<126 || capture.pixels[alpha_offset+1]>129 || capture.pixels[alpha_offset+2]!=0)
    throw std::runtime_error("shape alpha stack mismatch");
  const auto blend_pixel=[&](unsigned x,bool additive) {
    const auto i=(size_t(450*capture.height/480)*capture.width+x*capture.width/640)*4;
    const auto r=capture.pixels[i],g=capture.pixels[i+1],b=capture.pixels[i+2];
    if(g!=0 || (additive ? (r<126 || r>129 || b!=255):(r!=0 || b<126 || b>129)))
      throw std::runtime_error("additive/multiply capture mismatch");
  };
  blend_pixel(150,true); blend_pixel(220,false);
  const auto blend=(size_t(450*capture.height/480)*capture.width+64*capture.width/640)*4;
  if(capture.pixels[blend]<126 || capture.pixels[blend]>129 || capture.pixels[blend+1]!=0 ||
     capture.pixels[blend+2]<126 || capture.pixels[blend+2]>129)
    throw std::runtime_error("capture alpha blending mismatch");
  }
  if(njin::renderer_resource_stats(ctx).live_targets!=3) throw std::runtime_error("render target slot missing");
  njin::renderer_destroy_target(ctx,hdr_target);
  njin::renderer_destroy_target(ctx,unused_target);
  njin::renderer_destroy_target(ctx,render_target);
  bool target_stale=false;
  try { (void)njin::renderer_target_texture(ctx,render_target); }
  catch(const std::invalid_argument &) { target_stale=true; }
  if(!target_stale || njin::renderer_resource_stats(ctx).live_targets) throw std::runtime_error("render target was not released");
  njin::renderer_destroy_mesh(ctx,mesh);
  if(njin::renderer_resource_stats(ctx).live_meshes) throw std::runtime_error("mesh slot was not released");
  bool mesh_rejected=false;
  try { njin::renderer_destroy_mesh(ctx,mesh); }
  catch(const std::invalid_argument &) { mesh_rejected=true; }
  if(!mesh_rejected) throw std::runtime_error("stale mesh destruction was accepted");
  const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_textures!=(portable ? 5u:1u) || stats.texture_uploads!=(portable ? 5u:1u))
    throw std::runtime_error("persistent texture was not reused");
  njin::renderer_destroy_texture(ctx,texture);
  njin::world(ctx).destroy(sprite_entity); njin::world(ctx).destroy(camera_entity);
  njin::texture_unload(ctx,legacy_texture);
  njin::world(ctx).destroy(tile_entity); njin::world(ctx).destroy(tile_overlay);
  const auto &emitter=njin::world(ctx).get<njin::particle_emitter>(particle_entity);
  if(limit>90 && (emitter.gpu || emitter.particles.empty() || emitter.particles[0].age<=0))
    throw std::runtime_error("CPU particle simulation did not advance");
  njin::world(ctx).destroy(particle_entity);
  njin::texture_unload(ctx,tile_texture);
  njin::font_set_style(ctx,{},njin::font_pixel); // releases the baked smooth atlas after the submitted frame
  // The portable 2D white texture remains owned by the engine until teardown.
  if(njin::renderer_resource_stats(ctx).live_textures!=(portable ? 1u:0u)) throw std::runtime_error("game texture was not released");
  bool rejected=false;
  try { njin::renderer_destroy_texture(ctx,texture); }
  catch(const std::invalid_argument &) { rejected=true; }
  if(!rejected) throw std::runtime_error("stale texture destruction was accepted");
  rejected=false;
  try { njin::renderer_draw_sprite(ctx,texture,0,0,1,1); }
  catch(const std::invalid_argument &) { rejected=true; }
  if(!rejected) throw std::runtime_error("stale texture draw was accepted");
}
void draw(njin::context &ctx) {
  const auto size=njin::window_size(ctx);
  if(size.x<=0 || size.y<=0) throw std::runtime_error("invalid window size");
  // SDL resize requests can be asynchronous; observe acknowledgement before
  // the next request rather than assuming the OS applies it in one frame.
  if(frames>30 && frames<60 && size.x==800 && size.y==600) resize_grew=true;
  if(frames>60 && size.x==640 && size.y==480) resize_shrank=true;
  if((frames==59 && !resize_grew) || (frames==89 && !resize_shrank))
    throw std::runtime_error("window resize was not acknowledged within 29 frames");
  if(frames==30) njin::window_set_size(ctx,{800,600});
  if(frames==60) njin::window_set_size(ctx,{640,480});
  if(frames==90) njin::screenshot(ctx,capture_path.c_str());
  if(frames==0) {
    bool rejected=false;
    try { njin::renderer_draw_sprite(ctx,njin::renderer_target_texture(ctx,unused_target),0,0,1,1); }
    catch(const std::logic_error &) { rejected=true; }
    if(!rejected) throw std::runtime_error("uninitialized target sampling was accepted");
  }
  const njin::RenderImage image{2,2,pixels};
  njin::renderer_draw_sprite(ctx,texture,-0.9f,-0.7f,0.7f,1.1f);
  const njin::RenderVertex vertices[]{
    {-0.1f,-0.6f,0.5f,0,1},{0.9f,-0.6f,0.5f,1,1},{0.4f,0.8f,0.5f,0.5f,0},
    {0,-0.4f,-0.5f,0,0},{0.8f,-0.4f,-0.5f,1,0},{0.4f,0.5f,-0.5f,0.5f,1}};
  njin::MeshDraw flat{vertices,image}; flat.texture=texture;
  njin::renderer_draw_mesh(ctx,flat);
  const float points[8][3]{{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                         {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}};
  const int faces[6][4]{{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
  const int order[6]{0,1,2,0,2,3};
  const float uv[4][2]{{0,0},{1,0},{1,1},{0,1}};
  std::array<njin::RenderVertex,24> cube{};
  std::array<std::uint16_t,36> cube_indices{};
  int n=0,index=0;
  const float angle=frames*0.02f,c=std::cos(angle),s=std::sin(angle);
  for(auto &face:faces) {
    const auto base=n;
    for(int corner=0;corner<4;++corner) {
      const auto &p=points[face[corner]];
      cube[n++]={p[0],p[1],p[2],uv[corner][0],uv[corner][1]};
    }
    for(int corner:order) cube_indices[index++]=(std::uint16_t)(base+corner);
  }
  if(frames==0) mesh=njin::renderer_create_mesh(ctx,cube,cube_indices);
  njin::MeshDraw perspective{cube,image};
  perspective.texture=texture;
  perspective.mesh=mesh;
  // Keep the perspective cube visible below the flat depth-control triangles.
  perspective.transform={0.36f*c,0,0.306f*s,0.3f*s, 0,0.48f,0,0,
    0.36f*s,0,-0.306f*c,-0.3f*c, 1.7f,-1.7f,2.858f,3};
  njin::renderer_draw_mesh(ctx,perspective);
  if(portable && frames==0) {
    bool texture_busy=false,mesh_busy=false;
    try { njin::renderer_destroy_texture(ctx,texture); }
    catch(const std::logic_error &) { texture_busy=true; }
    try { njin::renderer_destroy_mesh(ctx,mesh); }
    catch(const std::logic_error &) { mesh_busy=true; }
    if(!texture_busy || !mesh_busy) throw std::runtime_error("queued GPU resource destruction was accepted");
  }
  const auto solid_triangle=[&](float x,float z,float u,float v,float alpha) {
    const njin::RenderVertex triangle[]{{x,-0.95f,z,u,v},{x+0.16f,-0.95f,z,u,v},{x+0.08f,-0.75f,z,u,v}};
    const std::uint16_t indices[]{0,1,2};
    njin::MeshDraw draw{triangle,{}}; draw.texture=texture; draw.tint[3]=alpha;
    draw.indices=indices;
    njin::renderer_draw_mesh(ctx,draw);
  };
  solid_triangle(0.82f,-0.5f,0.25f,0.25f,1); // red, near
  solid_triangle(0.82f,0.5f,0.25f,0.75f,1);  // blue, far: depth must reject
  solid_triangle(-0.9f,0.2f,0.25f,0.75f,1); // opaque blue base
  solid_triangle(-0.9f,0,0.25f,0.25f,0.5f); // half red over blue
  const njin::RenderVertex overlay_depth[]{{0.6f,0.7f,-0.8f,0.25f,0.75f},
    {0.8f,0.7f,-0.8f,0.25f,0.75f},{0.65f,0.95f,-0.8f,0.25f,0.75f}};
  njin::MeshDraw depth_sample{overlay_depth,{}}; depth_sample.texture=texture;
  njin::renderer_draw_mesh(ctx,depth_sample);
  if (++frames>=limit) njin::quit(ctx);
}
void overlay(njin::context &ctx) {
  if(frames==10) njin::screenshot(ctx,(capture_path+".dialog.png").c_str());
  if(frames==11) {
    njin::dialog_stop(ctx);
    njin::scene_fade(ctx,transition_scene,{.fade_out=0,.hold=1000,.fade_in=0,.color={0,0,1,1}});
  }
  if(frames==12) njin::screenshot(ctx,(capture_path+".fade.png").c_str());
  if(frames==13) {
    njin::scene_set(ctx,transition_scene);
    njin::screen_flash(ctx,{1,0,0,1},1000);
  }
  if(frames==14) njin::screenshot(ctx,(capture_path+".flash.png").c_str());
  if(frames==15) njin::screen_flash(ctx,{0,0,0,0},0);

  njin::draw_rect(ctx,{{20,20},{80,20}},{1,0,0,1});
  njin::clip_begin(ctx,{{20,20},{30,20}});
  njin::draw_offset_push(ctx,{20,20},2);
  njin::draw_circle(ctx,{15,5},4,{0,0,1,1});
  njin::draw_offset_pop(ctx); njin::clip_end(ctx);
  njin::draw_alpha_push(ctx,0.5f); njin::draw_rect(ctx,{{70,20},{20,20}},{0,1,0,1}); njin::draw_alpha_pop(ctx);
  njin::draw_rect_rounded_lines(ctx,{{20,60},{80,20}},5,2,{0,0,1,1});
  njin::draw_rect_rotated(ctx,{120,30},{10,10},45,{0,0,1,1});
  njin::draw_triangle(ctx,{110,60},{130,60},{120,80},{1,0,0,1});
  njin::draw_line(ctx,{145,20},{145,80},4,{0,0,1,1});
  njin::draw_text(ctx,"Njin",{180,20},16,{0,0,1,1});
  const auto material_control=[&](float x,float u,float v,float cutoff,bool opaque,std::array<float,4> color,std::array<float,3> emission={},njin::RenderLighting lighting={},std::array<float,3> normal={0,0,1},float normal_x_scale=1,float y=20,njin::RenderToneMap tonemap=njin::RenderToneMap::None,const njin::RenderLocalLight *local=nullptr,std::shared_ptr<const njin::RenderShadowData> shadows={}) {
    const float left=x/320-1,right=(x+20)/320-1,top=1-y/240,bottom=1-(y+20)/240;
    njin::RenderVertex quad[]{{left,top,0,u,v},{right,top,0,u,v},{right,bottom,0,u,v},
      {left,top,0,u,v},{right,bottom,0,u,v},{left,bottom,0,u,v}};
    for(auto &vertex:quad) { vertex.color=color; vertex.normal=normal; }
    njin::MeshDraw draw{quad,{}}; draw.texture=texture; draw.depth_test=false;
    draw.alpha_cutoff=cutoff; draw.opaque_alpha=opaque; draw.emission=emission; draw.lighting=lighting; draw.normal_transform[0]=normal_x_scale; draw.tonemap=tonemap;
    draw.shadows=std::move(shadows);
    if(local) { draw.local_lights[0]=*local; draw.local_light_count=1; }
    njin::renderer_draw_mesh(ctx,draw);
  };
  material_control(410,0.25f,0.25f,0,false,{0,1,1,1}); // red * cyan vertex color = black
  material_control(430,0.75f,0.25f,0.8f,false,{1,1,1,1}); // alpha 180/255 must be discarded
  material_control(450,0.75f,0.25f,0.5f,true,{1,1,1,1}); // surviving masked fragment writes opaque alpha

  material_control(470,0.25f,0.25f,0,false,{0,1,1,1},{0,1,1});
  material_control(490,0.25f,0.25f,0,false,{0,1,1,1}); // emission must reset per draw

  njin::RenderLighting light; light.enabled=true; light.ambient={0,0,0}; light.specular=0; light.camera={0,0,10000};
  material_control(350,0.25f,0.25f,0,false,{1,1,1,1},{},light,{0.70710678f,0,0.70710678f},0.5f,100);
  material_control(370,0.25f,0.25f,0,false,{1,1,1,1},{},light,{0,0,-1},1,100);
  light.specular=1;
  material_control(390,0.25f,0.25f,0,false,{0,1,1,1},{},light,{0,0,1},1,100);

  light.pbr=true; light.metallic=1; light.roughness=1;
  material_control(350,0.25f,0.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::Linear);
  light.roughness=0.5f;
  material_control(370,0.25f,0.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::Linear);
  light.metallic=0; light.roughness=1;
  material_control(390,0.25f,0.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::Linear);
  light.pbr=false; light.color={0,0,0}; light.specular=0;
  njin::RenderLocalLight local; local.position={440.0f/320-1,1-140.0f/240,2}; local.radius=4; local.intensity=4;
  material_control(430,0.25f,0.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::None,&local);
  local.spot=true; local.position[0]=460.0f/320-1; local.direction={0,0,1};
  material_control(450,0.25f,0.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::None,&local);
  njin::renderer_begin_target(ctx,hdr_target,{0,0,0,1});
  const njin::RenderVertex full[]{{-1,1,0,0.25f,0.25f},{1,1,0,0.25f,0.25f},{1,-1,0,0.25f,0.25f},
    {-1,1,0,0.25f,0.25f},{1,-1,0,0.25f,0.25f},{-1,-1,0,0.25f,0.25f}};
  njin::MeshDraw energy{full,{}}; energy.texture=texture; energy.tint={0,0,0,1}; energy.depth_test=false;
  energy.emission={1,0,0}; energy.emission_strength=4; energy.linear_output=true;
  njin::renderer_draw_mesh(ctx,energy);
  njin::renderer_end_target(ctx);
  njin::RenderVertex resolved[]{{350.0f/320-1,1-170.0f/240,0,0,0},{370.0f/320-1,1-170.0f/240,0,1,0},{370.0f/320-1,1-190.0f/240,0,1,1},
    {350.0f/320-1,1-170.0f/240,0,0,0},{370.0f/320-1,1-190.0f/240,0,1,1},{350.0f/320-1,1-190.0f/240,0,0,1}};
  njin::MeshDraw resolve{resolved,{}}; resolve.texture=njin::renderer_target_texture(ctx,hdr_target);
  resolve.depth_test=false; resolve.resolve=true; resolve.opaque_alpha=true; resolve.tonemap=njin::RenderToneMap::ACES;
  njin::renderer_draw_mesh(ctx,resolve);
  for(auto &v:resolved) v.x+=20.0f/320;
  resolve.tonemap=njin::RenderToneMap::AgX; njin::renderer_draw_mesh(ctx,resolve);
  for(auto &v:resolved) v.x+=20.0f/320;
  resolve.tonemap=njin::RenderToneMap::Neutral; njin::renderer_draw_mesh(ctx,resolve);

  const auto blend_swatch=[&](float center,njin::RenderBlend blend) {
    njin::RenderVertex quad[]{{center-0.05f,-0.95f,0,0.25f,0.75f},{center+0.05f,-0.95f,0,0.25f,0.75f},
      {center+0.05f,-0.8f,0,0.25f,0.75f},{center-0.05f,-0.95f,0,0.25f,0.75f},
      {center+0.05f,-0.8f,0,0.25f,0.75f},{center-0.05f,-0.8f,0,0.25f,0.75f}};
    njin::MeshDraw swatch{quad,{}}; swatch.texture=texture; swatch.depth_test=false;
    njin::renderer_draw_mesh(ctx,swatch);
    for(auto &vertex:quad) vertex.v=0.25f;
    swatch.blend=blend; swatch.tint[3]=0.5f;
    njin::renderer_draw_mesh(ctx,swatch);
  };
  blend_swatch(150.0f/320-1,njin::RenderBlend::Additive);
  blend_swatch(220.0f/320-1,njin::RenderBlend::Multiply);
  njin::renderer_begin_target(ctx,hdr_target,{1,1,1,1});
  njin::RenderVertex caster[6]; std::copy(std::begin(full),std::end(full),caster);
  for(auto &v:caster) v.z=-.5f;
  njin::MeshDraw depth_packet{caster,{}}; depth_packet.texture=texture; depth_packet.shadow_pass=true;
  njin::renderer_draw_mesh(ctx,depth_packet); njin::renderer_end_target(ctx);
  auto shadows=std::make_shared<njin::RenderShadowData>();
  shadows->texture=njin::renderer_target_texture(ctx,hdr_target); shadows->views=1;
  shadows->width=shadows->height=4; shadows->regions[0]={0,0,1,1};
  shadows->forward={0,0,-1,1}; shadows->splits[0]=100000;
  for(unsigned i=0;i<4;++i) shadows->matrices[i][i]=1;
  light.color={1,1,1}; light.pbr=false;
  material_control(490,.25f,.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130,njin::RenderToneMap::None,nullptr,shadows);
  material_control(510,.25f,.25f,0,true,{1,1,1,1},{},light,{0,0,1},1,130);
  njin::renderer_begin_target(ctx,render_target,{0,0,0,1});
  bool feedback_rejected=false,nested_rejected=false,active_rejected=false;
  try { njin::renderer_draw_sprite(ctx,target_texture,-1,-1,2,2); }
  catch(const std::logic_error &) { feedback_rejected=true; }
  try { njin::renderer_begin_target(ctx,render_target); }
  catch(const std::logic_error &) { nested_rejected=true; }
  try { njin::renderer_destroy_target(ctx,render_target); }
  catch(const std::logic_error &) { active_rejected=true; }
  if(!feedback_rejected || !nested_rejected || !active_rejected) throw std::runtime_error("render target pass validation failed");
  njin::renderer_draw_sprite(ctx,texture,-1,-1,2,2);
  njin::clip_begin(ctx,{{0,0},{8,8}});
  njin::draw_rect(ctx,{{0,0},{16,16}},{0,1,0,1});
  njin::clip_end(ctx);
  njin::renderer_end_target(ctx);
  njin::renderer_draw_sprite(ctx,target_texture,-0.0625f,0.65f,0.2f,64.0f/240);
  const njin::RenderVertex far[]{{0.82f,-0.95f,0.5f,0.25f,0.75f},
    {0.98f,-0.95f,0.5f,0.25f,0.75f},{0.9f,-0.75f,0.5f,0.25f,0.75f}};
  njin::MeshDraw resume_depth{far,{}}; resume_depth.texture=texture;
  njin::renderer_draw_mesh(ctx,resume_depth);
  njin::texture_draw_ex(ctx,legacy_texture,{.pos={528,40},.source={{0,0},{1,2}},
    .scale={-32,-16},.origin={0.25f,0.5f}});
  njin::texture_draw(ctx,legacy_texture,{570,20},{1,1,1,1});
  njin::ui_canvas_begin(ctx);
  njin::ui_canvas_begin(ctx); // nesting must not apply the scale twice
  if(std::abs(njin::screen_size(ctx).y*njin::ui_canvas_scale(ctx)-njin::window_size(ctx).y)>0.01f) throw std::runtime_error("UI canvas logical size mismatch");
  njin::ui_begin(ctx,{.id="slice-ui",.anchor={0,0},.pivot={0,0},.offset={90,30},.width=50,.navigable=false});
  njin::ui_label(ctx,"UI");
  (void)njin::ui_button(ctx,"OK");
  njin::ui_progress_circle(ctx,{.value=0.75f,.diameter=16,.thickness=3,.round_caps=true,
    .track={0,0,1,1},.fill={1,0,0,1}});
  njin::ui_end(ctx);
  njin::draw_rect(ctx,{{140,50},{16,4}},{1,0,0,1});
  njin::clip_begin(ctx,{{140,50},{4,4}});
  njin::draw_rect(ctx,{{140,50},{16,4}},{0,0,1,1});
  njin::clip_end(ctx);
  njin::ui_canvas_end(ctx); njin::ui_canvas_end(ctx);

}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);
  njin::ecs_register(ctx,njin::phase_render,draw);
  njin::ecs_register(ctx,njin::phase_post_render,overlay);
  njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  static_assert(njin::config{}.graphics_backend==njin::GraphicsBackend::Auto);
  try {
    njin::config cfg{.title="Njin RHI slice",.width=640,.height=480,.target_fps=60};
    cfg.audio=false; cfg.resizable=true;
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) {
        if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");
      } else if(std::strcmp(argv[i],"--gpu-validation")==0) {
        cfg.graphics_validation=true;
      } else if(std::strcmp(argv[i],"--audio-smoke")==0) {
        cfg.audio=true; audio_smoke=true;
      } else if(std::strncmp(argv[i],"--frames=",9)==0) {
        limit=std::stoi(argv[i]+9);
        if(limit<1 || limit>12000) throw std::invalid_argument("frame limit must be 1..12000");
      } else throw std::invalid_argument("unknown argument");
    }
    for(int repeat=0;repeat<2;++repeat) {
      portable=cfg.graphics_backend!=njin::GraphicsBackend::OpenGL;
      backend_label=std::string(njin::graphics_backend_name(cfg.graphics_backend));
      capture_path=(std::filesystem::path(argv[0]).parent_path()/"captures"/("rhi-"+backend_label+".png")).string();
      cfg.title=portable ? "Njin renderer slice - bgfx" : "Njin RHI slice - OpenGL";
      frames=0;
      std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="rhi_slice",.setup=setup});
      njin::run(*ctx);
    }
    return 0;
  } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
