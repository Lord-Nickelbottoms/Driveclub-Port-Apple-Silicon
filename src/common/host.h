#pragma once

// Facts about the machine the (x86_64) host process is running on.

#include <cstddef>

#include <sys/sysctl.h>

namespace Host {

// True when this x86_64 process is being translated by Rosetta 2 on Apple Silicon.
inline bool RunningUnderRosetta() {
    static const bool translated = [] {
        int value = 0;
        size_t size = sizeof(value);
        return sysctlbyname("sysctl.proc_translated", &value, &size, nullptr, 0) == 0 && value == 1;
    }();
    return translated;
}

} // namespace Host
