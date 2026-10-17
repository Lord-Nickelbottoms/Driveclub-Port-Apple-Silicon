# FS patcher test (Linux x86-64 only)

`fs_patcher_test_linux.cpp` runs small `fs:`-using snippets natively with a real FS base, then
the same snippets after `Core::FsPatch::PatchRange` with FS = 0 and GS pointing at a fake TSD
block, and compares registers (including the r10/r11 scratch candidates), flags, memory, the red
zone and RSP. It needs GNU `as`, Zydis (v4.1.1) and a Linux x86-64 host; it does not run on macOS.

```bash
cd tests && python3 fs_patcher_gen.py          # assembles the cases into fs_patcher_cases.inc
g++ -std=c++23 -O1 -masm=intel -w -I../src -I<zydis>/include -I<zydis>/dependencies/zycore/include \
    -I<zydis>/build -I<zydis>/build/zycore fs_patcher_test_linux.cpp ../src/core/fs_patcher.cpp \
    <zydis>/build/libZydis.a <zydis>/build/zycore/libZycore.a -o fs_patcher_test && ./fs_patcher_test
```
