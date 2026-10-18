#!/bin/bash
# Does Rosetta fault when translating code that sits at the end of an executable region?
# Builds a mock "text segment" (a small function + padding up to a 16 KB boundary) followed by
# a second page in various states, calls the function in a child process and reports how it ends.
# Usage: bash rosetta_exec_boundary_test.sh
set -u
dir=$(mktemp -d); cd "$dir" || exit 1
cat > t.c <<'C'
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PG 0x4000

// push rbp; mov rbp,rsp; call helper; pop rbp; ret; ...; helper: ret
static const unsigned char kFn[] = {0x55, 0x48, 0x89, 0xe5, 0xe8, 0x07, 0x00, 0x00, 0x00, 0x5d,
                                    0xc3, 0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};

static int run(int fill, int next) {
    pid_t pid = fork();
    if (pid == 0) {
        unsigned char* m = mmap(NULL, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED) _exit(90);
        memset(m, fill, PG);                // padding byte for the rest of the text page
        memcpy(m, kFn, sizeof kFn);
        memset(m + PG, 0xAB, PG);           // "data" page
        if (mprotect(m, PG, PROT_READ | PROT_EXEC)) _exit(91);
        switch (next) {
        case 0: break;                                                   // rw-
        case 1: mprotect(m + PG, PG, PROT_READ | PROT_WRITE | PROT_EXEC); break; // rwx
        case 2: mprotect(m + PG, PG, PROT_NONE); break;                  // ---
        case 3: munmap(m + PG, PG); break;                               // unmapped
        }
        ((void (*)(void))m)();
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 100 + WTERMSIG(st);
}

int main(void) {
    const char* fills[] = {"zero padding", "nop padding", "int3 padding"};
    const int fillv[] = {0x00, 0x90, 0xCC};
    const char* nexts[] = {"next page rw-", "next page rwx", "next page ---", "next page unmapped"};
    printf("%-14s", "");
    for (int n = 0; n < 4; ++n) printf("%-20s", nexts[n]);
    printf("\n");
    for (int f = 0; f < 3; ++f) {
        printf("%-14s", fills[f]);
        for (int n = 0; n < 4; ++n) {
            int r = run(fillv[f], n);
            char buf[24];
            if (r == 0) snprintf(buf, sizeof buf, "ok");
            else if (r == 110) snprintf(buf, sizeof buf, "SIGBUS");
            else if (r == 111) snprintf(buf, sizeof buf, "SIGSEGV");
            else snprintf(buf, sizeof buf, "code %d", r);
            printf("%-20s", buf);
        }
        printf("\n");
    }
    return 0;
}
C
clang -arch x86_64 -O0 t.c -o t && arch -x86_64 ./t
