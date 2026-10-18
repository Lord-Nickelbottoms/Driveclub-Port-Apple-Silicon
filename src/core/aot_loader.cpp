#include "core/aot_loader.h"
#include <dlfcn.h>

namespace Core::AOT {

AOTLoader& AOTLoader::Instance() {
    static AOTLoader loader;
    return loader;
}

bool AOTLoader::LoadLibrary(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) return false;
    // Expect the recompiler to export a table: guest_addr -> host_func
    using ExportTable = const std::unordered_map<uint64_t, void*>* (*)();
    auto get_table = reinterpret_cast<ExportTable>(dlsym(handle, "get_recompiled_table"));
    if (get_table) {
        const auto* table = get_table();
        if (table) {
            for (const auto& [guest, host] : *table) {
                symbol_map_[guest] = host;
            }
        }
    }
    return true;
}

void* AOTLoader::Lookup(uint64_t guest_rip) {
    auto it = symbol_map_.find(guest_rip);
    return it != symbol_map_.end() ? it->second : nullptr;
}

} // namespace Core::AOT