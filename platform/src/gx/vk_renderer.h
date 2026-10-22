#pragma once

#include "gx_camera.h"
#include "gpu.h"
#include "gx/vk/vk_hot_path.h"
#include "gx/vk/vk_types.h"
#include "gx_builder.h"
#include "gx_record.h"
#include "mobile/mobile_ui.h"
#include "vk_shadergen.h"
#include <android/native_window.h>
#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

namespace gpu {

enum class PrewarmResult {
    Reused,               // Existing real pipeline reused from cache
    Created,              // New real pipeline successfully created
    SpirvUnavailable,     // SPIR-V bytecode missing or unavailable
    CompilationPending,   // Shader compilation is currently pending
    PlaceholderReturned,  // Placeholder returned (never counted as success)
    CreationFailed,       // vkCreateGraphicsPipelines failed
    UnsupportedState      // Unsupported state or incompatible render pass
};

// Per-draw texture resolution counters reported by the draw diagnostic log.
struct DrawTexStats {
  uint32_t realTex = 0;
  uint32_t dummyTex = 0;
  uint32_t copyHits = 0;
  uint32_t copyFallbacks = 0;
  uint32_t copyMisses = 0;
};

class VkRenderer {
public:
  static VkRenderer &get();

  VkRenderer() = default;
  ~VkRenderer() { shutdown(); }

  bool init(ANativeWindow *window);
  void shutdown();

  bool isReady() const { return mInitialized; }
  bool hasFailed() const { return mFatal; }
  int width() const { return mWidth; }
  int height() const { return mHeight; }
  const char *deviceName() const { return mDeviceProperties.deviceName; }

  VkTarget createTarget(int w, int h);
  void destroyTarget(VkTarget &target);

  bool update();
  bool render(VkTarget &target, const EyeView *eye, VkTarget *hud,
              HudMode hudMode = HudMode::Inline);
  uint64_t frameNumber() const { return mFrameNumber; }
  bool hasFrame() const { return mCurrentFrame != nullptr; }
  bool waitingForPipelines() const { return mPipelinesLoading; }
  void nativeTargetSize(int& width, int& height) const;
  bool present(const VkTarget &target, int vpX, int vpY, int vpW, int vpH);

  void onWindowResized(ANativeWindow *window);
  void savePipelineCache();

  // Internal subsystem methods
  bool createInstance();
  bool selectPhysicalDevice();
  bool createLogicalDevice();
  bool createSurface(ANativeWindow *window);
  bool createSwapchain();
  bool createRenderPasses();
  bool createSwapchainFramebuffers();
  bool createCommandPool();
  bool createSyncObjects();
  bool createDescriptorLayouts();
  bool growSamplerPool();
  VkDescriptorSet allocateImageSet(VkDescriptorSetLayout layout, VkDescriptorPool& pool);
  void releaseRetiredImageSets();
  bool createEfbCopyPipeline();
  void destroyEfbCopyPipeline();
  bool copyEfbTexture(VkCommandBuffer cmd, VkTarget& target, VkCopyTex& dst,
                      int x, int y, int w, int h, uint32_t ctrl, uint32_t pixelFormat);
  bool createPresentationPipeline();
  void destroyPresentationPipeline();
  bool createUiPipeline();
  void destroyUiPipeline();
  void renderUiOverlay(int width, int height, VkCommandBuffer cmd);
  void initPipelineCache();

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);
  VkFormat findSupportedDepthFormat();
  void flushDeferredDeletes();
  void trimFrameCaches(uint64_t frame);
  bool waitInFlightSubmissions(const char *where);
  void markFatal(const char *where, const char *what, int result);
  void logDeviceFault(const char *where);
  void discardPipelineCacheAfterFault();

  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer &buffer,
                    VkDeviceMemory &bufferMemory);
  bool uploadDynamicBuffer(VkBuffer &buf, VkDeviceMemory &mem, void *&mappedPtr,
                           size_t &currentCap, VkBufferUsageFlags usage,
                           const void *data, size_t bytes);

  VkSampler getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels);
  void uploadTextures(VkCommandBuffer cmd,
                      const std::vector<std::shared_ptr<TexImage>> &textures);
  VkPipeline getPipeline(const Item &it, const ShaderUid &uid);
  PrewarmResult prewarmExactPipeline(const PipelineKey &key, const ShaderUid &uid);
  VkPipeline createPipeline(const PipelineKey &key,
                            const std::vector<uint32_t> &vsSpirv,
                            const std::vector<uint32_t> &fsSpirv);
  VkPipeline buildPipeline(const PipelineKey &key,
                           const std::vector<uint32_t> &vsSpirv,
                           const std::vector<uint32_t> &fsSpirv);
  bool prepareFramePipelines(const Prepared &frame, HudMode hudMode);

  void startShaderWorker();
  void stopShaderWorker();
  void requestPipeline(const PipelineKey &key, const ShaderUid &uid);
  void drainCompiledShaders();
  VkDescriptorSet
  getSamplerDescriptorSet(const std::array<VkImageView, 8> &views,
                          const std::array<VkSampler, 8> &samplers);
  VkDescriptorSet resolveDrawTextureSet(const Frame &f, const Item &it,
                                        DrawTexStats &stats);

  VkShaderModule createShaderModule(const uint32_t *code, size_t sizeBytes);
  VkCopyTex &getCopyTex(uint64_t key, int w, int h);
  void evictCopies(uint64_t currentFrame);
  void invalidateSamplerDescSetsForView(VkImageView view);

  bool uploadUiFontAtlas();
  void executeCopyItem(VkCommandBuffer cmd, const Item &it, const Prepared &p,
                       VkTarget &target, float fx, float fy, bool &inRenderPass,
                       const VkRenderPassBeginInfo &rpInfo, FrameResource &fr);
  void executeDrawPass(VkCommandBuffer cmd, const Frame &f, const Prepared &p,
                       VkTarget &target, HudMode hudMode, FrameResource &fr);

  bool mInitialized = false;
  ANativeWindow *mWindow = nullptr;
  int mWidth = 0;
  int mHeight = 0;

  VkInstance mInstance = VK_NULL_HANDLE;
  VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties mDeviceProperties{};
  VkPhysicalDeviceFeatures mDeviceFeatures{};
  VkDevice mDevice = VK_NULL_HANDLE;
  VkQueue mQueue = VK_NULL_HANDLE;
  uint32_t mQueueFamily = 0;

  VkSurfaceKHR mSurface = VK_NULL_HANDLE;
  VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
  VkFormat mSwapchainFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageUsageFlags mSwapchainUsage = 0;
  VkSurfaceTransformFlagBitsKHR mSwapchainPreTransform =
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  VkExtent2D mSwapchainExtent{0, 0};
  VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
  std::vector<VkImage> mSwapchainImages;
  std::vector<VkImageView> mSwapchainImageViews;
  std::vector<VkFramebuffer> mSwapchainFramebuffers;

  VkRenderPass mEfbRenderPass = VK_NULL_HANDLE;
  VkRenderPass mEfbLoadRenderPass = VK_NULL_HANDLE;
  VkRenderPass mPresentRenderPass = VK_NULL_HANDLE;
  VkCommandPool mCommandPool = VK_NULL_HANDLE;
  VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
  VkCommandBuffer mPresentCommandBuffer = VK_NULL_HANDLE;

  VkSemaphore mImageAvailableSem = VK_NULL_HANDLE;
  VkSemaphore mRenderToPresentSem = VK_NULL_HANDLE;
  bool mRenderSignaledSem = false;
  std::vector<VkSemaphore> mRenderFinishedSems;
  VkFence mRenderFence = VK_NULL_HANDLE;
  VkFence mPresentFence = VK_NULL_HANDLE;
  bool mRenderFenceInFlight = false;
  bool mPresentFenceInFlight = false;
  bool mFatal = false;

  struct StagingBufferItem {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };
  std::vector<StagingBufferItem> mStagingBuffersToFree;

  // Descriptors & Pipelines
  VkDescriptorSetLayout mStorageDescLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout mTextureDescLayout = VK_NULL_HANDLE;
  VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
  std::vector<VkDescriptorPool> mSamplerPools;
  std::vector<SamplerSetEntry> mRetiredImageSets;
  VkDescriptorPool mStaticDescPool = VK_NULL_HANDLE;
  VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
  VkPipelineCache mPipelineCache = VK_NULL_HANDLE;
  VkDescriptorSetLayout mCopyDescLayout = VK_NULL_HANDLE;
  VkPipelineLayout mCopyPipelineLayout = VK_NULL_HANDLE;
  VkPipeline mCopyPipeline = VK_NULL_HANDLE;
  VkSampler mCopySampler = VK_NULL_HANDLE;

  // Presentation pipeline
  VkDescriptorSetLayout mPresentDescLayout = VK_NULL_HANDLE;
  VkPipelineLayout mPresentPipelineLayout = VK_NULL_HANDLE;
  VkPipeline mPresentPipeline = VK_NULL_HANDLE;
  VkSampler mPresentSampler = VK_NULL_HANDLE;
  std::unordered_map<VkImageView, VkDescriptorSet> mPresentDescSets;

  // UI overlay pipeline
  VkDescriptorSetLayout mUiDescLayout = VK_NULL_HANDLE;
  VkPipelineLayout mUiPipelineLayout = VK_NULL_HANDLE;
  VkPipeline mUiPipeline = VK_NULL_HANDLE;
  VkSampler mUiFontSampler = VK_NULL_HANDLE;
  VkTexture mUiFontTexture;
  VkDescriptorSet mUiDescSet = VK_NULL_HANDLE;

  // Rotating frame resources
  FrameResource mFrameRes[2];
  int mFrameIndex = 0;

  // Shader worker state
  struct CompiledShader {
    ShaderUid uid;
    PipelineKey key;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool ok = false;
    bool knownUid = false;
    double ms = 0.0;
  };
  std::thread mShaderThread;
  std::mutex mShaderMutex;
  std::condition_variable mShaderCv;
  struct PipelineRequest { PipelineKey key; ShaderUid uid; bool knownUid; };
  std::deque<PipelineRequest> mShaderQueue;
  std::vector<CompiledShader> mShaderDone;
  std::unordered_set<ShaderUid, ShaderUidHash> mShaderFailed;
  bool mShaderStop = false;
  std::unordered_set<PipelineKey, PipelineKeyHash> mPipelinePending;
  std::unordered_set<PipelineKey, PipelineKeyHash> mPipelineFailed;
  std::mutex mPipelineCompileMutex;
  bool mPipelinesLoading = false;
  uint64_t mShaderCompiles = 0;

  // Caches
  std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> mPipelines;
  std::vector<VkPipeline> mDrawPipelines;
  PipelineMru mPipelineMru;
  uint64_t mPipelineMruHits = 0;
  uint64_t mPipelineMruMisses = 0;
  // Pipeline keys created at runtime (not prewarmed) and how many of those
  // already had their ShaderUid in the inventory catalogue.
  uint64_t mRuntimePipelineCreates = 0;
  uint64_t mRuntimeCreatesKnownUid = 0;
  std::unordered_map<uint64_t, VkSampler> mSamplers;
  std::unordered_map<SamplerDescKey, SamplerSetEntry, SamplerDescKeyHash>
      mSamplerDescSets;
  // Bumped whenever cached sampler descriptor sets are freed, so a cached
  // set handle from an earlier draw can never be bound again by accident.
  uint64_t mSamplerDescEpoch = 0;
  std::unordered_map<uint64_t, VkCopyTex> mCopies;

  VkTexture mDummyTexture;
  VkSampler mDummySampler = VK_NULL_HANDLE;
  uint32_t mCopySlot = 0;

  // Frame command batching
  Builder mBuilder;
  std::unique_ptr<Prepared> mPrepared;
  std::shared_ptr<const Frame> mCurrentFrame;
  uint64_t mFrameNumber = 0;

  std::recursive_mutex mRenderMutex;
};

VkRenderer &vkRenderer();
void setVkPipelineCachePath(const char *path);

} // namespace gpu
