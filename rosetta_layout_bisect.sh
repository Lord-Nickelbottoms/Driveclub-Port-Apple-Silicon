#!/bin/bash
# Finds which part of driveclub's fixed Mach-O layout Rosetta 2 refuses.
# Builds a trivial x86_64 program with the layout flags added one at a time and
# runs each variant; the first one that dies is the culprit.
# Usage: bash rosetta_layout_bisect.sh      (needs Xcode command line tools)
set -u
dir=$(mktemp -d); cd "$dir" || exit 1

cat > t.c <<'C'
#include <stdio.h>
#ifdef V_TCB
__asm__(".zerofill GUEST_TCB,GUEST_TCB,__guest_tcb,0x3FC000");
#endif
#ifdef V_SYSMGD
__asm__(".zerofill GUEST_SYSMGD,GUEST_SYSMGD,__guest_sysmgd,0x7FFBFC000");
#endif
#ifdef V_SYSRSV
__asm__(".zerofill GUEST_SYSRSV,GUEST_SYSRSV,__guest_sysrsv,0x7C0004000");
#endif
#ifdef V_USER
__asm__(".zerofill GUEST_USER,GUEST_USER,__guest_user,0xF000000000");
#endif
int main(void) { puts("alive"); return 0; }
C

PZ="-Wl,-no_pie,-pagezero_size,0x4000"
IB="-Wl,-image_base,0x700000000000"
TCB="-DV_TCB -Wl,-segaddr,GUEST_TCB,0x4000"
MGD="-DV_SYSMGD -Wl,-segaddr,GUEST_SYSMGD,0x400000"
RSV="-DV_SYSRSV -Wl,-segaddr,GUEST_SYSRSV,0x7FFFFC000"
USR="-DV_USER -Wl,-segaddr,GUEST_USER,0x1000000000"

try() {  # name, flags...
    local name=$1; shift
    if ! clang -arch x86_64 "$@" t.c -o "v_$name" 2>"err_$name"; then
        printf '%-34s BUILD FAILED: %s\n' "$name" "$(head -c 200 "err_$name")"; return
    fi
    local out status
    out=$(arch -x86_64 "./v_$name" 2>&1); status=$?
    if [ "$status" -eq 0 ]; then printf '%-34s ok (%s)\n' "$name" "$out"
    else printf '%-34s DIED, status %d\n' "$name" "$status"; fi
}

echo "-- single flags"
try pagezero_only        $PZ
try image_base_only      $IB
echo "-- cumulative (first DIED line is the culprit)"
try 1_plain
try 2_pagezero           $PZ
try 3_+image_base        $PZ $IB
try 4_+TCB               $PZ $IB $TCB
try 5_+SYSMGD_32GB       $PZ $IB $TCB $MGD
try 6_+SYSRSV_31GB       $PZ $IB $TCB $MGD $RSV
try 7_+USER_1TB          $PZ $IB $TCB $MGD $RSV $USR
echo
echo "Send me this output. Files are in $dir"
