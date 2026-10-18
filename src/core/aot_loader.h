#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Core::AOT {

// Maps a guest RIP to a host function pointer produced by an offline recompiler.
class AOTLoader {
public:
    static AOTLoader& Instance();

    // Load a shared library (.dylib/.so) that exports recompiled guest functions.
    bool LoadLibrary(const std::string& path);

    // Returns the host function for a guest address, or nullptr.
    void* Lookup(uint64_t guest_rip);

private:
    std::unordered_map<uint64_t, void*> symbol_map_;
};

} // namespace Core::AOT