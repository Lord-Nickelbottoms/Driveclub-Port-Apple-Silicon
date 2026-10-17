#include "core/fs_patcher.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <sys/mman.h>

#include <Zydis/Zydis.h>

#include "common/log.h"
#include "core/signals.h"
#include "core/tls.h"

namespace Core::FsPatch {

namespace {

constexpr u64 kHostPage = 0x1000;
constexpr s64 kCanaryDisp = 0x28; // Tls::Tcb::canary

struct Slots {
    u32 tcb;     // gs-relative offset of the slot holding the TCB pointer
    u32 canary;  // ... holding the canary value
    u32 scratch; // ... used to park the trampoline's scratch register
};

enum class Kind { None, InPlace, Trampoline, Unsupported };

struct Rewrite {
    Kind kind = Kind::None;
    size_t length = 1;           // bytes the decoder consumed (1 on failure)
    const char* reason = nullptr; // Unsupported only
    std::vector<u8> bytes;       // InPlace: replacement; Trampoline: body before the jump back
};

bool IsLegacyPrefix(u8 b) {
    switch (b) {
    case 0xF0: case 0xF2: case 0xF3: case 0x2E: case 0x36: case 0x3E:
    case 0x26: case 0x64: case 0x65: case 0x66: case 0x67:
        return true;
    default:
        return false;
    }
}

void Put32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) {
        v.push_back(static_cast<u8>(x >> (8 * i)));
    }
}

// mov gs:[off], r64     65 REX.W[R] 89 /r   (ModRM 00 reg 100, SIB 25 = disp32 only)
void EmitStoreGs(std::vector<u8>& v, int reg, u32 off) {
    v.push_back(0x65);
    v.push_back(static_cast<u8>(0x48 | ((reg >> 3) << 2)));
    v.push_back(0x89);
    v.push_back(static_cast<u8>(((reg & 7) << 3) | 4));
    v.push_back(0x25);
    Put32(v, off);
}

// mov r64, gs:[off]     65 REX.W[R] 8B /r
void EmitLoadGs(std::vector<u8>& v, int reg, u32 off) {
    v.push_back(0x65);
    v.push_back(static_cast<u8>(0x48 | ((reg >> 3) << 2)));
    v.push_back(0x8B);
    v.push_back(static_cast<u8>(((reg & 7) << 3) | 4));
    v.push_back(0x25);
    Put32(v, off);
}

Rewrite NoChange(size_t length) {
    Rewrite r;
    r.length = length;
    return r;
}

Rewrite Unsupported(size_t length, const char* why) {
    Rewrite r;
    r.kind = Kind::Unsupported;
    r.length = length;
    r.reason = why;
    return r;
}

// Decides how to rewrite the instruction at `code` (insn already decoded without
// operands, known to carry an FS prefix).
Rewrite Plan(const ZydisDecoder& dec, ZydisDecoderContext& ctx, const ZydisDecodedInstruction& insn,
             const u8* code, const Slots& slots) {
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&dec, &ctx, &insn, ops, insn.operand_count))) {
        return NoChange(insn.length);
    }

    // The memory operand that really uses FS (a prefix on lea, nop, ... is ignored).
    const ZydisDecodedOperand* mem = nullptr;
    for (u8 i = 0; i < insn.operand_count; ++i) {
        if (ops[i].type == ZYDIS_OPERAND_TYPE_MEMORY && ops[i].mem.segment == ZYDIS_REGISTER_FS) {
            mem = &ops[i];
            break;
        }
    }
    if (!mem || mem->mem.type != ZYDIS_MEMOP_TYPE_MEM) {
        return NoChange(insn.length);
    }

    if (insn.encoding != ZYDIS_INSTRUCTION_ENCODING_LEGACY) {
        return Unsupported(insn.length, "VEX/EVEX/XOP-encoded FS access");
    }
    const bool absolute = mem->mem.base == ZYDIS_REGISTER_NONE &&
                          mem->mem.index == ZYDIS_REGISTER_NONE && insn.raw.modrm.offset != 0 &&
                          insn.raw.modrm.mod == 0 && insn.raw.modrm.rm == 4 &&
                          insn.raw.sib.offset == insn.raw.modrm.offset + 1 &&
                          insn.raw.sib.base == 5 && insn.raw.sib.index == 4 &&
                          insn.raw.disp.size == 32 &&
                          insn.raw.disp.offset == insn.raw.sib.offset + 1;
    if (!absolute) {
        return Unsupported(insn.length, "FS access with base/index register or moffs form");
    }

    // Locate the FS prefix, an optional REX and the opcode.
    size_t p = 0;
    size_t fs_pos = SIZE_MAX;
    while (p < insn.length && IsLegacyPrefix(code[p])) {
        if (code[p] == 0x64) {
            fs_pos = p;
        }
        ++p;
    }
    if (fs_pos == SIZE_MAX || p >= insn.length) {
        return Unsupported(insn.length, "FS prefix not found");
    }
    const size_t rex_pos = (code[p] & 0xF0) == 0x40 ? p : SIZE_MAX;
    const size_t opcode_pos = rex_pos == SIZE_MAX ? p : p + 1;
    const size_t modrm_pos = insn.raw.modrm.offset;
    const size_t disp_pos = insn.raw.disp.offset;
    if (opcode_pos >= modrm_pos) {
        return Unsupported(insn.length, "unexpected instruction layout");
    }

    const s64 disp = mem->mem.disp.value;
    const bool writes = (mem->actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;

    // Fast path: the TCB pointer and the canary have a slot of their own, so the
    // instruction keeps its length and only swaps FS for GS.
    if (!writes && mem->size <= 64 && (disp == 0 || disp == kCanaryDisp)) {
        Rewrite r;
        r.kind = Kind::InPlace;
        r.length = insn.length;
        r.bytes.assign(code, code + insn.length);
        r.bytes[fs_pos] = 0x65;
        const u32 off = disp == 0 ? slots.tcb : slots.canary;
        std::memcpy(r.bytes.data() + disp_pos, &off, 4);
        return r;
    }

    // General path: trampoline running the instruction on [scratch + disp32].
    int scratch = -1;
    for (int cand : {10, 11}) {
        bool used = false;
        for (u8 i = 0; i < insn.operand_count; ++i) {
            if (ops[i].type != ZYDIS_OPERAND_TYPE_REGISTER) {
                continue;
            }
            const ZydisRegister big =
                ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, ops[i].reg.value);
            used |= big == (cand == 10 ? ZYDIS_REGISTER_R10 : ZYDIS_REGISTER_R11);
        }
        if (!used) {
            scratch = cand;
            break;
        }
    }
    if (scratch < 0) {
        return Unsupported(insn.length, "no free scratch register");
    }
    if (rex_pos == SIZE_MAX) {
        // Adding REX.B would turn ah/ch/dh/bh into spl/bpl/sil/dil.
        for (u8 i = 0; i < insn.operand_count; ++i) {
            if (ops[i].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                (ops[i].reg.value == ZYDIS_REGISTER_AH || ops[i].reg.value == ZYDIS_REGISTER_CH ||
                 ops[i].reg.value == ZYDIS_REGISTER_DH || ops[i].reg.value == ZYDIS_REGISTER_BH)) {
                return Unsupported(insn.length, "high-byte register with FS access");
            }
        }
    }

    Rewrite r;
    r.kind = Kind::Trampoline;
    r.length = insn.length;
    std::vector<u8>& b = r.bytes;
    EmitStoreGs(b, scratch, slots.scratch);
    EmitLoadGs(b, scratch, slots.tcb);
    for (size_t i = 0; i < opcode_pos; ++i) { // prefixes, minus FS, REX gains B
        if (i == fs_pos) {
            continue;
        }
        b.push_back(i == rex_pos ? static_cast<u8>(code[i] | 0x01) : code[i]);
    }
    if (rex_pos == SIZE_MAX) {
        b.push_back(0x41);
    }
    b.insert(b.end(), code + opcode_pos, code + modrm_pos);              // opcode bytes
    b.push_back(static_cast<u8>(0x80 | (insn.raw.modrm.reg << 3) | (scratch & 7))); // [scratch+disp32]
    b.insert(b.end(), code + disp_pos, code + insn.length);              // disp32 + immediates
    EmitLoadGs(b, scratch, slots.scratch);
    return r;
}

void WriteSite(u8* site, size_t length, const u8* tramp) {
    const s64 rel = static_cast<s64>(reinterpret_cast<VAddr>(tramp)) -
                    static_cast<s64>(reinterpret_cast<VAddr>(site) + 5);
    site[0] = 0xE9;
    const s32 rel32 = static_cast<s32>(rel);
    std::memcpy(site + 1, &rel32, 4);
    std::memset(site + 5, 0x90, length - 5);
}

} // namespace

Stats PatchRange(VAddr begin, VAddr end) {
    Stats st;
    const Slots slots{Tls::TcbSlotOffset(), Tls::CanarySlotOffset(), Tls::ScratchSlotOffset()};

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    const VAddr page_begin = AlignDown(begin, kHostPage);
    const VAddr page_end = AlignUp(end, kHostPage);
    if (mprotect(reinterpret_cast<void*>(page_begin), page_end - page_begin,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        DC_WARN("fs patcher: cannot make %#llx-%#llx writable", begin, end);
    }

    for (VAddr a = begin; a < end;) {
        auto* code = reinterpret_cast<u8*>(a);
        const size_t avail = std::min<VAddr>(end - a, 15);

        ZydisDecoderContext ctx;
        ZydisDecodedInstruction insn;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&dec, &ctx, code, avail, &insn))) {
            ++a; // data in code or a truncated tail: resynchronize
            continue;
        }
        if (!(insn.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_FS)) {
            a += insn.length;
            continue;
        }

        const Rewrite r = Plan(dec, ctx, insn, code, slots);
        switch (r.kind) {
        case Kind::None:
            break;
        case Kind::InPlace:
            std::memcpy(code, r.bytes.data(), r.length);
            ++st.in_place;
            break;
        case Kind::Trampoline: {
            std::vector<u8> body = r.bytes;
            const size_t total = body.size() + 5;
            u8* tramp = Signals::AllocTrampoline(a, total);
            if (!tramp) {
                DC_ERROR("fs patcher: no trampoline space for %#llx", a);
                ++st.unsupported;
                break;
            }
            const s64 back = static_cast<s64>(a + r.length) -
                             static_cast<s64>(reinterpret_cast<VAddr>(tramp) + body.size() + 5);
            body.push_back(0xE9);
            const s32 back32 = static_cast<s32>(back);
            for (int i = 0; i < 4; ++i) {
                body.push_back(static_cast<u8>(static_cast<u32>(back32) >> (8 * i)));
            }
            std::memcpy(tramp, body.data(), body.size());
            WriteSite(code, r.length, tramp);
            ++st.trampolines;
            break;
        }
        case Kind::Unsupported: {
            // The executable segment of a PS4 module also holds read-only data, which this
            // linear sweep decodes as code: bytes such as 'd' (0x64) in strings look like
            // FS prefixes. Most of these hits are therefore data, so only the first few are
            // listed (all of them with DC_FSPATCH_VERBOSE=1).
            static const bool verbose = std::getenv("DC_FSPATCH_VERBOSE") != nullptr;
            static int listed = 0;
            if (verbose || listed < 8) {
                ++listed;
                DC_WARN("fs patcher: %s at %#llx (%02x %02x %02x %02x %02x %02x %02x %02x)",
                        r.reason, a, code[0], code[1], code[2], code[3], code[4], code[5], code[6],
                        code[7]);
            }
            ++st.unsupported;
            break;
        }
        }
        a += r.length;
    }
    return st;
}

} // namespace Core::FsPatch
