#pragma once

// Guest thread-local storage, FreeBSD/PS4 "variant II" layout:
//
//   [ static TLS of the executable ][ Tcb ]
//                                    ^ fs:0 reads Tcb::self
//
// Other modules' TLS blocks are allocated lazily through the DTV and reached
// via __tls_get_addr. Guest code finds the TCB with `mov reg, fs:[0]`. The FS
// base itself cannot be set from user space on macOS, so there are two ways to
// make those accesses work (see Mode):
//  - Ldt: FS points at a small LDT segment holding a copy of the TCB. Needs
//    i386_set_ldt, i.e. a real Intel CPU.
//  - Patched: nothing is set up in hardware. The guest code is rewritten at load
//    time (fs_patcher.h) to read the TCB pointer and canary from per-thread
//    pthread TSD slots through GS instead. Works under Rosetta 2, where there
//    is no LDT. Chosen automatically when translated; DC_TLS=ldt|patch overrides.

#include "common/types.h"

namespace Core::Tls {

struct Tcb {
    Tcb* self;
    void** dtv; // dtv[0] = generation, dtv[1] = entry count, dtv[1 + index] = block
    void* thread;
    void* spare[2];
    u64 canary;
    void* fiber;
};

struct ModuleTls {
    VAddr image;
    u64 filesz;
    u64 memsz;
    u64 align;
};

enum class Mode { Ldt, Patched };

// The mode in use (decided on first call; call it before starting guest threads).
Mode GetMode();

// Patched mode: byte offsets from the GS base (the pthread TSD array) of the
// per-thread slots the rewritten guest code reads. Valid once GetMode() ran.
u32 TcbSlotOffset();     // holds the Tcb pointer (what fs:[0] returned)
u32 CanarySlotOffset();  // holds Tcb::canary (what fs:[0x28] returned)
u32 ScratchSlotOffset(); // spill slot for the patcher's trampolines

// Registers a module's TLS template under its 1-based TLS index. Index 1 (the
// executable) becomes the static block in front of each TCB.
void RegisterModule(u32 index, const ModuleTls& tls);

// Allocates TLS for the calling thread and points FS at it.
Tcb* SetupThread();
// Frees the calling thread's TLS.
void TeardownThread();

Tcb* Current();

// Backs the guest's __tls_get_addr.
struct TlsIndex {
    u64 module;
    u64 offset;
};
void* GetAddr(const TlsIndex* index);

} // namespace Core::Tls
