#include "njin_raytrace_vk.h"
#if NJIN_BGFX
#include "njin_bgfx.h"
#include "njin_log.h"
#include <bgfx/bgfx.h>
#define VK_NO_PROTOTYPES
#include <vulkan-local/vulkan.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <sstream>

// Entry points added to the pinned bgfx by cmake/njin_bgfx_rayquery_fix.cmake.
namespace bgfx {
struct NjinVkInternals {
  void *instance;
  void *physicalDevice;
  void *device;
  void *queue;
  uint32_t queueFamily;
  void *getInstanceProcAddr;
  bool rayQuery;
};
void njinVkWantRayQuery(bool);
bool njinVkInternals(NjinVkInternals &);
bool njinVkTexture(TextureHandle,uint64_t &image,uint32_t &layout,uint32_t &format);
void njinVkSetDispatchHook(void *fn,void *user);
}

namespace njin::rhi {
namespace {

#define NJIN_VK_DEVICE_FUNCS(X) \
  X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
  X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateAccelerationStructureKHR) \
  X(vkDestroyAccelerationStructureKHR) X(vkGetAccelerationStructureBuildSizesKHR) X(vkCmdBuildAccelerationStructuresKHR) \
  X(vkGetAccelerationStructureDeviceAddressKHR) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
  X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
  X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCmdBindPipeline) X(vkCmdPushDescriptorSetKHR) X(vkCmdDispatch) \
  X(vkCmdPipelineBarrier) X(vkCmdUpdateBuffer) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) \
  X(vkDestroySampler) X(vkDeviceWaitIdle)

bool depth_format(VkFormat f) {
  return f==VK_FORMAT_D16_UNORM||f==VK_FORMAT_X8_D24_UNORM_PACK32||f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D16_UNORM_S8_UINT||
         f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_D32_SFLOAT_S8_UINT;
}

class Device final : public VulkanRayQuery {
public:
  bool init(const bgfx::NjinVkInternals &in) {
    instance=VkInstance(in.instance);physical=VkPhysicalDevice(in.physicalDevice);device=VkDevice(in.device);
    const auto gipa=PFN_vkGetInstanceProcAddr(in.getInstanceProcAddr);
    const auto gdpa=PFN_vkGetDeviceProcAddr(gipa(instance,"vkGetDeviceProcAddr"));
    const auto memory_properties=PFN_vkGetPhysicalDeviceMemoryProperties(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
    const auto properties2=PFN_vkGetPhysicalDeviceProperties2(gipa(instance,"vkGetPhysicalDeviceProperties2"));
    if(!gdpa||!memory_properties||!properties2) return false;
#define NJIN_VK_LOAD(name) name=PFN_##name(gdpa(device,#name)); if(!name) {NJIN_WARN("ray tracing: %s missing, compute traversal used",#name);return false;}
    NJIN_VK_DEVICE_FUNCS(NJIN_VK_LOAD)
#undef NJIN_VK_LOAD
    get_address=PFN_vkGetBufferDeviceAddress(gdpa(device,"vkGetBufferDeviceAddress"));
    if(!get_address) get_address=PFN_vkGetBufferDeviceAddress(gdpa(device,"vkGetBufferDeviceAddressKHR"));
    if(!get_address) return false;
    memory_properties(physical,&memory);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&as_props};
    properties2(physical,&props);
    scratch_alignment=std::max<VkDeviceSize>(as_props.minAccelerationStructureScratchOffsetAlignment,1);
    device_name=props.properties.deviceName;
    return create_pipelines();
  }
  ~Device() override {
    bgfx::njinVkSetDispatchHook(nullptr,nullptr);
    if(!device) return;
    vkDeviceWaitIdle(device);
    release(current);
    for(auto &g:graveyard) g.second();
    for(auto &b:uniform_buffers) destroy(b);
    destroy(material_buffer);
    for(auto p:pipelines) if(p) vkDestroyPipeline(device,p,nullptr);
    if(pipeline_layout) vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
    if(set_layout) vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
    if(sampler) vkDestroySampler(device,sampler,nullptr);
  }
  void scene(const std::shared_ptr<const std::vector<std::array<float,4>>> &triangles) override {
    if(triangles==scene_rows) return;
    scene_rows=triangles; // kept alive, so an unchanged scene keeps its address
    retire(current);
    current={};
    if(!triangles||triangles->empty()) return;
    const auto &rows=*triangles;
    const size_t count=rows.size()/3;
    std::vector<float> vertices(count*9);
    for(size_t i=0;i<count;++i) {
      const auto &a=rows[i*3],&e1=rows[i*3+1],&e2=rows[i*3+2];
      const float v[9]{a[0],a[1],a[2],a[0]+e1[0],a[1]+e1[1],a[2]+e1[2],a[0]+e2[0],a[1]+e2[1],a[2]+e2[2]};
      std::memcpy(&vertices[i*9],v,sizeof(v));
    }
    const VkBufferUsageFlags input=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    current.vertices=make_buffer(vertices.size()*sizeof(float),input,true,vertices.data());
    current.triangles=make_buffer(rows.size()*sizeof(rows[0]),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true,rows.data());
    // Bottom level: every triangle, opaque; top level: that one instance.
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto &tri=geometry.geometry.triangles;
    tri.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress=address(current.vertices);
    tri.vertexStride=sizeof(float)*3;
    tri.maxVertex=uint32_t(count*3-1);
    tri.indexType=VK_INDEX_TYPE_NONE_KHR;
    current.blas_geometry=geometry;
    current.primitives=uint32_t(count);
    const auto blas_sizes=build_sizes(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,current.blas_geometry,current.primitives);
    current.blas=make_structure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,blas_sizes.accelerationStructureSize,current.blas_buffer);
    VkAccelerationStructureDeviceAddressInfoKHR blas_address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,nullptr,current.blas};
    VkAccelerationStructureInstanceKHR instance_data{};
    instance_data.transform.matrix[0][0]=instance_data.transform.matrix[1][1]=instance_data.transform.matrix[2][2]=1.f;
    instance_data.mask=0xff;
    instance_data.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance_data.accelerationStructureReference=vkGetAccelerationStructureDeviceAddressKHR(device,&blas_address);
    current.instances=make_buffer(sizeof(instance_data),input,true,&instance_data);
    VkAccelerationStructureGeometryKHR top{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    top.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    top.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    top.geometry.instances.data.deviceAddress=address(current.instances);
    current.tlas_geometry=top;
    const auto tlas_sizes=build_sizes(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,current.tlas_geometry,1);
    current.tlas=make_structure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,tlas_sizes.accelerationStructureSize,current.tlas_buffer);
    const auto align=[&](VkDeviceSize v) {return (v+scratch_alignment-1)/scratch_alignment*scratch_alignment;};
    current.tlas_scratch_offset=align(blas_sizes.buildScratchSize);
    current.scratch=make_buffer(current.tlas_scratch_offset+tlas_sizes.buildScratchSize+scratch_alignment,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,false,nullptr);
    current.valid=true;current.built=false;
  }
  void materials(const std::vector<std::array<float,4>> &rows) override {
    material_rows=rows;
    if(material_rows.empty()) material_rows.push_back({});
    const VkDeviceSize size=material_rows.size()*sizeof(material_rows[0]);
    if(size>material_buffer.size) {
      retire(material_buffer);
      material_buffer=make_buffer(std::max<VkDeviceSize>(size,4096),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false,nullptr);
    }
    materials_dirty=true;
  }
  void queue(const Dispatch &d) override {pending.push_back(d);}
  void begin_frame() override {
    // Whatever the last bgfx frame did not run was dropped by bgfx; do not run it now.
    pending.clear();
    ++frame;
    while(!graveyard.empty()&&frame-graveyard.front().first>8) {graveyard.front().second();graveyard.pop_front();}
  }
  std::uint64_t dispatches() const override {return dispatched;}

  static bool hook(void *user,VkCommandBuffer cmd,uint16_t program,uint32_t x,uint32_t y,uint32_t z) {
    return static_cast<Device*>(user)->record(cmd,program,x,y,z);
  }

private:
  struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkDeviceSize size=0;};
  struct Scene {
    bool valid=false,built=false;
    Buffer vertices,triangles,instances,scratch,blas_buffer,tlas_buffer;
    VkAccelerationStructureKHR blas=VK_NULL_HANDLE,tlas=VK_NULL_HANDLE;
    VkAccelerationStructureGeometryKHR blas_geometry{},tlas_geometry{};
    uint32_t primitives=0;
    VkDeviceSize tlas_scratch_offset=0;
  };

  bool create_pipelines() {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(device,&si,nullptr,&sampler)!=VK_SUCCESS) return false;
    const VkShaderStageFlags cs=VK_SHADER_STAGE_COMPUTE_BIT;
    const VkDescriptorSetLayoutBinding bindings[]{
      {0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,cs,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,cs,nullptr},
      {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,cs,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,cs,nullptr},
      {8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,cs,nullptr},{9,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,cs,nullptr},
      {17,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,cs,nullptr},{21,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,cs,nullptr},
      {22,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,cs,nullptr}};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    li.bindingCount=uint32_t(std::size(bindings));li.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device,&li,nullptr,&set_layout)!=VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount=1;pli.pSetLayouts=&set_layout;
    if(vkCreatePipelineLayout(device,&pli,nullptr,&pipeline_layout)!=VK_SUCCESS) return false;
    for(unsigned stage:{0u,2u,3u}) {
      const auto code=bgfx_raytrace_hw_bytes(stage);
      if(code.empty()||code.size()%4) return false;
      VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      std::vector<uint32_t> words(code.size()/4);
      std::memcpy(words.data(),code.data(),code.size());
      mi.codeSize=code.size();mi.pCode=words.data();
      VkShaderModule module=VK_NULL_HANDLE;
      if(vkCreateShaderModule(device,&mi,nullptr,&module)!=VK_SUCCESS) return false;
      VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
      ci.layout=pipeline_layout;
      const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&pipelines[stage]);
      vkDestroyShaderModule(device,module,nullptr);
      if(result!=VK_SUCCESS) return false;
    }
    for(auto &b:uniform_buffers)
      b=make_buffer(sizeof(Uniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false,nullptr);
    return true;
  }
  uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags wanted) const {
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
      if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&wanted)==wanted) return i;
    throw std::runtime_error("njin ray tracing: no suitable Vulkan memory type");
  }
  Buffer make_buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool host,const void *data) {
    Buffer b;b.size=size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=size;bi.usage=usage;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateBuffer(device,&bi,nullptr,&b.buffer)!=VK_SUCCESS) throw std::runtime_error("njin ray tracing: buffer creation failed");
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext=(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)?&flags:nullptr;
    ai.allocationSize=req.size;
    ai.memoryTypeIndex=memory_type(req.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(vkAllocateMemory(device,&ai,nullptr,&b.memory)!=VK_SUCCESS) {vkDestroyBuffer(device,b.buffer,nullptr);throw std::runtime_error("njin ray tracing: out of GPU memory");}
    vkBindBufferMemory(device,b.buffer,b.memory,0);
    if(data) {
      void *mapped=nullptr;
      vkMapMemory(device,b.memory,0,size,0,&mapped);
      std::memcpy(mapped,data,size_t(size));
      vkUnmapMemory(device,b.memory);
    }
    return b;
  }
  VkDeviceAddress address(const Buffer &b) const {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,nullptr,b.buffer};
    return get_address(device,&info);
  }
  VkAccelerationStructureBuildSizesInfoKHR build_sizes(VkAccelerationStructureTypeKHR type,const VkAccelerationStructureGeometryKHR &geometry,uint32_t primitives) const {
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type=type;info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;info.geometryCount=1;info.pGeometries=&geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,&primitives,&sizes);
    return sizes;
  }
  VkAccelerationStructureKHR make_structure(VkAccelerationStructureTypeKHR type,VkDeviceSize size,Buffer &storage) {
    storage=make_buffer(size,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,false,nullptr);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer=storage.buffer;ci.size=size;ci.type=type;
    VkAccelerationStructureKHR as=VK_NULL_HANDLE;
    if(vkCreateAccelerationStructureKHR(device,&ci,nullptr,&as)!=VK_SUCCESS) throw std::runtime_error("njin ray tracing: acceleration structure creation failed");
    return as;
  }
  void destroy(Buffer &b) {
    if(b.buffer) vkDestroyBuffer(device,b.buffer,nullptr);
    if(b.memory) vkFreeMemory(device,b.memory,nullptr);
    b={};
  }
  void release(Scene &s) {
    if(s.blas) vkDestroyAccelerationStructureKHR(device,s.blas,nullptr);
    if(s.tlas) vkDestroyAccelerationStructureKHR(device,s.tlas,nullptr);
    for(Buffer *b:{&s.vertices,&s.triangles,&s.instances,&s.scratch,&s.blas_buffer,&s.tlas_buffer}) destroy(*b);
    s={};
  }
  // Frames in flight may still read it: destroyed a few frames later.
  void retire(Scene &s) {if(s.valid) graveyard.emplace_back(frame,[this,old=s]() mutable {release(old);});}
  void retire(Buffer &b) {if(b.buffer) graveyard.emplace_back(frame,[this,old=b]() mutable {destroy(old);});b={};}
  void retire(VkImageView v) {graveyard.emplace_back(frame,[this,v] {vkDestroyImageView(device,v,nullptr);});}

  void barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkAccessFlags src_access,VkPipelineStageFlags dst,VkAccessFlags dst_access) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,src_access,dst_access};
    vkCmdPipelineBarrier(cmd,src,dst,0,1,&mb,0,nullptr,0,nullptr);
  }
  void build(VkCommandBuffer cmd) {
    const auto scratch=address(current.scratch);
    const auto aligned=(scratch+scratch_alignment-1)/scratch_alignment*scratch_alignment;
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;info.dstAccelerationStructure=current.blas;
    info.geometryCount=1;info.pGeometries=&current.blas_geometry;info.scratchData.deviceAddress=aligned;
    VkAccelerationStructureBuildRangeInfoKHR range{current.primitives,0,0,0};
    const VkAccelerationStructureBuildRangeInfoKHR *ranges=&range;
    vkCmdBuildAccelerationStructuresKHR(cmd,1,&info,&ranges);
    barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    info.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;info.dstAccelerationStructure=current.tlas;
    info.pGeometries=&current.tlas_geometry;info.scratchData.deviceAddress=aligned+current.tlas_scratch_offset;
    range.primitiveCount=1;
    vkCmdBuildAccelerationStructuresKHR(cmd,1,&info,&ranges);
    barrier(cmd,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    current.built=true;
  }
  void upload(VkCommandBuffer cmd,const Buffer &b,const void *data,VkDeviceSize size) {
    const auto *bytes=static_cast<const unsigned char*>(data);
    for(VkDeviceSize at=0;at<size;at+=65536) vkCmdUpdateBuffer(cmd,b.buffer,at,std::min<VkDeviceSize>(65536,size-at),bytes+at);
  }
  // A view of a bgfx texture in the layout bgfx left it in (it just bound it for this dispatch).
  bool view(uint16_t texture,VkDescriptorImageInfo &out,bool sampled) {
    uint64_t image=0;uint32_t layout=0,format=0;
    if(!bgfx::njinVkTexture(bgfx::TextureHandle{texture},image,layout,format)) return false;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    std::memcpy(&vi.image,&image,sizeof(VkImage));
    vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=VkFormat(format);
    vi.subresourceRange={VkImageAspectFlags(depth_format(vi.format)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
    VkImageView v=VK_NULL_HANDLE;
    if(vkCreateImageView(device,&vi,nullptr,&v)!=VK_SUCCESS) return false;
    retire(v);
    out={sampled?sampler:VK_NULL_HANDLE,v,VkImageLayout(layout)};
    return true;
  }
  bool record(VkCommandBuffer cmd,uint16_t program,uint32_t x,uint32_t y,uint32_t z) {
    if(pending.empty()||pending.front().program!=program) return false;
    const Dispatch d=pending.front();pending.pop_front();
    if(!current.valid||!pipelines[d.stage]) return false;
    const bool shading=d.stage!=0;
    if(shading&&!material_buffer.buffer) return false;
    VkDescriptorImageInfo images[2]{},depth{},grid{},grid_depth{};
    const unsigned image_count=d.stage==3?2:1;
    for(unsigned i=0;i<image_count;++i) if(!view(d.images[i],images[i],false)) return false;
    if(d.stage!=3&&!view(d.depth,depth,true)) return false;
    if(shading&&(!view(d.grid,grid,true)||!view(d.grid_depth,grid_depth,true))) return false;
    if(!current.built) build(cmd);
    auto &ubo=uniform_buffers[d.stage==0?0:d.stage==2?1:2];
    barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    upload(cmd,ubo,&d.uniforms,sizeof(d.uniforms));
    if(shading&&materials_dirty) {
      upload(cmd,material_buffer,material_rows.data(),material_rows.size()*sizeof(material_rows[0]));
      materials_dirty=false;
    }
    barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
    VkDescriptorBufferInfo tris{current.triangles.buffer,0,VK_WHOLE_SIZE},mats{material_buffer.buffer,0,VK_WHOLE_SIZE},
      uniforms{ubo.buffer,0,sizeof(Uniforms)};
    VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,nullptr,1,&current.tlas};
    std::vector<VkWriteDescriptorSet> writes;
    const auto add=[&](uint32_t binding,VkDescriptorType type,const VkDescriptorImageInfo *image,const VkDescriptorBufferInfo *buffer,const void *next=nullptr) {
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,next};
      w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=type;w.pImageInfo=image;w.pBufferInfo=buffer;
      writes.push_back(w);
    };
    for(unsigned i=0;i<image_count;++i) add(i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&images[i],nullptr);
    add(3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&tris);
    if(shading) add(4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&mats);
    add(8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,nullptr,&uniforms);
    add(9,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,nullptr,nullptr,&as);
    if(d.stage!=3) add(17,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&depth,nullptr);
    if(shading) {add(21,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&grid,nullptr);add(22,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&grid_depth,nullptr);}
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[d.stage]);
    vkCmdPushDescriptorSetKHR(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,uint32_t(writes.size()),writes.data());
    vkCmdDispatch(cmd,x,y,z);
    ++dispatched;
    return true;
  }

#define NJIN_VK_MEMBER(name) PFN_##name name=nullptr;
  NJIN_VK_DEVICE_FUNCS(NJIN_VK_MEMBER)
#undef NJIN_VK_MEMBER
  PFN_vkGetBufferDeviceAddress get_address=nullptr;
  VkInstance instance=VK_NULL_HANDLE;
  VkPhysicalDevice physical=VK_NULL_HANDLE;
  VkDevice device=VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memory{};
  VkDeviceSize scratch_alignment=256;
  VkSampler sampler=VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
  std::array<VkPipeline,4> pipelines{};
  std::array<Buffer,3> uniform_buffers{};
  Buffer material_buffer;
  std::vector<std::array<float,4>> material_rows;
  bool materials_dirty=false;
  Scene current;
  std::shared_ptr<const std::vector<std::array<float,4>>> scene_rows;
  std::deque<Dispatch> pending;
  std::deque<std::pair<std::uint64_t,std::function<void()>>> graveyard;
  std::uint64_t frame=0,dispatched=0;
public:
  std::string device_name;
};

}

void VulkanRayQuery::request(bool want) {bgfx::njinVkWantRayQuery(want);}

std::string vulkan_occlusion_diagnostics() {
  bgfx::NjinVkInternals in{};
  if(!bgfx::njinVkInternals(in)) return "Vulkan physical-device diagnostics unavailable";
  const auto gipa=PFN_vkGetInstanceProcAddr(in.getInstanceProcAddr);
  const auto instance=VkInstance(in.instance);
  const auto physical=VkPhysicalDevice(in.physicalDevice);
  const auto properties=PFN_vkGetPhysicalDeviceProperties(gipa(instance,"vkGetPhysicalDeviceProperties"));
  const auto features=PFN_vkGetPhysicalDeviceFeatures(gipa(instance,"vkGetPhysicalDeviceFeatures"));
  const auto format=PFN_vkGetPhysicalDeviceFormatProperties(gipa(instance,"vkGetPhysicalDeviceFormatProperties"));
  if(!properties||!features||!format) return "Vulkan physical-device diagnostic entry points unavailable";
  VkPhysicalDeviceProperties p{};VkPhysicalDeviceFeatures f{};VkFormatProperties r{};
  properties(physical,&p);features(physical,&f);format(physical,VK_FORMAT_R32_SFLOAT,&r);
  std::ostringstream out;
  out<<"Vulkan GPU="<<p.deviceName<<" API="<<VK_VERSION_MAJOR(p.apiVersion)<<'.'<<VK_VERSION_MINOR(p.apiVersion)<<'.'<<VK_VERSION_PATCH(p.apiVersion)
     <<" driver=0x"<<std::hex<<p.driverVersion<<" R32F_optimal=0x"<<r.optimalTilingFeatures<<std::dec
     <<" multiDrawIndirect="<<f.multiDrawIndirect<<" drawIndirectFirstInstance="<<f.drawIndirectFirstInstance
     <<" submission="<<(f.multiDrawIndirect?"native-multidraw":"split-single-indirect")
     <<" maxDrawIndirectCount="<<p.limits.maxDrawIndirectCount
     <<" computeInvocations="<<p.limits.maxComputeWorkGroupInvocations
     <<" computeSize="<<p.limits.maxComputeWorkGroupSize[0]<<','<<p.limits.maxComputeWorkGroupSize[1]<<','<<p.limits.maxComputeWorkGroupSize[2]
     <<" computeCount="<<p.limits.maxComputeWorkGroupCount[0]<<','<<p.limits.maxComputeWorkGroupCount[1]<<','<<p.limits.maxComputeWorkGroupCount[2]
     <<" sharedMemory="<<p.limits.maxComputeSharedMemorySize
     <<" storageBuffers="<<p.limits.maxPerStageDescriptorStorageBuffers
     <<" storageImages="<<p.limits.maxPerStageDescriptorStorageImages
     <<" storageBufferRange="<<p.limits.maxStorageBufferRange;
  return out.str();
}

std::unique_ptr<VulkanRayQuery> VulkanRayQuery::create() {
  bgfx::NjinVkInternals in{};
  if(!bgfx::njinVkInternals(in)) return nullptr;
  if(!in.rayQuery) {NJIN_INFO("ray tracing: this Vulkan device has no ray queries, compute traversal used");return nullptr;}
  if(bgfx_raytrace_hw_bytes(0).empty()) {NJIN_INFO("ray tracing: njin was built without glslc, compute traversal used");return nullptr;}
  auto device=std::make_unique<Device>();
  if(!device->init(in)) {NJIN_WARN("ray tracing: Vulkan ray query setup failed, compute traversal used");return nullptr;}
  bgfx::njinVkSetDispatchHook(reinterpret_cast<void*>(&Device::hook),device.get());
  NJIN_INFO("ray tracing: Vulkan ray queries on %s",device->device_name.c_str());
  return device;
}

}
#else
namespace njin::rhi {
std::string vulkan_occlusion_diagnostics() {return "Vulkan unavailable in this build";}
void VulkanRayQuery::request(bool) {}
std::unique_ptr<VulkanRayQuery> VulkanRayQuery::create() {return nullptr;}
}
#endif
