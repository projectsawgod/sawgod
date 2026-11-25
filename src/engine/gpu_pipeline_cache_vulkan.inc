// Project extension to SDL's private Vulkan backend; all calls run on the renderer thread.
// Pipeline blobs are driver/device-specific accelerators. Missing or stale files are harmless.
typedef struct SawGodPipelineCache {
    VkPipelineCache cache;
    PFN_vkGetPipelineCacheData get;
    PFN_vkDestroyPipelineCache destroy;
    char *path;
} SawGodPipelineCache;
static SawGodPipelineCache *SawGodPipelineCacheContext(VulkanRenderer *r) {
    return SDL_GetPointerProperty(r->props, "sawgod.gpu.pipeline-cache", NULL);
}
static VkPipelineCache SawGodPipelineCacheGet(VulkanRenderer *r) {
    SawGodPipelineCache *c = SawGodPipelineCacheContext(r);
    return c ? c->cache : VK_NULL_HANDLE;
}
// Validate the complete opaque blob before passing it to a driver, not just its Vulkan header.
// Publishing blob/receipt separately is safe: an interrupted/mismatched pair is a cache miss.
static void SawGodPipelineCacheRecord(char *record, size_t capacity, const void *bytes, size_t size) {
    SDL_snprintf(record, capacity, "SGPC1:%zu:%08x\n", size, (unsigned)SDL_crc32(0, bytes, size));
}
static bool SawGodPipelineCacheReceipt(const char *path, const void *bytes, size_t size) {
    char *receipt = NULL;
    SDL_asprintf(&receipt, "%s.crc", path);
    if (!receipt) return false;
    SDL_PathInfo file;
    size_t count = 0;
    void *saved = SDL_GetPathInfo(receipt, &file) && file.size <= 64 ? SDL_LoadFile(receipt, &count) : NULL;
    char record[64];
    SawGodPipelineCacheRecord(record, sizeof(record), bytes, size);
    bool valid = saved && count == SDL_strlen(record) && SDL_memcmp(saved, record, count) == 0;
    SDL_free(saved); SDL_free(receipt);
    return valid;
}
static void SawGodPipelineCacheWriteReceipt(const char *path, const void *bytes, size_t size) {
    char *receipt = NULL, *temporary = NULL;
    SDL_asprintf(&receipt, "%s.crc", path);
    SDL_asprintf(&temporary, "%s.crc.tmp", path);
    char record[64];
    SawGodPipelineCacheRecord(record, sizeof(record), bytes, size);
    if (receipt && temporary && SDL_SaveFile(temporary, record, SDL_strlen(record)))
        if (!SDL_RenamePath(temporary, receipt)) SDL_RemovePath(temporary);
    SDL_free(receipt); SDL_free(temporary);
}
SDL_DECLSPEC void SawGodVulkanPipelineCacheSetPath(SDL_GPUDevice *device, const char *directory) {
    if (!directory || !*directory || SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan") != 0)
        return;
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    if (SawGodPipelineCacheContext(r))
        return;
    PFN_vkCreatePipelineCache create = (PFN_vkCreatePipelineCache)r->vkGetDeviceProcAddr(r->logicalDevice, "vkCreatePipelineCache");
    SawGodPipelineCache *c = SDL_calloc(1, sizeof(*c));
    if (!c) return;
    c->get = (PFN_vkGetPipelineCacheData)r->vkGetDeviceProcAddr(r->logicalDevice, "vkGetPipelineCacheData");
    c->destroy = (PFN_vkDestroyPipelineCache)r->vkGetDeviceProcAddr(r->logicalDevice, "vkDestroyPipelineCache");
    const VkPhysicalDeviceProperties *properties = &r->physicalDeviceProperties.properties;
    SDL_asprintf(&c->path, "%sgpu-%x-%x-%x.cache", directory, properties->vendorID,
                 properties->deviceID, properties->driverVersion);
    if (!create || !c->get || !c->destroy || !c->path) {
        SDL_free(c->path); SDL_free(c); return;
    }
    size_t size = 0;
    // Bound the read before allocation, and validate Vulkan's cache header before giving it to a driver.
    SDL_PathInfo file;
    void *bytes = SDL_GetPathInfo(c->path, &file) && file.size >= 32 && file.size <= 64 * 1024 * 1024
                      ? SDL_LoadFile(c->path, &size) : NULL;
    Uint32 header[4] = {0};
    if (bytes && size >= 32) SDL_memcpy(header, bytes, sizeof(header));
    bool valid = bytes && size >= 32 && header[0] == 32 && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                 header[2] == properties->vendorID && header[3] == properties->deviceID &&
                 SDL_memcmp((Uint8 *)bytes + 16, properties->pipelineCacheUUID, VK_UUID_SIZE) == 0 &&
                 SawGodPipelineCacheReceipt(c->path, bytes, size);
    VkPipelineCacheCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = valid ? size : 0;
    info.pInitialData = valid ? bytes : NULL;
    VkResult result = create(r->logicalDevice, &info, NULL, &c->cache);
    if (result != VK_SUCCESS && valid) {
        info.initialDataSize = 0; info.pInitialData = NULL;
        result = create(r->logicalDevice, &info, NULL, &c->cache);
    }
    SDL_SetStringProperty(SDL_GetGPUDeviceProperties(device), "sawgod.pipeline-cache-status",
                          result == VK_SUCCESS && valid && info.initialDataSize ? "hit" : "miss");
    SDL_Log("startup: Vulkan pipeline cache %s (%zu bytes)",
            result == VK_SUCCESS && valid && info.initialDataSize ? "hit" : "miss", info.initialDataSize);
    SDL_free(bytes);
    if (result != VK_SUCCESS || !SDL_SetPointerProperty(r->props, "sawgod.gpu.pipeline-cache", c)) {
        if (result == VK_SUCCESS) c->destroy(r->logicalDevice, c->cache, NULL);
        SDL_free(c->path); SDL_free(c);
    }
}
static void SawGodPipelineCacheSave(VulkanRenderer *r) {
    SawGodPipelineCache *c = SawGodPipelineCacheContext(r);
    if (!c) return;
    size_t size = 0;
    if (c->get(r->logicalDevice, c->cache, &size, NULL) == VK_SUCCESS && size && size <= 64 * 1024 * 1024) {
        void *bytes = SDL_malloc(size);
        char *temporary = NULL;
        SDL_asprintf(&temporary, "%s.tmp", c->path);
        if (bytes && temporary && c->get(r->logicalDevice, c->cache, &size, bytes) == VK_SUCCESS) {
            size_t previous_size = 0;
            SDL_PathInfo file;
            void *previous = SDL_GetPathInfo(c->path, &file) && file.size <= 64 * 1024 * 1024
                                 ? SDL_LoadFile(c->path, &previous_size) : NULL;
            bool unchanged = previous && previous_size == size && SDL_memcmp(previous, bytes, size) == 0;
            SDL_free(previous);
            bool saved = unchanged;
            if (!unchanged && SDL_SaveFile(temporary, bytes, size)) {
                saved = SDL_RenamePath(temporary, c->path);
                if (!saved) SDL_RemovePath(temporary);
            }
            if (saved && !SawGodPipelineCacheReceipt(c->path, bytes, size))
                SawGodPipelineCacheWriteReceipt(c->path, bytes, size);
        }
        SDL_free(temporary); SDL_free(bytes);
    }
}
SDL_DECLSPEC void SawGodVulkanPipelineCacheSave(SDL_GPUDevice *device) {
    if (device && SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan") == 0)
        SawGodPipelineCacheSave((VulkanRenderer *)device->driverData);
}
static void SawGodPipelineCacheDestroy(VulkanRenderer *r) {
    SawGodPipelineCache *c = SawGodPipelineCacheContext(r);
    if (!c) return;
    SawGodPipelineCacheSave(r);
    c->destroy(r->logicalDevice, c->cache, NULL);
    SDL_ClearProperty(r->props, "sawgod.gpu.pipeline-cache");
    SDL_free(c->path); SDL_free(c);
}
