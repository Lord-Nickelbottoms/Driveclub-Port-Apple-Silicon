import subprocess, os, sys
from fs_patcher_cases import CASES
out=[]
for name, asm, kind in CASES:
    open("t.s","w").write(".intel_syntax noprefix\n.text\n"+asm+"\n")
    subprocess.check_call(["as","--64","t.s","-o","t.o"])
    subprocess.check_call(["objcopy","-O","binary","-j",".text","t.o","t.bin"])
    b=open("t.bin","rb").read()
    out.append((name,kind,b))
with open("fs_patcher_cases.inc","w") as f:
    f.write("struct Case { const char* name; const char* kind; std::vector<unsigned char> code; };\n")
    f.write("static std::vector<Case> kCases = {\n")
    for name,kind,b in out:
        f.write('  {"%s","%s",{%s}},\n'%(name,kind,",".join(str(x) for x in b)))
    f.write("};\n")
