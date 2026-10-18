#!/bin/bash
# Part 2: how big a GUEST_USER segment does Rosetta accept at exec time, does reserving
# address space at runtime work instead, and where do libraries get loaded?
# Usage: bash rosetta_layout_bisect2.sh
set -u
dir=$(mktemp -d); cd "$dir" || exit 1

cat > t.c <<'C'
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
__asm__(".zerofill GUEST_TCB,GUEST_TCB,__guest_tcb,0x3FC000");
__asm__(".zerofill GUEST_SYSMGD,GUEST_SYSMGD,__guest_sysmgd,0x7FFBFC000");
__asm__(".zerofill GUEST_SYSRSV,GUEST_SYSRSV,__guest_sysrsv,0x7C0004000");
#ifdef USER_SIZE
__asm__(".zerofill GUEST_USER,GUEST_USER,__guest_user," USER_SIZE);
#endif
static void try_reserve(uint64_t addr, uint64_t size) {
    void* p = mmap((void*)addr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE, -1, 0);
    printf("  mmap fixed %#llx + %#llx -> %s\n", (unsigned long long)addr, (unsigned long long)size,
           p == MAP_FAILED ? strerror(errno) : "ok");
    if (p != MAP_FAILED) munmap(p, size);
}
int main(void) {
#ifdef RUNTIME
    int local;
    void* sdl = dlopen("/usr/local/lib/libSDL3.dylib", RTLD_NOW);
    printf("  addresses: puts=%p malloc(1)=%p stack=%p mmap(NULL)=%p SDL3=%p\n", (void*)puts, malloc(1),
           (void*)&local, mmap(NULL, 1 << 20, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0),
           sdl ? dlsym(sdl, "SDL_Init") : NULL);
    try_reserve(0x1000000000ull, 0xF000000000ull);        // to 1 TB
    try_reserve(0x1000000000ull, 0x100000000000ull - 0x1000000000ull);  // to 16 TB
    try_reserve(0x1000000000ull, 0x600000000000ull - 0x1000000000ull); // to 96 TB (the original)
#endif
    puts("alive");
    return 0;
}
C

PZ="-Wl,-no_pie,-pagezero_size,0x4000"; IB="-Wl,-image_base,0x700000000000"
BASE="$PZ $IB -Wl,-segaddr,GUEST_TCB,0x4000 -Wl,-segaddr,GUEST_SYSMGD,0x400000 -Wl,-segaddr,GUEST_SYSRSV,0x7FFFFC000"
USR="-Wl,-segaddr,GUEST_USER,0x1000000000"

run() { # name, flags...
    local name=$1; shift
    if ! clang -arch x86_64 "$@" t.c -o "v_$name" 2>"err_$name"; then
        printf '%-26s BUILD FAILED: %s\n' "$name" "$(head -c 200 "err_$name")"; return
    fi
    local out status; out=$(arch -x86_64 "./v_$name" 2>&1); status=$?
    if [ $status -eq 0 ]; then printf '%-26s ok\n' "$name"; [ -n "${SHOW:-}" ] && echo "$out"
    else printf '%-26s DIED, status %d\n' "$name" $status; fi
}

echo "-- exec-time GUEST_USER segment sizes (all on top of the 63 GB already known to work)"
for s in 0x1000000000 0x3000000000 0x7000000000 0xB000000000; do
    run "USER_size_$s" $BASE $USR "-DUSER_SIZE=\"$s\""
done
echo
echo "-- runtime reservation instead (no GUEST_USER segment); libraries and addresses:"
SHOW=1 run runtime $BASE -DRUNTIME
echo; echo "Send me this output. Files are in $dir"
