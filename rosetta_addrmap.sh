#!/bin/bash
# Part 3: what does Rosetta's address space look like, and which fixed ranges can we take?
# Usage: bash rosetta_addrmap.sh > addrmap.txt 2>&1   (then send addrmap.txt)
set -u
dir=$(mktemp -d); cd "$dir" || exit 1

cat > t.c <<'C'
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifdef SEGS
__asm__(".zerofill GUEST_SYSMGD,GUEST_SYSMGD,__guest_sysmgd,0x7FFBFC000");
__asm__(".zerofill GUEST_SYSRSV,GUEST_SYSRSV,__guest_sysrsv,0x7C0004000");
#endif

static const char* kr_name(kern_return_t kr) {
    switch (kr) {
    case KERN_SUCCESS: return "ok";
    case KERN_INVALID_ADDRESS: return "INVALID_ADDRESS";
    case KERN_NO_SPACE: return "NO_SPACE (occupied)";
    case KERN_PROTECTION_FAILURE: return "PROTECTION_FAILURE";
    case KERN_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case KERN_RESOURCE_SHORTAGE: return "RESOURCE_SHORTAGE";
    default: return "other";
    }
}

int main(void) {
    printf("pagesize=%d\n-- mappings below 0x7ff000000000 (and a count of the rest)\n", (int)getpagesize());
    mach_vm_address_t a = 0; int high = 0;
    for (;;) {
        mach_vm_size_t sz = 0; vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t obj;
        if (mach_vm_region(mach_task_self(), &a, &sz, VM_REGION_BASIC_INFO_64,
                           (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS) break;
        if (a >= 0x7ff000000000ull) high++;
        else printf("  %#14llx-%#14llx  prot=%d max=%d\n", (unsigned long long)a, (unsigned long long)(a + sz),
                    info.protection, info.max_protection);
        a += sz;
    }
    printf("  (+%d regions at or above 0x7ff000000000)\n-- mach_vm_allocate FIXED probes\n", high);

    const uint64_t addrs[] = {0x10000000ull, 0x100000000ull, 0x800000000ull, 0xFC0000000ull,
        0x1000000000ull, 0x2000000000ull, 0x4000000000ull, 0x6000000000ull, 0x10000000000ull,
        0x40000000000ull, 0x100000000000ull, 0x200000000000ull, 0x400000000000ull, 0x500000000000ull};
    const uint64_t sizes[] = {0x4000ull, 0x40000000ull, 0x1000000000ull};
    for (unsigned i = 0; i < sizeof addrs / sizeof *addrs; ++i) {
        printf("  %#14llx:", (unsigned long long)addrs[i]);
        for (unsigned j = 0; j < 3; ++j) {
            mach_vm_address_t p = addrs[i];
            kern_return_t kr = mach_vm_allocate(mach_task_self(), &p, sizes[j], VM_FLAGS_FIXED);
            printf("  %#llx->%s", (unsigned long long)sizes[j], kr_name(kr));
            if (kr == KERN_SUCCESS) mach_vm_deallocate(mach_task_self(), p, sizes[j]);
        }
        printf("\n");
    }
    return 0;
}
C

echo "=== A: default layout (what Rosetta gives an ordinary x86_64 program)"
clang -arch x86_64 t.c -o a_plain && arch -x86_64 ./a_plain; echo "exit $?"
echo
echo "=== B: driveclub's small pagezero + image base + low segments (the combination that ran)"
clang -arch x86_64 -DSEGS t.c -o b_base -Wl,-no_pie,-pagezero_size,0x4000 \
  -Wl,-image_base,0x700000000000 -Wl,-segaddr,GUEST_SYSMGD,0x400000 -Wl,-segaddr,GUEST_SYSRSV,0x7FFFFC000
arch -x86_64 ./b_base; echo "exit $?"
