#pragma once

// Rewrites guest instructions that use the FS segment so they work on hosts
// where FS cannot be pointed at the guest TCB.
//
// The PS4 (FreeBSD) ABI keeps the thread pointer in FS; guest code reads the
// TCB with `mov reg, fs:[0]`, the stack-protector canary with `fs:[0x28]` and a
// few other fields at small offsets. On an Intel Mac we get an FS segment from
// an LDT entry (i386_set_ldt, see tls.cpp). Under Rosetta 2 there is no LDT, and
// FS itself cannot be set from user space, so instead the guest code is patched
// once, at load time, to read the same data through GS, which on macOS always
// addresses the per-thread pthread TSD array (one 8-byte slot per pthread key):
//
//   fs:[0]  / fs:[0x28]   ->  same instruction with gs:[slot], in place
//                             (the TCB pointer and the canary each own a slot)
//   any other fs:[disp32] ->  jmp to a small trampoline that loads the TCB
//                             pointer from its slot into a scratch register,
//                             runs the original instruction on [scratch+disp32]
//                             and restores the scratch register (no stack use,
//                             so the red zone and flags are untouched)
//
// FS accesses with a base or index register, VEX/EVEX-encoded ones and the
// like are not rewritten; they are counted and logged.

#include "common/types.h"

namespace Core::FsPatch {

struct Stats {
    u64 in_place = 0;
    u64 trampolines = 0;
    u64 unsupported = 0;
};

// Patches every FS-relative instruction in [begin, end). The range must be
// writable and every site needs trampoline room from Signals::AllocTrampoline.
// Call once per module before any guest thread runs.
Stats PatchRange(VAddr begin, VAddr end);

} // namespace Core::FsPatch
