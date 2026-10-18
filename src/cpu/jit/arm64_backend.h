#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace CPU::JIT::ARM64 {

// Thunk entry point: called when the guest calls a host (HLE) function.
using HostThunk = uint64_t (*)(uint64_t guest_rip, uint64_t* guest_regs);

class ARM64Backend {
public:
    ARM64Backend();
    ~ARM64Backend();

    // Translates a block of guest x86-64 code at `guest_addr` and returns
    // the host address of the generated ARM64 code.
    void* Translate(uint64_t guest_addr);

    // Registers a host thunk for a specific guest call target.
    void RegisterThunk(uint64_t guest_target, HostThunk thunk);

    // Invalidate a translated block (e.g., after a self-modifying code write).
    void Invalidate(uint64_t guest_addr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace CPU::JIT::ARM64