// riscv_disasm.hpp
// Decodes RISC-V machine code into text, objdump-style.
// Covers: RV32I/RV64I base integer ISA, M (multiply/divide) extension,
// C (compressed, 16-bit) extension, FENCE/ECALL/EBREAK, and CSR ops.
// Floating point (F/D) and atomics (A) are recognized by opcode but
// printed as ".word" placeholders (see decode32 default case) since the
// user asked specifically for a RISC-V *decoder*, not a full FPU model;
// this keeps the implementation focused and correct rather than guessing.
#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

namespace riscv {

struct DecodedInsn {
    uint32_t raw;      // raw bits (16 or 32)
    int size;          // 2 or 4 bytes
    std::string text;  // "addi sp,sp,-32" etc.
};

inline int32_t sext(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

inline std::string reg(int r) {
    static const char *names[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2",
        "s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7",
        "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    if (r < 0 || r > 31) return "x?";
    return names[r];
}

// Compressed instructions only address x8-x15; encoded as 3 bits.
inline int creg(int r3) { return r3 + 8; }

inline std::string hexi(int64_t v) {
    std::ostringstream os;
    if (v < 0) os << "-0x" << std::hex << -v;
    else       os << "0x"  << std::hex << v;
    return os.str();
}

// Absolute target of a pc-relative branch/jump. RV32 addresses are 32 bits wide,
// so a backward jump near address 0 wraps to 0xfffffffc, not 0xfffffffffffffffc.
inline uint64_t branch_target(uint64_t addr, int32_t off, bool rv64) {
    uint64_t t = addr + (uint64_t)(int64_t)off;
    return rv64 ? t : (t & 0xffffffffull);
}

inline std::string csr_name(uint32_t csr) {
    switch (csr) {
        case 0x001: return "fflags";
        case 0x002: return "frm";
        case 0x003: return "fcsr";
        case 0xc00: return "cycle";
        case 0xc01: return "time";
        case 0xc02: return "instret";
        case 0xc80: return "cycleh";
        case 0xc81: return "timeh";
        case 0xc82: return "instreth";
        case 0x100: return "sstatus";
        case 0x104: return "sie";
        case 0x105: return "stvec";
        case 0x140: return "sscratch";
        case 0x141: return "sepc";
        case 0x142: return "scause";
        case 0x143: return "stval";
        case 0x144: return "sip";
        case 0x180: return "satp";
        case 0x300: return "mstatus";
        case 0x301: return "misa";
        case 0x304: return "mie";
        case 0x305: return "mtvec";
        case 0x340: return "mscratch";
        case 0x341: return "mepc";
        case 0x342: return "mcause";
        case 0x343: return "mtval";
        case 0x344: return "mip";
        case 0xf11: return "mvendorid";
        case 0xf12: return "marchid";
        case 0xf13: return "mimpid";
        case 0xf14: return "mhartid";
        case 0xc20: return "vl";
        case 0xc21: return "vtype";
        case 0xc22: return "vlenb";
        default: {
            std::ostringstream os; os << "0x" << std::hex << csr;
            return os.str();
        }
    }
}

// ---------------- 32-bit instruction decode ----------------
inline std::string decode32(uint32_t insn, uint64_t addr, bool rv64) {
    uint32_t opcode = insn & 0x7f;
    uint32_t rd     = (insn >> 7) & 0x1f;
    uint32_t funct3 = (insn >> 12) & 0x7;
    uint32_t rs1    = (insn >> 15) & 0x1f;
    uint32_t rs2    = (insn >> 20) & 0x1f;
    uint32_t funct7 = (insn >> 25) & 0x7f;

    int32_t imm_i = sext(insn >> 20, 12);
    int32_t imm_s = sext(((insn >> 25) << 5) | ((insn >> 7) & 0x1f), 12);
    int32_t imm_b = sext((((insn >> 31) & 1) << 12) | (((insn >> 7) & 1) << 11) |
                          (((insn >> 25) & 0x3f) << 5) | (((insn >> 8) & 0xf) << 1), 13);
    int32_t imm_j = sext((((insn >> 31) & 1) << 20) | (((insn >> 12) & 0xff) << 12) |
                         (((insn >> 20) & 1) << 11) | (((insn >> 21) & 0x3ff) << 1), 21);

    std::ostringstream os;

    auto R = reg;

    switch (opcode) {
    case 0b0110111: // LUI
        os << "lui " << R(rd) << "," << hexi(insn >> 12);   // unsigned 20-bit field, like objdump
        return os.str();
    case 0b0010111: // AUIPC
        os << "auipc " << R(rd) << "," << hexi(insn >> 12);
        return os.str();
    case 0b1101111: { // JAL
        uint64_t target = branch_target(addr, imm_j, rv64);
        if (rd == 0) os << "j ";
        else if (rd == 1) os << "jal ";
        else os << "jal " << R(rd) << ",";
        os << "0x" << std::hex << target;
        return os.str();
    }
    case 0b1100111: { // JALR
        if (funct3 != 0) { os << "unknown(jalr funct3)"; return os.str(); }
        if (rd == 0 && imm_i == 0 && rs1 == 1) { os << "ret"; return os.str(); }
        if (rd == 0) os << "jr ";
        else os << "jalr " << R(rd) << ",";
        os << imm_i << "(" << R(rs1) << ")";
        return os.str();
    }
    case 0b1100011: { // Branch
        static const char *names[8] = {"beq","bne",nullptr,nullptr,"blt","bge","bltu","bgeu"};
        const char *nm = names[funct3];
        if (!nm) { os << "unknown(branch)"; return os.str(); }
        uint64_t target = branch_target(addr, imm_b, rv64);
        os << nm << " " << R(rs1) << "," << R(rs2) << ",0x" << std::hex << target;
        return os.str();
    }
    case 0b0000011: { // Load
        static const char *names[8] = {"lb","lh","lw","ld","lbu","lhu","lwu",nullptr};
        const char *nm = names[funct3];
        if (!nm) { os << "unknown(load)"; return os.str(); }
        os << nm << " " << R(rd) << "," << imm_i << "(" << R(rs1) << ")";
        return os.str();
    }
    case 0b0100011: { // Store
        static const char *names[8] = {"sb","sh","sw","sd",nullptr,nullptr,nullptr,nullptr};
        const char *nm = names[funct3];
        if (!nm) { os << "unknown(store)"; return os.str(); }
        os << nm << " " << R(rs2) << "," << imm_s << "(" << R(rs1) << ")";
        return os.str();
    }
    case 0b0010011: { // OP-IMM
        uint32_t shamt = insn >> 20;
        uint32_t funct6 = insn >> 26;      // for Zbs/Zbb shift-family immediates
        uint32_t shamt6 = (insn >> 20) & 0x3f;
        switch (funct3) {
        case 0b000:
            if (rs1 == 0 && imm_i == 0 && rd == 0) { os << "nop"; return os.str(); }
            if (rs1 == 0) { os << "li " << R(rd) << "," << imm_i; return os.str(); }      // pseudo: li
            if (imm_i == 0) { os << "mv " << R(rd) << "," << R(rs1); return os.str(); }    // pseudo: mv
            os << "addi " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b010: os << "slti " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b011:
            if (imm_i == 1) { os << "seqz " << R(rd) << "," << R(rs1); return os.str(); } // pseudo: sltiu rd,rs,1
            os << "sltiu " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b100: os << "xori " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b110: os << "ori " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b111:
            if (imm_i == 255) { os << "zext.b " << R(rd) << "," << R(rs1); return os.str(); } // Zbb pseudo
            os << "andi " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b001: // SLLI, or Zbs BCLRI/BSETI/BINVI, or Zbb CLZ/CTZ/CPOP/SEXT.B/SEXT.H
            if (funct7 == 0b0110000) { // Zbb unary ops, selected by rs2 field
                switch (rs2) {
                case 0b00000: os << "clz " << R(rd) << "," << R(rs1); return os.str();
                case 0b00001: os << "ctz " << R(rd) << "," << R(rs1); return os.str();
                case 0b00010: os << "cpop " << R(rd) << "," << R(rs1); return os.str();
                case 0b00100: os << "sext.b " << R(rd) << "," << R(rs1); return os.str();
                case 0b00101: os << "sext.h " << R(rd) << "," << R(rs1); return os.str();
                }
            }
            if (funct6 == 0b010010) { os << "bclri " << R(rd) << "," << R(rs1) << "," << hexi(shamt6); return os.str(); }
            if (funct6 == 0b001010) { os << "bseti " << R(rd) << "," << R(rs1) << "," << hexi(shamt6); return os.str(); }
            if (funct6 == 0b011010) { os << "binvi " << R(rd) << "," << R(rs1) << "," << hexi(shamt6); return os.str(); }
            os << "slli " << R(rd) << "," << R(rs1) << "," << (shamt & (rv64 ? 0x3f : 0x1f)); return os.str();
        case 0b101: // SRLI/SRAI, or Zbs BEXTI, or Zbb RORI/ORC.B/REV8
            if (imm_i == 0x287) { os << "orc.b " << R(rd) << "," << R(rs1); return os.str(); }
            if (imm_i == (rv64 ? 0x6b8 : 0x698)) { os << "rev8 " << R(rd) << "," << R(rs1); return os.str(); }
            if (funct6 == 0b010010) { os << "bexti " << R(rd) << "," << R(rs1) << "," << hexi(shamt6); return os.str(); }
            if (funct6 == 0b011000) { os << "rori " << R(rd) << "," << R(rs1) << "," << hexi(shamt6); return os.str(); }
            {
                bool arith = (insn >> 30) & 1;
                os << (arith ? "srai " : "srli ") << R(rd) << "," << R(rs1) << "," << (shamt & (rv64 ? 0x3f : 0x1f));
                return os.str();
            }
        }
        break;
    }
    case 0b0011011: { // OP-IMM-32 (RV64 only)
        uint32_t shamt = insn >> 20 & 0x1f;
        switch (funct3) {
        case 0b000: os << "addiw " << R(rd) << "," << R(rs1) << "," << imm_i; return os.str();
        case 0b001: // SLLIW, or Zba SLLI.UW, or Zbb CLZW/CTZW/CPOPW
            if (funct7 == 0b0110000) {
                switch (rs2) {
                case 0b00000: os << "clzw " << R(rd) << "," << R(rs1); return os.str();
                case 0b00001: os << "ctzw " << R(rd) << "," << R(rs1); return os.str();
                case 0b00010: os << "cpopw " << R(rd) << "," << R(rs1); return os.str();
                }
            }
            if ((insn >> 26) == 0b000010) { os << "slli.uw " << R(rd) << "," << R(rs1) << "," << hexi((insn >> 20) & 0x3f); return os.str(); }
            os << "slliw " << R(rd) << "," << R(rs1) << "," << shamt; return os.str();
        case 0b101:
            if (funct7 == 0b0110000) { // Zbb RORIW
                os << "roriw " << R(rd) << "," << R(rs1) << "," << hexi(shamt); return os.str();
            }
            {
                bool arith = (insn >> 30) & 1;
                os << (arith ? "sraiw " : "srliw ") << R(rd) << "," << R(rs1) << "," << shamt;
                return os.str();
            }
        }
        break;
    }
    case 0b0110011: { // OP (R-type): base ALU + M extension + Zba/Zbb/Zicond
        if (funct7 == 0b0000001) { // M extension
            static const char *names[8] = {"mul","mulh","mulhsu","mulhu","div","divu","rem","remu"};
            os << names[funct3] << " " << R(rd) << "," << R(rs1) << "," << R(rs2);
            return os.str();
        }
        if (funct7 == 0b0010000) { // Zba: sh1add/sh2add/sh3add
            if (funct3 == 0b010) { os << "sh1add " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b100) { os << "sh2add " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b110) { os << "sh3add " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0100000) { // Zbb: andn/orn/xnor (share funct7 with sub/sra, disambiguated by funct3)
            if (funct3 == 0b111) { os << "andn " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b110) { os << "orn " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b100) { os << "xnor " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0000101) { // Zbb: min/minu/max/maxu
            static const char *names[8] = {nullptr,nullptr,nullptr,nullptr,"min","minu","max","maxu"};
            const char *nm = names[funct3];
            if (nm) { os << nm << " " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0110000) { // Zbb: rol/ror
            if (funct3 == 0b001) { os << "rol " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b101) { os << "ror " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0000111) { // Zicond: czero.eqz/czero.nez
            if (funct3 == 0b101) { os << "czero.eqz " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b111) { os << "czero.nez " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0100100) { // Zbs: bclr/bext (share funct7 with andn/xnor's neighbor, disambiguated by funct3)
            if (funct3 == 0b001) { os << "bclr " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b101) { os << "bext " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0110100 && funct3 == 0b001) { os << "binv " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); } // Zbs
        if (funct7 == 0b0010100 && funct3 == 0b001) { os << "bset " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); } // Zbs
        switch (funct3) {
        case 0b000: os << (funct7 & 0x20 ? "sub " : "add ") << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b001: os << "sll " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b010:
            if (rs1 == 0) { os << "sgtz " << R(rd) << "," << R(rs2); return os.str(); }   // pseudo: slt rd,zero,rs2
            if (rs2 == 0) { os << "sltz " << R(rd) << "," << R(rs1); return os.str(); }   // pseudo: slt rd,rs1,zero
            os << "slt " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b011:
            if (rs1 == 0) { os << "snez " << R(rd) << "," << R(rs2); return os.str(); }   // pseudo: sltu rd,zero,rs2
            os << "sltu " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b100: os << "xor " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b101: os << (funct7 & 0x20 ? "sra " : "srl ") << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b110: os << "or " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b111: os << "and " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        }
        break;
    }
    case 0b0111011: { // OP-32 (RV64 only): W-suffixed base ALU + M extension + Zba/Zbb
        if (funct7 == 0b0000001) {
            static const char *names[8] = {"mulw",nullptr,nullptr,nullptr,"divw","divuw","remw","remuw"};
            const char *nm = names[funct3];
            if (nm) { os << nm << " " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            break;
        }
        if (funct7 == 0b0000100 && funct3 == 0b000) { // Zba: add.uw (zext.w when rs2==zero)
            if (rs2 == 0) { os << "zext.w " << R(rd) << "," << R(rs1); return os.str(); }
            os << "add.uw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        }
        if (funct7 == 0b0010000) { // Zba: sh1add.uw/sh2add.uw/sh3add.uw
            if (funct3 == 0b010) { os << "sh1add.uw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b100) { os << "sh2add.uw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b110) { os << "sh3add.uw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        if (funct7 == 0b0110000) { // Zbb: rolw/rorw
            if (funct3 == 0b001) { os << "rolw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
            if (funct3 == 0b101) { os << "rorw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str(); }
        }
        switch (funct3) {
        case 0b000:
            if ((funct7 & 0x20) && rs1 == 0) { os << "negw " << R(rd) << "," << R(rs2); return os.str(); } // pseudo: subw rd,zero,rs2
            os << (funct7 & 0x20 ? "subw " : "addw ") << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b001: os << "sllw " << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        case 0b101: os << (funct7 & 0x20 ? "sraw " : "srlw ") << R(rd) << "," << R(rs1) << "," << R(rs2); return os.str();
        }
        break;
    }
    case 0b0101111: { // AMO (A extension): LR/SC/AMOSWAP/AMOADD/AMOXOR/AMOAND/AMOOR/AMOMIN(U)/AMOMAX(U)
        if (funct3 != 0b010 && funct3 != 0b011) break;      // .w (010) or .d (011) only
        if (funct3 == 0b011 && !rv64) break;                 // .d needs RV64
        const char *sz = (funct3 == 0b011) ? ".d" : ".w";
        uint32_t funct5 = insn >> 27;
        bool aq = (insn >> 26) & 1, rl = (insn >> 25) & 1;
        std::string suf = aq && rl ? ".aqrl" : aq ? ".aq" : rl ? ".rl" : "";
        static const char *names[32] = {
            "amoadd",nullptr,"lr","sc","amoxor",nullptr,nullptr,nullptr,
            "amoor",nullptr,nullptr,nullptr,"amoand",nullptr,nullptr,nullptr,
            "amomin",nullptr,nullptr,nullptr,"amomax",nullptr,nullptr,nullptr,
            "amominu",nullptr,nullptr,nullptr,"amomaxu",nullptr,nullptr,nullptr
        };
        const char *nm = names[funct5];
        if (funct5 == 0b00001) nm = "amoswap";
        if (!nm) break;
        if (funct5 == 0b00010) { // LR: no rs2
            if (rs2 != 0) break;
            os << nm << sz << suf << " " << R(rd) << ",(" << R(rs1) << ")";
        } else {
            os << nm << sz << suf << " " << R(rd) << "," << R(rs2) << ",(" << R(rs1) << ")";
        }
        return os.str();
    }
    case 0b0001111: { // FENCE / FENCE.I
        if (funct3 == 1) { os << "fence.i"; return os.str(); }
        if (funct3 == 0) {
            uint32_t pred = (insn >> 24) & 0xf, succ = (insn >> 20) & 0xf;
            if (pred == 0b0001 && succ == 0 && rd == 0 && rs1 == 0) { os << "pause"; return os.str(); } // Zihintpause
            auto iorw = [](uint32_t b) {
                std::string s;
                if (b & 0x8) s += 'i';
                if (b & 0x4) s += 'o';
                if (b & 0x2) s += 'r';
                if (b & 0x1) s += 'w';
                return s;
            };
            os << "fence " << iorw(pred) << "," << iorw(succ);
            return os.str();
        }
        break;
    }
    case 0b1110011: { // SYSTEM: ECALL/EBREAK/CSR*
        uint32_t imm12 = insn >> 20;
        if (funct3 == 0) {
            if (imm12 == 0) { os << "ecall"; return os.str(); }
            if (imm12 == 1) { os << "ebreak"; return os.str(); }
            if (imm12 == 0x302) { os << "mret"; return os.str(); }
            if (imm12 == 0x102) { os << "sret"; return os.str(); }
            if (imm12 == 0x105) { os << "wfi"; return os.str(); }
            break;
        }
        std::string csr = csr_name(imm12);
        bool is_imm_form = funct3 >= 5;   // csrrwi/csrrsi/csrrci: 5-bit uimm lives in rs1's slot
        uint32_t op = funct3 & 0x3;       // 1=write, 2=set, 3=clear (same grouping for reg/imm forms)
        // Pseudo-op simplification, same rules objdump uses:
        //   rd==0   -> drop destination, use the short csrw/csrs/csrc(i) form
        //   rs1==0 (set/clear, register form only) -> pure read, "csrr rd,csr"
        if (op == 1) { // CSRRW / CSRRWI
            if (rd == 0) {
                if (is_imm_form) { os << "csrwi " << csr << "," << rs1; return os.str(); }
                os << "csrw " << csr << "," << R(rs1); return os.str();
            }
        } else if (!is_imm_form && rs1 == 0 && (op == 2 || op == 3)) { // CSRRS/CSRRC with rs1=x0 -> pure read
            os << "csrr " << R(rd) << "," << csr; return os.str();
        } else if (rd == 0 && (op == 2 || op == 3)) { // CSRRS/CSRRC(I) with rd=x0 -> write-only short form
            const char *nm = (op == 2) ? (is_imm_form ? "csrsi" : "csrs") : (is_imm_form ? "csrci" : "csrc");
            if (is_imm_form) os << nm << " " << csr << "," << rs1;
            else             os << nm << " " << csr << "," << R(rs1);
            return os.str();
        }
        static const char *names[8] = {nullptr,"csrrw","csrrs","csrrc",nullptr,"csrrwi","csrrsi","csrrci"};
        const char *nm = names[funct3];
        if (!nm) break;
        if (is_imm_form)
            os << nm << " " << R(rd) << "," << csr << "," << rs1;
        else
            os << nm << " " << R(rd) << "," << csr << "," << R(rs1);
        return os.str();
    }
    default: break;
    }
    os << ".word 0x" << std::hex << std::setw(8) << std::setfill('0') << insn
       << "  # unsupported opcode (likely F/D float or V vector extension)";
    return os.str();
}

// ---------------- 16-bit compressed instruction decode ----------------
inline std::string decode16(uint16_t insn, uint64_t addr, bool rv64) {
    uint32_t op  = insn & 0x3;
    uint32_t funct3 = (insn >> 13) & 0x7;
    std::ostringstream os;
    auto R = reg;

    if (insn == 0) { os << "unimp"; return os.str(); }

    if (op == 0b00) {
        uint32_t rd_ = creg((insn >> 2) & 0x7);
        uint32_t rs1_ = creg((insn >> 7) & 0x7);
        switch (funct3) {
        case 0b000: { // C.ADDI4SPN
            uint32_t nzuimm = ((insn >> 5 & 1) << 3) | ((insn >> 6 & 1) << 2) |
                               ((insn >> 7 & 0xf) << 6) | ((insn >> 11 & 0x3) << 4);
            if (nzuimm == 0) { os << "unimp"; return os.str(); }
            os << "addi " << R(rd_) << ",sp," << nzuimm; return os.str();
        }
        case 0b010: { // C.LW
            uint32_t imm = ((insn >> 6 & 1) << 2) | ((insn >> 10 & 0x7) << 3) | ((insn >> 5 & 1) << 6);
            os << "lw " << R(rd_) << "," << imm << "(" << R(rs1_) << ")"; return os.str();
        }
        case 0b011: { // C.LD (RV64) / C.FLW (RV32, unsupported)
            if (!rv64) break;
            uint32_t imm = ((insn >> 10 & 0x7) << 3) | ((insn >> 5 & 0x3) << 6);
            os << "ld " << R(rd_) << "," << imm << "(" << R(rs1_) << ")"; return os.str();
        }
        case 0b110: { // C.SW
            uint32_t imm = ((insn >> 6 & 1) << 2) | ((insn >> 10 & 0x7) << 3) | ((insn >> 5 & 1) << 6);
            os << "sw " << R((insn >> 2 & 0x7) + 8) << "," << imm << "(" << R(rs1_) << ")"; return os.str();
        }
        case 0b111: { // C.SD (RV64)
            if (!rv64) break;
            uint32_t imm = ((insn >> 10 & 0x7) << 3) | ((insn >> 5 & 0x3) << 6);
            os << "sd " << R((insn >> 2 & 0x7) + 8) << "," << imm << "(" << R(rs1_) << ")"; return os.str();
        }
        case 0b100: { // Zcb: C.LBU (funct6=100000; other Zcb load/store forms not yet implemented)
            if ((insn >> 10 & 0x7) == 0b000) {
                uint32_t uimm = ((insn >> 5 & 1) << 1) | (insn >> 6 & 1);
                os << "lbu " << R(rd_) << "," << uimm << "(" << R(rs1_) << ")"; return os.str();
            }
            break;
        }
        }
    } else if (op == 0b01) {
        uint32_t rd = (insn >> 7) & 0x1f;
        int32_t imm6 = sext(((insn >> 12 & 1) << 5) | (insn >> 2 & 0x1f), 6);
        switch (funct3) {
        case 0b000: // C.ADDI / C.NOP
            if (rd == 0 && imm6 == 0) { os << "nop"; return os.str(); }
            os << "addi " << R(rd) << "," << R(rd) << "," << imm6; return os.str();
        case 0b001: // C.ADDIW (RV64) — on RV32 this slot is C.JAL
            if (rv64) { os << "addiw " << R(rd) << "," << R(rd) << "," << imm6; return os.str(); }
            else {
                uint32_t b = ((insn>>12&1)<<11)|((insn>>8&1)<<10)|((insn>>9&3)<<8)|
                             ((insn>>6&1)<<7)|((insn>>7&1)<<6)|((insn>>2&1)<<5)|
                             ((insn>>11&1)<<4)|((insn>>3&7)<<1);
                int32_t off = sext(b, 12);
                os << "jal ra,0x" << std::hex << branch_target(addr, off, rv64); return os.str();
            }
        case 0b010: // C.LI
            os << "li " << R(rd) << "," << imm6; return os.str();
        case 0b011: { // C.ADDI16SP / C.LUI
            if (rd == 2) {
                uint32_t b = ((insn>>12&1)<<9)|((insn>>6&1)<<4)|((insn>>5&1)<<6)|
                             ((insn>>3&3)<<7)|((insn>>2&1)<<5);
                int32_t imm = sext(b, 10);
                if (imm == 0) { os << "unimp"; return os.str(); }
                os << "addi sp,sp," << imm; return os.str();
            } else {
                int32_t imm = sext(((insn>>12&1)<<17)|((insn>>2&0x1f)<<12), 18);
                if (imm == 0) break;
                os << "lui " << R(rd) << "," << hexi((imm >> 12) & 0xfffff); return os.str();
            }
        }
        case 0b100: { // misc-alu
            uint32_t rd_ = creg((insn >> 7) & 0x7);
            uint32_t funct2 = (insn >> 10) & 0x3;
            if (funct2 == 0b00) { // C.SRLI
                uint32_t sh = ((insn>>12&1)<<5) | (insn>>2&0x1f);
                os << "srli " << R(rd_) << "," << R(rd_) << "," << sh; return os.str();
            }
            if (funct2 == 0b01) { // C.SRAI
                uint32_t sh = ((insn>>12&1)<<5) | (insn>>2&0x1f);
                os << "srai " << R(rd_) << "," << R(rd_) << "," << sh; return os.str();
            }
            if (funct2 == 0b10) { // C.ANDI
                os << "andi " << R(rd_) << "," << R(rd_) << "," << imm6; return os.str();
            }
            // funct2 == 0b11: register-register ops
            uint32_t rs2_ = creg((insn >> 2) & 0x7);
            uint32_t funct6bit = (insn >> 12) & 1;
            uint32_t f2b = (insn >> 5) & 0x3;
            if (funct6bit == 0) {
                static const char *names[4] = {"sub","xor","or","and"};
                os << names[f2b] << " " << R(rd_) << "," << R(rd_) << "," << R(rs2_); return os.str();
            } else {
                static const char *names[4] = {"subw","addw",nullptr,nullptr};
                const char *nm = names[f2b];
                if (!nm) break;
                os << nm << " " << R(rd_) << "," << R(rd_) << "," << R(rs2_); return os.str();
            }
        }
        case 0b101: { // C.J
            uint32_t b = ((insn>>12&1)<<11)|((insn>>8&1)<<10)|((insn>>9&3)<<8)|
                         ((insn>>6&1)<<7)|((insn>>7&1)<<6)|((insn>>2&1)<<5)|
                         ((insn>>11&1)<<4)|((insn>>3&7)<<1);
            int32_t off = sext(b, 12);
            os << "j 0x" << std::hex << branch_target(addr, off, rv64); return os.str();
        }
        case 0b110: case 0b111: { // C.BEQZ / C.BNEZ
            uint32_t rs1_ = creg((insn >> 7) & 0x7);
            uint32_t b = ((insn>>12&1)<<8)|((insn>>5&3)<<6)|((insn>>2&1)<<5)|
                         ((insn>>10&3)<<3)|((insn>>3&3)<<1);
            int32_t off = sext(b, 9);
            os << (funct3 == 0b110 ? "beqz " : "bnez ") << R(rs1_) << ",0x" << std::hex << branch_target(addr, off, rv64);
            return os.str();
        }
        }
    } else if (op == 0b10) {
        uint32_t rd = (insn >> 7) & 0x1f;
        uint32_t rs2 = (insn >> 2) & 0x1f;
        switch (funct3) {
        case 0b000: { // C.SLLI
            uint32_t sh = ((insn>>12&1)<<5) | rs2;
            os << "slli " << R(rd) << "," << R(rd) << "," << sh; return os.str();
        }
        case 0b010: { // C.LWSP
            if (rd == 0) break;
            uint32_t imm = ((insn>>4&7)<<2) | ((insn>>12&1)<<5) | ((insn>>2&3)<<6);
            os << "lw " << R(rd) << "," << imm << "(sp)"; return os.str();
        }
        case 0b011: { // C.LDSP (RV64)
            if (!rv64 || rd == 0) break;
            uint32_t imm = ((insn>>5&3)<<3) | ((insn>>12&1)<<5) | ((insn>>2&7)<<6);
            os << "ld " << R(rd) << "," << imm << "(sp)"; return os.str();
        }
        case 0b100: {
            bool bit12 = (insn >> 12) & 1;
            if (!bit12 && rs2 == 0) { // C.JR
                if (rd == 0) break;
                if (rd == 1) { os << "ret"; return os.str(); }
                os << "jr " << R(rd); return os.str();
            }
            if (!bit12 && rs2 != 0) { // C.MV
                os << "mv " << R(rd) << "," << R(rs2); return os.str();
            }
            if (bit12 && rd == 0 && rs2 == 0) { os << "ebreak"; return os.str(); }
            if (bit12 && rs2 == 0) { // C.JALR
                os << "jalr " << R(rd); return os.str();
            }
            if (bit12 && rs2 != 0) { // C.ADD
                os << "add " << R(rd) << "," << R(rd) << "," << R(rs2); return os.str();
            }
            break;
        }
        case 0b110: { // C.SWSP
            uint32_t imm = ((insn>>9&0xf)<<2) | ((insn>>7&3)<<6);
            os << "sw " << R(rs2) << "," << imm << "(sp)"; return os.str();
        }
        case 0b111: { // C.SDSP (RV64)
            if (!rv64) break;
            uint32_t imm = ((insn>>10&7)<<3) | ((insn>>7&7)<<6);
            os << "sd " << R(rs2) << "," << imm << "(sp)"; return os.str();
        }
        }
    }
    os << ".half 0x" << std::hex << std::setw(4) << std::setfill('0') << insn
       << "  # unsupported compressed opcode (likely F/D extension)";
    return os.str();
}

// Reads one instruction at `data[off]` (off < size guaranteed by caller),
// figures out if it's 16- or 32-bit, decodes it, returns the result.
inline DecodedInsn decode_one(const unsigned char *data, size_t avail, uint64_t addr, bool rv64) {
    DecodedInsn out;
    if (avail < 2) {
        // A single stray byte cannot be an instruction; never claim more than is available.
        out.raw = data[0];
        out.size = 1;
        out.text = ".byte (truncated)";
        return out;
    }
    uint16_t lo16 = (uint16_t)(data[0] | (data[1] << 8));
    if ((lo16 & 0x3) != 0x3) {
        // Compressed 16-bit instruction
        out.raw = lo16;
        out.size = 2;
        out.text = decode16(lo16, addr, rv64);
    } else if (avail >= 4) {
        uint32_t insn = lo16 | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        out.raw = insn;
        out.size = 4;
        out.text = decode32(insn, addr, rv64);
    } else {
        // Truncated at end of section
        out.raw = lo16;
        out.size = (int)avail;
        out.text = ".byte (truncated)";
    }
    return out;
}

} // namespace riscv
