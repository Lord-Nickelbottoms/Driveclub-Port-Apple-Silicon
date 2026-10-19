#include "gpu/metal/mtl_device.h"

#include <cstdlib>
#include <stdexcept>

#import <Metal/Metal.h>

#include "common/log.h"

namespace Gpu::Metal {

namespace {

constexpr NSUInteger kMaxCommandBuffersInFlight = 8;
constexpr NSUInteger kMaxPresenterCommandBuffersInFlight = 3;

} // namespace

struct Device::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLCommandQueue> present_queue;
    id<MTLSharedEvent> frame_event;
};

static id<MTLDevice> PickDevice() {
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    const char* wanted = std::getenv("DC_GPU");

    id<MTLDevice> best = nil;
    for (id<MTLDevice> dev in devices) {
        DC_INFO("gpu candidate: %s (low power: %d, metal3: %d)", dev.name.UTF8String,
                dev.isLowPower, [dev supportsFamily:MTLGPUFamilyMetal3]);
        if (![dev supportsFamily:MTLGPUFamilyMetal3]) {
            continue;
        }
        if (wanted) {
            if ([dev.name rangeOfString:@(wanted) options:NSCaseInsensitiveSearch].location !=
                NSNotFound) {
                return dev;
            }
            continue;
        }
        // Prefer the discrete GPU; among equals, the one with more memory.
        if (!best || (best.isLowPower && !dev.isLowPower) ||
            (best.isLowPower == dev.isLowPower &&
             dev.recommendedMaxWorkingSetSize > best.recommendedMaxWorkingSetSize)) {
            best = dev;
        }
    }
    return best;
}

Device::Device() : impl(std::make_unique<Impl>()) {
    impl->device = PickDevice();
    if (!impl->device) {
        throw std::runtime_error("no Metal 3 capable GPU found");
    }
    // Guest compute and graphics share a queue; presentation gets independent
    // command-buffer slots so guest backpressure cannot consume them.
    impl->queue = [impl->device
        newCommandQueueWithMaxCommandBufferCount:kMaxCommandBuffersInFlight];
    impl->present_queue = [impl->device
        newCommandQueueWithMaxCommandBufferCount:kMaxPresenterCommandBuffersInFlight];
    impl->frame_event = [impl->device newSharedEvent];
    if (!impl->queue || !impl->present_queue || !impl->frame_event) {
        throw std::runtime_error("cannot create Metal command queues or frame event");
    }

    id<MTLDevice> dev = impl->device;
    info.name = dev.name.UTF8String;
    info.unified_memory = dev.hasUnifiedMemory;
    info.argument_buffers_tier2 = dev.argumentBuffersSupport == MTLArgumentBuffersTier2;
    info.working_set_bytes = dev.recommendedMaxWorkingSetSize;

    DC_INFO("using gpu: %s, unified memory: %d, argument buffers tier2: %d, working set: %llu MB",
            info.name.c_str(), info.unified_memory, info.argument_buffers_tier2,
            info.working_set_bytes >> 20);
}

Device::~Device() = default;

void* Device::Handle() const {
    return (__bridge void*)impl->device;
}

void* Device::Queue() const {
    return (__bridge void*)impl->queue;
}

void* Device::PresentQueue() const {
    return (__bridge void*)impl->present_queue;
}

void* Device::FrameEvent() const {
    return (__bridge void*)impl->frame_event;
}

} // namespace Gpu::Metal
