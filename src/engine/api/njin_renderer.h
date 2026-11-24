#pragma once
#include <span>
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <string>
namespace njin {
struct context;
struct RenderImageAsset;
struct RenderTextureHandle {
  std::uint64_t id=0;
  explicit operator bool() const { return id!=0; }
};
struct RenderMeshHandle {
  std::uint64_t id=0;
  explicit operator bool() const { return id!=0; }
};
struct RenderTargetHandle {
  std::uint64_t id=0;
  explicit operator bool() const { return id!=0; }
};
// Targets own their color texture. The borrowed texture handle expires with
// the target and must not be destroyed separately. No nested/feedback passes.
RenderTargetHandle renderer_create_target(context &, unsigned width, unsigned height);
RenderTargetHandle renderer_create_hdr_target(context &, unsigned width, unsigned height);
RenderTextureHandle renderer_target_texture(context &, RenderTargetHandle);
// Borrowed sampled depth, where supported; owned by the target like its color.
RenderTextureHandle renderer_target_depth_texture(context &, RenderTargetHandle);
void renderer_destroy_target(context &, RenderTargetHandle);
// Synchronous readback in top-left row order; flushes GPU work without presenting.
RenderImageAsset renderer_read_target(context &, RenderTargetHandle);
void renderer_begin_target(context &, RenderTargetHandle, std::array<float,4> clear={0,0,0,0});
void renderer_end_target(context &);
// Resume an initialized target without clearing its color or depth contents.
void renderer_resume_target(context &, RenderTargetHandle);
struct RenderVertex {
  float x, y, z, u, v;
  float w = 1;
  std::array<float,4> color{1,1,1,1};
  std::array<float,3> normal{0,0,1};
  std::array<float,2> uv1{};
  std::array<float,4> tangent{}; // xyz direction, w handedness; w=0 derives the frame from UV gradients
  std::array<float,2> lightmap_uv{}; ///< UV lightmap riêng; không thay UV0/secondary UV của material.
};
// CPU assets are copied on submission. No native GPU objects cross this API.
enum class RenderImageFormat { RGBA8, RGBA16F, RGBA32F };
constexpr unsigned render_image_pixel_bytes(RenderImageFormat format) {return format==RenderImageFormat::RGBA32F?16:format==RenderImageFormat::RGBA16F?8:4;}
enum class RenderMipMode { None, Linear, SRGB, Normal };
struct RenderImage { unsigned width, height; std::span<const std::uint8_t> rgba; RenderImageFormat format=RenderImageFormat::RGBA8; RenderMipMode mips=RenderMipMode::None; };
struct RenderImageAsset {
  unsigned width=0,height=0;
  std::vector<std::uint8_t> pixels;
  RenderImageFormat format=RenderImageFormat::RGBA8;
  RenderMipMode mips=RenderMipMode::None;
  RenderImage view() const { return {width,height,pixels,format,mips}; }
};
RenderImageAsset renderer_load_image(const char *path);
RenderTextureHandle renderer_create_texture(context &, const RenderImage &);
void renderer_update_texture(context &, RenderTextureHandle, const RenderImage &);
void renderer_destroy_texture(context &, RenderTextureHandle);
RenderMeshHandle renderer_create_mesh(context &, std::span<const RenderVertex>, std::span<const std::uint16_t> indices={});
void renderer_destroy_mesh(context &, RenderMeshHandle);
struct RendererResourceStats {
  std::uint64_t live_textures=0, texture_uploads=0;
  std::uint64_t live_meshes=0, mesh_uploads=0;
  std::uint64_t live_targets=0;
  std::uint64_t instanced_draws=0,instances_submitted=0,instance_uploads=0;
  std::uint64_t deformed_mesh_uploads=0;
  std::uint64_t ray_traces=0; // compute ray tracing passes dispatched (light3d::ray_traced_shadows)
  std::uint64_t ray_bvh_builds=0; // ray tracing scenes (BVH) built; an unchanged scene reuses its BVH
  std::uint64_t ray_query_dispatches=0; // ray tracing dispatches run on the GPU's ray tracing hardware (Vulkan ray queries)
  std::uint64_t occlusion_builds=0,occlusion_dispatches=0,occlusion_indirect_draws=0;
  std::uint64_t occlusion_buffer_allocations=0; ///< Số buffer culling đã cấp, cộng dồn.
  std::uint64_t occlusion_pool_slots=0; ///< Số slot pool culling đang giữ.
  std::uint64_t occlusion_pool_bytes=0; ///< Byte buffer pool yêu cầu, chưa gồm overhead driver.
};
RendererResourceStats renderer_resource_stats(context &);
/// GPU có compute, indirect và định dạng cần cho Hi-Z của renderer hiện tại.
bool renderer_gpu_occlusion_available(context &);
/// Chẩn đoán capability, giới hạn và đường GPU occlusion đang được chọn.
std::string renderer_gpu_occlusion_diagnostics(context &);
/// Đọc đồng bộ số instance đầu vào/còn thấy của các lượt Hi-Z trong frame gần nhất.
/// Chỉ dùng cho kiểm chứng/debug: hàm đợi GPU, không dùng trong vòng lặp thông thường.
std::array<std::uint64_t,2> renderer_occlusion_readback(context &);
/// Đọc đồng bộ tổng caster đầu vào/còn lại của các shadow view trên GPU.
std::array<std::uint64_t,2> renderer_shadow_culling_readback(context &);
// Premultiplied: colour already multiplied by alpha (src ONE, dst ONE_MINUS_SRC_ALPHA).
enum class RenderBlend { Alpha, Additive, Multiply, Opaque, WeightedAdd, Reveal, Premultiplied };
enum class RenderCull { None, Clockwise, CounterClockwise };
// Logical pixels of the current target, with top-left origin.
struct RenderScissor { int x=0,y=0; unsigned width=0,height=0; };
enum class RenderToneMap { None, Linear, ACES, AgX, Neutral };
struct RenderLighting {
  bool enabled=false;
  std::array<float,3> direction{0,0,-1}, color{1,1,1}, ambient{0.35f,0.35f,0.4f}, camera{0,0,1};
  float specular=0.25f, shininess=32;
  bool pbr=false;
  float metallic=0, roughness=0.5f, intensity=1;
};
struct RenderLocalLight {
  std::array<float,3> position{0,0,0}, direction{0,-1,0}, color{1,1,1};
  float radius=10, intensity=1, cos_inner=0.9f, cos_outer=0.8f;
  bool spot=false;
};
enum class RenderWrap { Repeat, Clamp, Mirror };
enum class RenderMinFilter { Inherit, Nearest, Linear, NearestMipNearest, LinearMipNearest, NearestMipLinear, LinearMipLinear };
struct RenderUvTransform {
  std::array<float,6> values{1,0,0,1,0,0}; // 2x2 rows followed by translation
  unsigned set=0;
};
struct RenderTextureMap {
  RenderTextureHandle texture;
  RenderUvTransform uv;
  bool linear=true;
  RenderWrap wrap_s=RenderWrap::Repeat,wrap_t=RenderWrap::Repeat;
  RenderMinFilter min_filter=RenderMinFilter::Inherit;
};
struct RenderMaterialMaps {
  // Tangent-space +Y normal; linear G=roughness/B=metallic; linear R=AO; sRGB emission.
  RenderTextureMap normal,metal_rough,occlusion,emission;
  float normal_scale=1,occlusion_strength=1;
  std::array<float,4> emission_factor{1,1,1,1}; // linear RGB multiplier, strength
  bool constant_emission=false;
  RenderTextureMap height,lightmap,under,under_normal;
  // world-UV metres, exposed under-layer fraction, parallax UV depth, baked-light intensity.
  std::array<float,4> advanced{0,0,0,1};
  bool lightmap_linear=false;
  RenderTextureMap transmission,thickness; // linear R and G respectively
  // KHR_materials_clearcoat: linear R scales the coat, linear G its roughness;
  // tangent-space +Y coat normal (geometric normal frame). bgfx binds at most two
  // distinct textures here, and not together with transmission or water.
  RenderTextureMap clearcoat,clearcoat_roughness,clearcoat_normal;
  float clearcoat_normal_scale=1;
  bool lightmap_static_direct=false; ///< Lightmap E/pi thay toàn bộ ánh sáng diffuse tĩnh.
};
inline constexpr unsigned render_local_light_max=16;
// One packed-depth atlas: four sun cascades plus up to four shadowed local
// sources (six faces per point source). Matrix rows are OpenGL clip transforms.
struct RenderShadowData {
  RenderTextureHandle texture;
  std::array<std::array<float,4>,32> matrices{};
  std::array<std::array<float,4>,28> regions{}, metadata{};
  std::array<float,4> splits{}, forward{};
  std::array<int,1024> local_base{};
  // light3d::ray_traced_shadows: the ray traced mask's channel (1..3) of a shadowed
  // local source, 0 when it keeps its shadow map. Read only by ray traced draws.
  std::array<std::uint8_t,1024> ray_channel{};
  unsigned views=0, width=0, height=0;
  float softness=1;
  RenderShadowData() { local_base.fill(-1); }
};
struct RenderEnvironmentData {
  RenderTextureHandle base,atlas;
  std::array<std::array<float,4>,9> sh{}; // radiance SH9, linear RGB
  float intensity=1,rotation=0; // rotation in radians
  unsigned levels=6,level_height=64;
};
struct RenderClusterData {
  RenderTextureHandle texture;
  std::array<std::array<float,4>,4> parameters{};
  std::array<float,16> view{},view_projection{};
};
struct RenderInstance {
  std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  std::array<float,4> color{1,1,1,1};
  std::array<float,4> data{}; // optional engine-defined instance attributes
};
enum class RenderPostMode { None, BloomDown, BloomUp, Resolve, FXAA, SSAO, AOBlurX, AOBlurY, ApplyAO, SSR, Outline, ShaftMask, ShaftBlur, ShaftComposite, FlareVisibility, FlareComposite, SMAAEdge, SMAAWeights, SMAABlend, Luminance, LuminanceDown, ExposureAdapt, OITComposite, VelocityDepth, TAA, LinearDepth, MotionBlur, LegacyPost, LegacyBlur, LegacyBright, LegacyDOF, LightAmbient, LightDirect, LightResolve, Volume, BackdropMask, LightGBuffer };
struct RenderPostProcess {
  RenderPostMode mode=RenderPostMode::None;
  RenderTextureHandle auxiliary,secondary;
  std::array<float,2> texel{};
  float amount=0,levels=1;
  bool karis=false,auxiliary_linear=true;
  float radius=.6f,samples=12,thickness=.5f;
  std::array<float,4> sky{};
  std::array<float,16> projection{},inverse_projection{};
  std::array<float,4> sun{.5f,.5f,0,1}; // viewport UV, visibility, aspect
  float edge_cos=-2,fade=0,halo=.5f;
  bool auto_exposure=false,reset=false;
  RenderTextureHandle depth,velocity;
  std::array<std::array<float,4>,10> legacy{};
  std::array<float,16> previous_clip{},previous_view{};
  std::array<float,4> temporal{};
};
struct RenderGeometryData {
  std::vector<RenderVertex> vertices;
  std::vector<std::uint16_t> indices;
};
/// Dữ liệu chuẩn bị culling, độc lập backend; ma trận dùng clip OpenGL của Njin.
struct RenderGpuCull {
  std::array<float,4> bounds{}; ///< Tâm local XYZ và bán kính bao kín hình học.
  std::array<float,16> view_projection{}; ///< Projection chưa tile của camera hoặc đèn.
  std::array<float,4> camera{}; ///< Vị trí XYZ; W là tan(fovy/2) hoặc nửa chiều cao ortho.
  std::array<float,4> lod{}; ///< Threshold, bucket hiện tại, bucket tối đa, cờ ortho.
  bool frustum=true; ///< Thử bounds với frustum của view này.
  bool occlusion=false; ///< Thử Hi-Z cùng frame của camera.
  bool select_lod=false; ///< Chọn bucket LOD theo quy ước model_lod_level.
  unsigned view_key=0; ///< Camera là 0; shadow view độc lập bắt đầu từ 1.
  std::array<std::array<float,4>,6> camera_planes{}; ///< Plane camera chuẩn hóa cho frustum và LOD caster.
  bool shadow_lod=false; ///< Caster trong camera dùng LOD camera; ngoài camera dùng base.
};
// Engine-owned shader identifiers and immutable per-draw values; no driver handles.
struct RenderShaderHandle {
  std::uint64_t id=0;
  explicit operator bool() const { return id!=0; }
};
struct RenderShaderUniform {
  std::string name;
  std::vector<std::array<float,4>> values;
};
struct RenderShaderTexture {
  std::string name;
  unsigned stage=0;
  RenderTextureHandle texture; // empty samples the draw's base image
  bool linear=false;
};
struct RenderShaderBindings {
  RenderShaderHandle program;
  std::vector<RenderShaderUniform> uniforms;
  std::vector<RenderShaderTexture> textures;
};
struct RenderSpriteEffects {
  std::array<float,4> flash{}; // RGB, mix amount
  std::array<float,4> edge{}; // RGB, mix amount
  std::array<float,4> dissolve{}; // progress, grain in source texels, seed, edge width
  bool dissolving=false;
};
struct RenderWorldEffects {
  std::array<float,4> flash{},edge{};
  std::array<float,4> dissolve{0,.1f,0,0}; // amount, grain, seed, edge width
  std::array<float,4> from{},to{},mask{},camera{}; // from.w=sweep; mask.w=radius
};
// Procedural sky of draw_sky3d(), drawn by a draw with this packet: towards the sun +
// day; sun colour; zenith; horizon; ground; sunset glow; twilight, cloud cover, cloud
// darkness, sun size; cloud height, cloud scale, stars, fog; fog colour; wind drift XZ;
// eye position. Colours are display (sRGB) values as the legacy sky. No GPU objects.
// Weather cover of draw_sky3d() rain/snow: a top-down packed-depth map of the casters
// round the camera (optional; rows are its OpenGL clip transform) and up to 16 boxes
// (centre, half size). No native GPU objects.
struct RenderCoverData {
  RenderTextureHandle texture;
  std::array<std::array<float,4>,4> rows{};
  float bias=0;
  std::array<std::array<float,4>,32> boxes{};
  unsigned box_count=0;
};
struct RenderSkyEffects {
  std::array<std::array<float,4>,11> rows{};
  bool backdrop=true; // false: only what reflections (water) sample, not a sky quad
};
struct RenderWaterEffects {
  // ice/snow; ice RGB; pour position XZ, strength, radius; eight splash records
  // (position XZ, age, strength); radii in rows 11/12. Ice row Z/W = refraction/reflection;
  // ice-color alpha = caustic strength. No native GPU objects.
  std::array<std::array<float,4>,13> parameters{};
  // Appended to preserve callers using the original 13-row packet prefix.
  std::array<float,4> absorption{1,1,1,6}; // per-metre transmittance RGB, depth fade
  std::array<float,4> shallow_color{.1f,.42f,.45f,1},deep_color{.02f,.09f,.16f,1};
};
struct RenderTerrainImprint {
  RenderTextureHandle texture;
  std::array<float,4> window{}; // origin XZ, metres per texel, side in texels
  std::array<float,4> relief{}; // depth, rim, lifetime, fade
  std::array<float,4> tint{};
};
/// Dữ liệu relief địa hình độc lập backend; các texture được renderer sở hữu.
struct RenderTerrainRelief {
  std::array<RenderTextureHandle,3> textures{};
  // Gốc XZ/bước lưới/số mẫu; camera/tầm; độ sâu; tile; kích thước map/bước/lớp; camera cũ.
  std::array<std::array<float,4>,6> rows{};
};
struct MeshDraw {
  std::span<const RenderVertex> vertices;
  RenderImage image;
  std::array<float, 16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  // Optional persistent texture; when supplied, image is ignored.
  RenderTextureHandle texture{};
  RenderMeshHandle mesh{};
  std::array<float,4> tint{1,1,1,1};
  bool linear_sampling=false;
  std::span<const std::uint16_t> indices{};
  bool depth_write=true;
  bool depth_test=true; // false disables both depth testing and depth writes
  RenderBlend blend=RenderBlend::Alpha;
  bool scissor_enabled=false;
  RenderScissor scissor{};
  float alpha_cutoff=0; // discard fragments below this alpha; 0 disables
  RenderCull cull=RenderCull::None;
  bool opaque_alpha=false; // alpha still participates in cutoff, then output alpha is 1
  std::array<float,3> emission{0,0,0}; // additive RGB; finite, nonnegative, current LDR target clamps
  RenderLighting lighting{};
  std::array<float,16> world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  std::array<float,16> normal_transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  RenderToneMap tonemap=RenderToneMap::None;
  float exposure=1, emission_strength=1;
  bool linear_output=false; // write radiance into an HDR target
  bool resolve=false; // sample linear HDR radiance, tone map and encode sRGB
  std::array<RenderLocalLight,render_local_light_max> local_lights{};
  unsigned local_light_count=0;
  std::shared_ptr<const RenderShadowData> shadows;
  bool shadow_pass=false, cast_shadow=true, receive_shadow=true, shadow_only=false;
  RenderMaterialMaps maps;
  RenderUvTransform color_uv;
  RenderWrap wrap_s=RenderWrap::Repeat,wrap_t=RenderWrap::Repeat;
  std::shared_ptr<const RenderEnvironmentData> environment;
  bool environment_sky=false;
  bool normal_pass=false;
  float reflectivity=0;
  std::array<float,16> view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  RenderPostProcess post;
  std::shared_ptr<const RenderGeometryData> geometry;
  std::shared_ptr<const std::vector<RenderInstance>> instances;
  bool transparent=false;
  unsigned transparency_pass=0;
  RenderTextureHandle opaque_depth;
  std::array<float,16> transparency_projection{};
  RenderMinFilter color_min_filter=RenderMinFilter::Inherit;
  bool velocity_pass=false,motion_recorded=false;
  std::array<float,16> motion_current{},motion_previous{},motion_view{};
  std::shared_ptr<const RenderGeometryData> previous_geometry;
  std::shared_ptr<const RenderShaderBindings> shader;
  RenderSpriteEffects sprite_effects;
  std::array<float,4> particle_effects{}; // style+1, time, shape, ribbon intensity
  std::shared_ptr<const std::array<std::array<float,4>,64>> light_edges;
  RenderWorldEffects world_effects;
  // CPU-only extension packets, snapshotted by the high-level renderer.
  std::array<std::array<float,4>,4> surface_effects{}; // fog, toon, rim, coat/aniso
  // Append extensions so existing aggregate initializer prefixes stay valid.
  std::shared_ptr<const RenderClusterData> clusters;
  bool persistent_instances=false; // immutable spawn snapshots may reuse GPU storage across frames
  std::array<std::array<float,4>,2> transmission{}; // amount/ior/thickness/distance; attenuation RGB
  std::array<std::array<float,4>,6> world_style{}; // terrain layers, grass, water
  std::shared_ptr<const RenderWaterEffects> water;
  std::shared_ptr<const RenderTerrainImprint> terrain_imprint;
  std::shared_ptr<const RenderSkyEffects> sky; // procedural sky (normal = view direction)
  std::shared_ptr<const RenderCoverData> cover; // weather cover for rain/snow draws
  unsigned instance_count=0; // instanced draws: how many of `instances` (0 = all)
  bool ray_shadow=false; // sun shadow from this frame's ray traced mask (bgfx compute)
  std::shared_ptr<const RenderTerrainRelief> terrain_relief;
  // Tâm local XYZ và bán kính sphere bảo thủ cho model/instance; W=0 tắt Hi-Z.
  std::array<float,4> instance_occlusion_bounds{};
  std::shared_ptr<const RenderGpuCull> gpu_cull; ///< Snapshot cấu hình culling GPU tùy chọn.
  std::array<float,8> virtual_texture{}; ///< Renderer-owned virtual albedo parameters; zero disables indirection.
};
// Textured triangle-list path with optional directional lighting. Transform is column-major,
// with OpenGL clip coordinates (depth -1..1); the device adapter converts Z.
void renderer_draw_mesh(context &, const MeshDraw &);
void renderer_draw_sprite(context &, const RenderImage &, float x, float y, float width, float height);
void renderer_draw_sprite(context &, RenderTextureHandle, float x, float y, float width, float height);
}
