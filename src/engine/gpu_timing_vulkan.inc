// Private SDL Vulkan bridge. Included after SDL's native types/entry points.
// Main renderer thread only; inactive unless an opt-in timing log starts a query.
static void VULKAN_ReleaseFence(SDL_GPURenderer *, SDL_GPUFence *);
typedef struct SawGodTimingSlot {
    VulkanCommandBuffer *command;
    VulkanFenceHandle *fence;
    Uint64 frame;
    int state; // 0 free, 1 recording, 2 submitted
} SawGodTimingSlot;
typedef struct SawGodTiming {
    VulkanRenderer *renderer;
    VkQueryPool pool;
    PFN_vkCmdResetQueryPool reset;
    PFN_vkCmdWriteTimestamp write;
    PFN_vkGetQueryPoolResults results;
    PFN_vkDestroyQueryPool destroy;
    Uint32 bits;
    SawGodTimingSlot slots[16];
} SawGodTiming;
// Store per-device context in SDL properties; device lifetimes remain independent.
static SawGodTiming *SawGodTimingGet(VulkanRenderer *r) {
    return (SawGodTiming *)SDL_GetPointerProperty(r->props, "sawgod.gpu.timestamps", NULL);
}
bool SawGodVulkanTimingStart(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command, Uint64 frame) {
    if (SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan") != 0) return false;
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    SawGodTiming *t = SawGodTimingGet(r);
    if (!t) {
        Uint32 n = 0;
        r->vkGetPhysicalDeviceQueueFamilyProperties(r->physicalDevice, &n, NULL);
        VkQueueFamilyProperties *queues = SDL_malloc(n * sizeof(*queues));
        if (!queues) return false;
        r->vkGetPhysicalDeviceQueueFamilyProperties(r->physicalDevice, &n, queues);
        Uint32 bits = queues[r->queueFamilyIndex].timestampValidBits;
        SDL_free(queues);
        if (!bits || !r->physicalDeviceProperties.properties.limits.timestampComputeAndGraphics) return false;
        t = SDL_calloc(1, sizeof(*t));
        if (!t) return false;
        t->renderer = r;
        t->bits = bits;
        t->reset = (PFN_vkCmdResetQueryPool)r->vkGetDeviceProcAddr(r->logicalDevice, "vkCmdResetQueryPool");
        t->write = (PFN_vkCmdWriteTimestamp)r->vkGetDeviceProcAddr(r->logicalDevice, "vkCmdWriteTimestamp");
        t->results = (PFN_vkGetQueryPoolResults)r->vkGetDeviceProcAddr(r->logicalDevice, "vkGetQueryPoolResults");
        t->destroy = (PFN_vkDestroyQueryPool)r->vkGetDeviceProcAddr(r->logicalDevice, "vkDestroyQueryPool");
        PFN_vkCreateQueryPool create = (PFN_vkCreateQueryPool)r->vkGetDeviceProcAddr(r->logicalDevice, "vkCreateQueryPool");
        VkQueryPoolCreateInfo info = {0};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 32;
        if (!create || !t->reset || !t->write || !t->results || !t->destroy ||
            create(r->logicalDevice, &info, NULL, &t->pool) != VK_SUCCESS) {
            SDL_free(t); return false;
        }
        SDL_SetPointerProperty(r->props, "sawgod.gpu.timestamps", t);
    }
    for (Uint32 i = 0; i < 16; ++i)
        if (t->slots[i].state == 1 && t->slots[i].command == (VulkanCommandBuffer *)command) return true;
    for (Uint32 i = 0; i < 16; ++i) if (!t->slots[i].state) {
        VulkanCommandBuffer *c = (VulkanCommandBuffer *)command;
        t->slots[i].command = c;
        t->slots[i].frame = frame;
        t->slots[i].state = 1;
        t->reset(c->commandBuffer, t->pool, i*2, 2);
        t->write(c->commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, t->pool, i*2);
        return true;
    }
    return false; // bounded ring; never stall when GPU falls behind
}
static void SawGodTimingEnd(VulkanRenderer *r, VulkanCommandBuffer *c) {
    SawGodTiming *t = SawGodTimingGet(r);
    if (!t) return;
    for (Uint32 i = 0; i < 16; ++i) if (t->slots[i].state == 1 && t->slots[i].command == c)
        t->write(c->commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, t->pool, i*2+1);
}
static void SawGodTimingSubmitted(VulkanRenderer *r, VulkanCommandBuffer *c) {
    SawGodTiming *t = SawGodTimingGet(r);
    if (!t) return;
    for (Uint32 i = 0; i < 16; ++i) if (t->slots[i].state == 1 && t->slots[i].command == c) {
        t->slots[i].fence = c->inFlightFence;
        SDL_AtomicIncRef(&c->inFlightFence->referenceCount);
        t->slots[i].state = 2;
    }
}
bool SawGodVulkanTimingPoll(SDL_GPUDevice *device, Uint64 *frame, double *ms) {
    if (SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan") != 0) return false;
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    SawGodTiming *t = SawGodTimingGet(r);
    if (!t) return false;
    int oldest = -1;
    for (int i = 0; i < 16; ++i) if (t->slots[i].state == 2 &&
        (oldest < 0 || t->slots[i].frame < t->slots[oldest].frame)) oldest = i;
    if (oldest < 0) return false;
    SawGodTimingSlot *s = &t->slots[oldest];
    // Fence check also prevents observing stale availability before a recycled slot's reset executes.
    if (r->vkGetFenceStatus(r->logicalDevice, s->fence->fence) != VK_SUCCESS) return false;
    Uint64 values[4] = {0};
    VkResult result = t->results(r->logicalDevice, t->pool, oldest*2, 2, sizeof(values), values,
                                 sizeof(Uint64)*2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS || !values[1] || !values[3]) return false;
    Uint64 mask = t->bits == 64 ? ~(Uint64)0 : (((Uint64)1 << t->bits) - 1);
    *frame = s->frame;
    *ms = (double)((values[2] - values[0]) & mask) * r->physicalDeviceProperties.properties.limits.timestampPeriod / 1e6;
    VULKAN_ReleaseFence((SDL_GPURenderer *)r, (SDL_GPUFence *)s->fence);
    SDL_zero(*s);
    return true;
}
static void SawGodTimingDestroy(VulkanRenderer *r) {
    SawGodTiming *t = SawGodTimingGet(r);
    if (!t) return;
    for (int i = 0; i < 16; ++i) if (t->slots[i].fence)
        VULKAN_ReleaseFence((SDL_GPURenderer *)r, (SDL_GPUFence *)t->slots[i].fence);
    t->destroy(r->logicalDevice, t->pool, NULL);
    SDL_free(t);
    SDL_ClearProperty(r->props, "sawgod.gpu.timestamps");
}
