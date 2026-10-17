# Zydis x86 decoder, for the guest FS patcher. Skipped when a Zydis target already
# exists (the Vulkan renderer's cmake/ShadPS4Vulkan.cmake uses the same one).
include_guard(GLOBAL)

if (NOT TARGET Zydis)
    include(FetchContent)
    set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(dc_zydis
        GIT_REPOSITORY https://github.com/zyantific/zydis.git
        GIT_TAG v4.1.1
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(dc_zydis)
    if (NOT dc_zydis_POPULATED)
        message(STATUS "fetching Zydis v4.1.1")
        FetchContent_Populate(dc_zydis)
        add_subdirectory(${dc_zydis_SOURCE_DIR} ${dc_zydis_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
