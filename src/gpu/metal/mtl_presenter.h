#pragma once

// Presents finished frames to a CAMetalLayer. Up to kFramesInFlight frames
// are queued on the GPU so the CPU never waits on the GPU unless it is a
// full three frames ahead.

#include <memory>
#include <cstdint>

namespace Gpu::Metal {

class Device;

struct PresenterConfig {
    bool vsync = true;
};

class Presenter {
public:
    static constexpr int kFramesInFlight = 3;

    // `layer` is a CAMetalLayer* (e.g. from SDL_Metal_GetLayer).
    Presenter(Device& device, void* layer, const PresenterConfig& config);
    ~Presenter();

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    void Resize(int width_px, int height_px);
    void SetVsync(bool enabled);

    // Placeholder frame: clears the drawable.
    void PresentClear(float r, float g, float b);

    // Draws a renderer frame after its shared-event value is reached.
    void PresentTexture(void* texture_handle, std::uint64_t ready_value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Gpu::Metal
