cmake_minimum_required(VERSION 3.24)

project(DriveclubNative LANGUAGES C CXX OBJC OBJCXX)

# Backends
#   macOS:          this tree, Metal renderer (src/gpu/metal).
#   Windows/Linux:  the DCport backend in windows/ (its own native runtime and
#                   launcher, Vulkan); see windows/README.md. This tree's core
#                   (src/core: loader, memory, signals, TLS) is macOS-only, so
#                   it does not configure there.
if (NOT APPLE)
    message(FATAL_ERROR
        "Driveclub Native: this tree (src/) builds the macOS backend only.\n"
        "For Windows/Linux use the DCport backend: see windows/README.md.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# PS4 code runs natively, so the host must be x86_64 (Intel Mac).
set(CMAKE_OSX_ARCHITECTURES x86_64)

execute_process(COMMAND sysctl -n hw.optional.arm64
                OUTPUT_VARIABLE DC_HW_ARM64 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (DC_HW_ARM64 STREQUAL "1")
    set(DC_APPLE_SILICON TRUE)
    message(STATUS "Apple Silicon host: building x86_64 for Rosetta 2")
    # Libraries must be x86_64 too, so use the Intel Homebrew in /usr/local (install it and
    # the dependencies through `arch -x86_64`, see README, "Apple Silicon").
    if (EXISTS /usr/local/Homebrew)
        list(PREPEND CMAKE_PREFIX_PATH /usr/local)
        # Keep find_package from picking up the arm64 libraries in /opt/homebrew.
        list(APPEND CMAKE_IGNORE_PREFIX_PATH /opt/homebrew)
    else()
        message(WARNING "No Intel Homebrew in /usr/local: SDL3 from /opt/homebrew is arm64 and "
                        "will not link into this x86_64 build (see README, Apple Silicon)")
    endif()
endif()

# Detect ARM64 host
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(DC_ARM64_HOST ON)
else()
    set(DC_ARM64_HOST OFF)
endif()

# Instruction-set baseline for our own code. -march=native is wrong here: on Apple
# Silicon it names an arm64 CPU, and Rosetta 2 only guarantees SSE4.2 (plus AVX/AVX2
# on macOS 15 and later), so the default there is x86-64-v2.
# if (DC_APPLE_SILICON)
#    set(DC_HOST_MARCH_DEFAULT x86-64-v2)
# else()
#    set(DC_HOST_MARCH_DEFAULT native)
# endif()
# set(DC_HOST_MARCH ${DC_HOST_MARCH_DEFAULT} CACHE STRING
    # "-march= value for Release builds (x86-64-v2, x86-64-v3, native, ...)")

find_package(SDL3 REQUIRED CONFIG)

# Platform core, HLE and the GPU front end (PM4, tiling, GCN decode).
set(DC_CORE_SOURCES
    src/main.mm
    src/common/log.h
    src/common/sha1.h
    src/common/types.h
    src/core/address_space.cpp
    src/core/address_space.h
    src/core/loader/elf_types.h
    src/core/loader/linker.cpp
    src/core/loader/linker.h
    src/core/loader/module.cpp
    src/core/loader/module.h
    src/core/loader/self.cpp
    src/core/loader/self.h
    src/core/memory.cpp
    src/core/memory.h
    src/core/rosetta_compat.cpp
    src/core/rosetta_compat.h
    src/core/runtime.cpp
    src/core/runtime.h
    src/core/signals.cpp
    src/core/signals.h
    src/core/tls.cpp
    src/core/tls.h
    src/gpu/renderer.h
    src/gpu/liverpool/pm4.h
    src/gpu/liverpool/command_processor.h
    src/gpu/liverpool/command_processor.cpp
    src/gpu/liverpool/registers.h
    src/gpu/liverpool/tiling.h
    src/gpu/liverpool/tiling.cpp
    src/gpu/shader/gcn.h
    src/gpu/shader/gcn_decode.cpp
    src/gpu/shader/program.h
    src/gpu/shader/program.cpp
    src/hle/hle.cpp
    src/hle/hle.h
    src/hle/kernel/kernel.cpp
    src/hle/kernel/kernel.h
    src/hle/kernel/equeue.h
    src/hle/kernel/file.cpp
    src/hle/kernel/memory.cpp
    src/hle/kernel/misc.cpp
    src/hle/kernel/pthread.cpp
    src/hle/kernel/sync.cpp
    src/hle/kernel/time.cpp
    src/hle/video_out/video_out.h
    src/hle/video_out/video_out.cpp
    src/hle/gnmdriver/gnmdriver.h
    src/hle/gnmdriver/gnmdriver.cpp
    src/hle/playgo/playgo.h
    src/hle/playgo/playgo.cpp
    src/hle/user_service/user_service.h
    src/hle/user_service/user_service.cpp
    src/hle/system_service/system_service.h
    src/hle/system_service/system_service.cpp
    src/hle/save_data/save_data.h
    src/hle/save_data/save_data.cpp
    src/hle/pad/pad.h
    src/hle/pad/pad.cpp
    src/hle/rtc/rtc.h
    src/hle/rtc/rtc.cpp
    src/hle/sysmodule/sysmodule.h
    src/hle/sysmodule/sysmodule.cpp
    src/hle/net/net.h
    src/hle/net/net.cpp
    src/hle/np/np.h
    src/hle/np/np.cpp
    src/hle/font/font.h
    src/hle/font/font.cpp
    src/hle/avplayer/avplayer.h
    src/hle/avplayer/avplayer.cpp
    src/hle/services/misc_services.h
    src/hle/audio_out/audio_out.h
    src/hle/audio_out/audio_out.cpp
    src/hle/services/misc_services.cpp
    src/hle/nid_db.inc
)

add_executable(driveclub ${DC_CORE_SOURCES})

if (DC_ARM64_HOST)
    target_sources(driveclub PRIVATE
        src/cpu/jit/arm64_backend.cpp
        src/cpu/jit/arm64_backend.h
        src/core/aot_loader.cpp
        src/core/aot_loader.h
    )
    target_compile_definitions(driveclub PRIVATE DC_ARM64_JIT=1)
endif()

target_include_directories(driveclub PRIVATE src)

target_compile_options(driveclub PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    $<$<CONFIG:Release>:-O3 -march=native>
)

# Guest memory is mapped at its PS4 addresses (executable at 0x400000), which
# sit inside the default 4 GB __PAGEZERO. Shrink it, move our own image above
# every guest range, and pin the zero-fill reservations from address_space.cpp
# to the guest ranges so the kernel maps them before any library can.
target_link_options(driveclub PRIVATE
    -Wl,-no_pie,-pagezero_size,0x4000,-image_base,0x700000000000
    -Wl,-segaddr,GUEST_TCB,0x4000
    -Wl,-segaddr,GUEST_SYSMGD,0x400000
    -Wl,-segaddr,GUEST_SYSRSV,0x7FFFFC000
    -Wl,-segaddr,GUEST_USER,0x1000000000
)

target_link_libraries(driveclub PRIVATE
    SDL3::SDL3
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
    "-framework CoreText"
    "-framework CoreGraphics"
)

# Developer tool: GCN disassembler + MSL translation check (gcn_dis shader.bin [vs|ps] [--msl]).
add_executable(gcn_dis EXCLUDE_FROM_ALL
    src/tools/gcn_dis.mm
    src/gpu/shader/gcn_decode.cpp
    src/gpu/shader/program.cpp
    src/gpu/shader/msl.cpp
)
target_include_directories(gcn_dis PRIVATE src)
target_compile_options(gcn_dis PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(gcn_dis PRIVATE "-framework Metal" "-framework Foundation")
