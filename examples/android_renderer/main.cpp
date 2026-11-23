#include <SDL3/SDL.h>
#include <android/log.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

// Run the same Njin scene/API regression used on desktop; SDLActivity only
// supplies the native entry point and platform lifecycle.
#define main njin_android_model_test
#include "../../src/engine/tests/models3d.cpp"
#undef main
int njin_android_shadow_test(int,char**);
int njin_android_animation_test(int,char**);
int njin_android_environment_test(int,char**);
int njin_android_lightmap_test(int,char**);
int njin_android_lightmap_model_test(int,char**);
int njin_android_atmosphere_test(int,char**);
int njin_android_atmosphere_full_test(int,char**);
int njin_android_materials_test(int,char**);
int njin_android_post_test(int,char**);
int njin_android_ssao_test(int,char**);
int njin_android_ssr_test(int,char**);
int njin_android_effects_test(int,char**);
int njin_android_smaa_test(int,char**);
int njin_android_exposure_test(int,char**);
int njin_android_oit_test(int,char**);
int njin_android_mipmaps_test(int,char**);
int njin_android_streaming_test(int,char**);
int njin_android_residency_test(int,char**);
int njin_android_temporal_test(int,char**);
int njin_android_person_test(int,char**);
int njin_android_crisp_text_test(int,char**);
int njin_android_world_fx_test(int,char**);
int njin_android_renderer_batch_test(int,char**);
int njin_android_world3d_test(int,char**);
int njin_android_advanced_material_test(int,char**);
int njin_android_raytrace_test(int,char**);
int njin_android_backdrop_test(int,char**);
int njin_android_light2d_test(int,char**);
int njin_android_gpu_particles_test(int,char**);
int njin_android_clustered_test(int,char**);
int njin_android_render_textures_test(int,char**);
int njin_android_atlas_test(int,char**);
int njin_android_virtual_screen_test(int,char**);
int njin_android_video_test(int,char**);
int njin_android_dynamic_mesh_test(int,char**);
int njin_android_lod_test(int,char**);
int njin_android_custom_shader_test(int,char**);
int njin_android_sprite_fx_test(int,char**);
int njin_android_gizmos_test(int,char**);
int njin_android_terrain_relief_test(int,char**);
int njin_android_occlusion_test(int,char**);
int njin_android_instancing_test(int,char**);
int njin_android_ocean_test(int,char**);


extern "C" SDL_DECLSPEC int SDL_main(int argc,char **argv) {
  const char *storage=SDL_GetAndroidInternalStoragePath();
  if(!storage || chdir(storage)!=0) {
    __android_log_print(ANDROID_LOG_ERROR,"NjinAndroid","App storage is unavailable");
    return 2;
  }
  SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD,"0"); // this regression has no text-entry fields
  SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft");
  const auto result_path=std::filesystem::path(storage)/"result.txt";
  std::ofstream(result_path) << "RUNNING\n";
  std::ofstream(std::filesystem::path(storage)/"frame-ready.txt") << "STARTING\n";
  std::ofstream(std::filesystem::path(storage)/"input-ready.txt") << "STARTING\n";
  std::string executable=(std::filesystem::path(storage)/"njin_model_test").string();
  std::vector<char*> arguments{executable.data()};
  std::string suite="models";
  for(int i=1;i<argc;++i) {
    if(std::strcmp(argv[i],"--shadow-suite")==0) suite="shadows";
    else if(std::strncmp(argv[i],"--suite=",8)==0) suite=argv[i]+8;
    else arguments.push_back(argv[i]);
  }
  arguments.push_back(nullptr);
  __android_log_print(ANDROID_LOG_INFO,"NjinAndroid","Starting Njin renderer regression");
  const int result=suite=="person"?njin_android_person_test((int)arguments.size()-1,arguments.data()):
    suite=="occlusion"?njin_android_occlusion_test((int)arguments.size()-1,arguments.data()):
    suite=="instancing"?njin_android_instancing_test((int)arguments.size()-1,arguments.data()):
    suite=="terrain_relief"?njin_android_terrain_relief_test((int)arguments.size()-1,arguments.data()):
    suite=="ocean"?njin_android_ocean_test((int)arguments.size()-1,arguments.data()):
    suite=="ssao"?njin_android_ssao_test((int)arguments.size()-1,arguments.data()):
    suite=="ssr"?njin_android_ssr_test((int)arguments.size()-1,arguments.data()):
    suite=="effects"?njin_android_effects_test((int)arguments.size()-1,arguments.data()):
    suite=="smaa"?njin_android_smaa_test((int)arguments.size()-1,arguments.data()):
    suite=="exposure"?njin_android_exposure_test((int)arguments.size()-1,arguments.data()):
    suite=="oit"?njin_android_oit_test((int)arguments.size()-1,arguments.data()):
    suite=="mipmaps"?njin_android_mipmaps_test((int)arguments.size()-1,arguments.data()):
    suite=="streaming"?njin_android_streaming_test((int)arguments.size()-1,arguments.data()):
    suite=="residency"?njin_android_residency_test((int)arguments.size()-1,arguments.data()):
    suite=="temporal"?njin_android_temporal_test((int)arguments.size()-1,arguments.data()):
    suite=="custom_shader"?njin_android_custom_shader_test((int)arguments.size()-1,arguments.data()):
    suite=="sprite_fx"?njin_android_sprite_fx_test((int)arguments.size()-1,arguments.data()):
    suite=="gizmos"?njin_android_gizmos_test((int)arguments.size()-1,arguments.data()):
    suite=="render_textures"?njin_android_render_textures_test((int)arguments.size()-1,arguments.data()):
    suite=="atlas"?njin_android_atlas_test((int)arguments.size()-1,arguments.data()):
    suite=="virtual_screen"?njin_android_virtual_screen_test((int)arguments.size()-1,arguments.data()):
    suite=="video"?njin_android_video_test((int)arguments.size()-1,arguments.data()):
    suite=="dynamic_mesh"?njin_android_dynamic_mesh_test((int)arguments.size()-1,arguments.data()):
    suite=="lod"?njin_android_lod_test((int)arguments.size()-1,arguments.data()):
    suite=="advanced_material"?njin_android_advanced_material_test((int)arguments.size()-1,arguments.data()):
    suite=="lightmap"?njin_android_lightmap_test((int)arguments.size()-1,arguments.data()):
    suite=="lightmap_model"?njin_android_lightmap_model_test((int)arguments.size()-1,arguments.data()):
    suite=="raytrace"?njin_android_raytrace_test((int)arguments.size()-1,arguments.data()):
    suite=="backdrop"?njin_android_backdrop_test((int)arguments.size()-1,arguments.data()):
    suite=="light2d"?njin_android_light2d_test((int)arguments.size()-1,arguments.data()):
    suite=="gpu_particles"?njin_android_gpu_particles_test((int)arguments.size()-1,arguments.data()):
    suite=="clustered"?njin_android_clustered_test((int)arguments.size()-1,arguments.data()):
    suite=="world3d"?njin_android_world3d_test((int)arguments.size()-1,arguments.data()):
    suite=="renderer_batch"?njin_android_renderer_batch_test((int)arguments.size()-1,arguments.data()):
    suite=="world_fx"?njin_android_world_fx_test((int)arguments.size()-1,arguments.data()):
    suite=="crisp_text"?njin_android_crisp_text_test((int)arguments.size()-1,arguments.data()):
    suite=="animation"?njin_android_animation_test((int)arguments.size()-1,arguments.data()):
    suite=="atmosphere"?njin_android_atmosphere_test((int)arguments.size()-1,arguments.data()):
    suite=="atmosphere_full"?njin_android_atmosphere_full_test((int)arguments.size()-1,arguments.data()):
    suite=="environment"?njin_android_environment_test((int)arguments.size()-1,arguments.data()):
    suite=="materials"?njin_android_materials_test((int)arguments.size()-1,arguments.data()):
    suite=="post"?njin_android_post_test((int)arguments.size()-1,arguments.data()):
    suite=="shadows"?njin_android_shadow_test((int)arguments.size()-1,arguments.data()):
    njin_android_model_test((int)arguments.size()-1,arguments.data());
  std::ofstream(result_path) << (result==0?"PASS":"FAIL") << " " << result << "\n";
  __android_log_print(result==0?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"NjinAndroid","Renderer regression returned %d",result);
  return result;
}
