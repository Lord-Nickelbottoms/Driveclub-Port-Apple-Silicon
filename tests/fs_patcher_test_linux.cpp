// Linux x86-64 test: runs guest-style FS code natively with a real FS base, then the
// patched version with FS=0 and GS pointing at a fake TSD block, and compares.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include "core/fs_patcher.h"
#include "core/signals.h"
#include "core/tls.h"
#include "fs_patcher_cases.inc"

static u8* g_tramp_next; static u8* g_tramp_end;
namespace Core::Tls {
u32 TcbSlotOffset() { return 0x100; }
u32 CanarySlotOffset() { return 0x108; }
u32 ScratchSlotOffset() { return 0x110; }
}
namespace Core::Signals {
u8* AllocTrampoline(VAddr, u64 size) {
    if (g_tramp_next + size > g_tramp_end) return nullptr;
    u8* p = g_tramp_next; g_tramp_next = (u8*)AlignUp((u64)g_tramp_next + size, 16); return p;
}
}

extern "C" void run_with_bases(const void* code, u64 fsbase, u64 gsbase, u64* regs, u64 rdi_arg);
asm(R"(
.globl run_with_bases
run_with_bases:
  push rbx
  push r12
  push r13
  push r14
  push r15
  sub rsp, 32
  mov r12, rdi
  mov r13, rsi
  mov r14, rdx
  mov r15, rcx
  mov rbx, r8
  mov eax, 158
  mov edi, 0x1003
  lea rsi, [rsp]
  syscall
  mov eax, 158
  mov edi, 0x1004
  lea rsi, [rsp+8]
  syscall
  mov eax, 158
  mov edi, 0x1002
  mov rsi, r13
  syscall
  mov eax, 158
  mov edi, 0x1001
  mov rsi, r14
  syscall
  mov rdi, rbx
  mov r10, 0x1010101010101010
  mov r11, 0x1111111111111111
  mov r8,  0x0808080808080808
  mov rcx, 0x0c0c0c0c0c0c0c0c
  mov rdx, 0x0d0d0d0d0d0d0d0d
  call r12
  mov [r15+0], rax
  mov [r15+8], rcx
  mov [r15+16], rdx
  mov [r15+24], rdi
  mov [r15+32], r8
  mov [r15+40], r10
  mov [r15+48], r11
  mov rbx, rax
  mov eax, 158
  mov edi, 0x1002
  mov rsi, [rsp]
  syscall
  mov eax, 158
  mov edi, 0x1001
  mov rsi, [rsp+8]
  syscall
  mov rax, rbx
  add rsp, 32
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  ret
)");

int main() {
    // asm above is AT&T-default in gcc top-level asm; we compile with -masm=intel
    u8* region = (u8*)mmap(nullptr, 1 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    g_tramp_next = region + 0x80000; g_tramp_end = region + 0xF0000;
    int fails = 0;
    for (auto& c : kCases) {
        const std::string kind = c.kind;
        u8* c1 = region + 0x10000; u8* c2 = region + 0x20000;
        std::memset(c1, 0xCC, 0x1000); std::memset(c2, 0xCC, 0x1000);
        std::memcpy(c1, c.code.data(), c.code.size());
        std::memcpy(c2, c.code.data(), c.code.size());
        auto st = Core::FsPatch::PatchRange((VAddr)c2, (VAddr)c2 + c.code.size());

        bool ok = true;
        if (kind == "inplace") ok = st.in_place >= 1 && st.trampolines == 0 && st.unsupported == 0 && std::memcmp(c2, c.code.data(), c.code.size()) != 0 && std::memcmp(c2 + 0, c.code.data(), 0) == 0;
        if (kind == "tramp") ok = st.trampolines >= 1 && st.in_place == 0 && st.unsupported == 0;
        if (kind == "unsupported") ok = st.unsupported == 1 && st.in_place == 0 && st.trampolines == 0;
        if (kind == "none") ok = st.unsupported == 0 && st.in_place == 0 && st.trampolines == 0 && !std::memcmp(c1, c2, c.code.size());
        if (!ok) { std::printf("FAIL %-18s planner stats in=%llu tr=%llu un=%llu\n", c.name, st.in_place, st.trampolines, st.unsupported); ++fails; continue; }
        if (kind == "unsupported") { std::printf("ok   %-18s (left unpatched, as expected)\n", c.name); continue; }

        u8* mem = (u8*)aligned_alloc(4096, 16384);
        u8* tcb = mem + 8192;
        for (int i = 0; i < 16384; ++i) mem[i] = (u8)(i * 7 + 3);
        *(u64*)tcb = (u64)tcb;
        *(u64*)(tcb + 0x30) = 0;      // so cmp/cmpxchg cases are meaningful
        *(u64*)(tcb + 0x38) = 41;
        std::vector<u8> snap(mem, mem + 16384);

        u8* G = (u8*)aligned_alloc(4096, 8192); std::memset(G, 0xAB, 8192);
        *(u64*)(G + 0x100) = (u64)tcb; *(u64*)(G + 0x108) = *(u64*)(tcb + 0x28); *(u64*)(G + 0x110) = 0xDEAD;

        u64 r1[8]{}, r2[8]{};
        run_with_bases(c1, (u64)tcb, 0, r1, 0x99);
        std::vector<u8> after1(mem, mem + 16384);
        std::memcpy(mem, snap.data(), 16384);
        run_with_bases(c2, 0, (u64)G, r2, 0x99);
        std::vector<u8> after2(mem, mem + 16384);

        bool same = !std::memcmp(r1, r2, sizeof r1) && after1 == after2;
        if (kind == "none") same = same; // identical code, trivially
        std::printf("%s %-18s rax=%#llx/%#llx  %s\n", same ? "ok  " : "FAIL", c.name, r1[0], r2[0],
                    same ? "" : "<-- mismatch");
        if (!same) { ++fails; for (int i=0;i<7;++i) std::printf("   reg%d %#llx vs %#llx\n", i, r1[i], r2[i]); }
        free(mem); free(G);
    }
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
