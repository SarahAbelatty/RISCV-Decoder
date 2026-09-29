// riscv_vector.hpp -- RISC-V "V" (RVV 1.0) decoder.
// Covers: vsetvli/vsetivli/vsetvl, vector loads/stores (unit-stride, strided,
// indexed, segment, whole-register, mask, fault-only-first), and OP-V
// arithmetic (integer, fixed-point, mask, permutation, reductions, FP).
// Output follows binutils/objdump syntax. Each function returns "" if the
// encoding is not a valid vector instruction (caller prints .word).
#pragma once
#include <cstdint>
#include <string>

namespace riscv { namespace vec {

inline std::string xr(unsigned r) {
    static const char *n[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","t3","t4","t5","t6"};
    return n[r & 31];
}
inline std::string fr(unsigned r) {
    static const char *n[32] = {
        "ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","fs0","fs1","fa0","fa1","fa2","fa3","fa4","fa5",
        "fa6","fa7","fs2","fs3","fs4","fs5","fs6","fs7","fs8","fs9","fs10","fs11","ft8","ft9","ft10","ft11"};
    return n[r & 31];
}
inline std::string vr(unsigned r) { return "v" + std::to_string(r & 31); }
inline int sext5(uint32_t v) { return (int)((v & 0x1f) ^ 0x10) - 0x10; }

inline std::string vtype_str(uint32_t z) {
    static const char *sew[4]  = {"e8", "e16", "e32", "e64"};
    static const char *lmul[8] = {"m1", "m2", "m4", "m8", nullptr, "mf8", "mf4", "mf2"};
    uint32_t vsew = (z >> 3) & 7, vlmul = z & 7;
    if (vsew > 3 || !lmul[vlmul] || (z >> 8)) {           // reserved -> raw hex, like objdump
        char b[16]; snprintf(b, sizeof b, "0x%x", z); return b;
    }
    return std::string(sew[vsew]) + "," + lmul[vlmul] + "," +
           (((z >> 6) & 1) ? "ta" : "tu") + "," + (((z >> 7) & 1) ? "ma" : "mu");
}

// ---------------- vector loads / stores (opcodes LOAD-FP 0x07 / STORE-FP 0x27) ----------------
inline std::string mem(uint32_t insn, bool store) {
    uint32_t width = (insn >> 12) & 7;
    int eew;
    switch (width) { case 0: eew = 8; break; case 5: eew = 16; break; case 6: eew = 32; break;
                     case 7: eew = 64; break; default: return ""; }   // 1..4 = scalar FP / reserved
    uint32_t nf = (insn >> 29) & 7, mew = (insn >> 28) & 1, mop = (insn >> 26) & 3;
    uint32_t vm = (insn >> 25) & 1, lumop = (insn >> 20) & 0x1f;
    unsigned rs1 = (insn >> 15) & 31, vd = (insn >> 7) & 31;
    if (mew) return "";
    std::string m = vm ? "" : ",v0.t", e = std::to_string(eew), n = std::to_string(nf + 1);
    std::string base = "(" + xr(rs1) + ")", ld = store ? "vs" : "vl";
    if (mop == 0) {                                        // unit-stride
        switch (lumop) {
        case 0x00:
            return ld + (nf ? "seg" + n : "") + "e" + e + ".v " + vr(vd) + "," + base + m;
        case 0x08:                                         // whole register
            if (!vm || !(nf == 0 || nf == 1 || nf == 3 || nf == 7)) return "";
            if (store) { if (width != 0) return ""; return "vs" + n + "r.v " + vr(vd) + "," + base; }
            return "vl" + n + (eew == 8 ? std::string("r.v ") : "re" + e + ".v ") + vr(vd) + "," + base;  // objdump aliases vlNre8.v -> vlNr.v
        case 0x0b:                                         // mask load/store
            if (width != 0 || nf != 0 || !vm) return "";
            return ld + "m.v " + vr(vd) + "," + base;
        case 0x10:                                         // fault-only-first (load only)
            if (store) return "";
            return "vl" + (nf ? "seg" + n : "") + "e" + e + "ff.v " + vr(vd) + "," + base + m;
        }
        return "";
    }
    if (mop == 2)                                          // strided
        return ld + (nf ? "sseg" + n + "e" : "se") + e + ".v " + vr(vd) + "," + base + "," + xr(lumop) + m;
    // indexed: 1 = unordered, 3 = ordered
    return std::string("v") + (store ? "s" : "l") + (mop == 1 ? "ux" : "ox") +
           (nf ? "seg" + n : "") + "ei" + e + ".v " + vr(vd) + "," + base + "," + vr(lumop) + m;
}

// ---------------- OP-V arithmetic ----------------
struct Op { uint8_t f6; const char *name; const char *kinds; char style; };
// style: n=normal(simm5) u=normal(uimm5) w=wide/narrow src (.w?) m=macc order (vd,src,vs2)
//        r=reduction (.vs) M=mask logical (.mm) c=carry-in (needs v0) C=carry-out

inline const Op *find(const Op *t, size_t n, uint32_t f6, char k) {
    for (size_t i = 0; i < n; i++)
        if (t[i].f6 == f6) for (const char *p = t[i].kinds; *p; p++) if (*p == k) return &t[i];
    return nullptr;
}

inline std::string emit(const Op &o, char k, uint32_t insn) {
    uint32_t vm = (insn >> 25) & 1;
    unsigned vd = (insn >> 7) & 31, s1 = (insn >> 15) & 31, s2 = (insn >> 20) & 31;
    std::string m = vm ? "" : ",v0.t", n = o.name;
    std::string kc(1, k == 'V' ? 'v' : k == 'X' ? 'x' : k == 'I' ? 'i' : 'f');
    std::string src = k == 'V' ? vr(s1) : k == 'X' ? xr(s1) : k == 'F' ? fr(s1)
                    : std::to_string((o.style == 'u' || o.style == 'w') ? (int)s1 : sext5(s1));
    switch (o.style) {
    case 'n': case 'u': return n + ".v" + kc + " " + vr(vd) + "," + vr(s2) + "," + src + m;
    case 'w':           return n + ".w" + kc + " " + vr(vd) + "," + vr(s2) + "," + src + m;
    case 'm':           return n + ".v" + kc + " " + vr(vd) + "," + src + "," + vr(s2) + m;
    case 'r':           return n + ".vs " + vr(vd) + "," + vr(s2) + "," + vr(s1) + m;
    case 'M':           if (!vm) return "";
                        return n + ".mm " + vr(vd) + "," + vr(s2) + "," + vr(s1);
    case 'c':           if (vm) return "";
                        return n + ".v" + kc + "m " + vr(vd) + "," + vr(s2) + "," + src + ",v0";
    case 'C':           if (vm) return n + ".v" + kc + " " + vr(vd) + "," + vr(s2) + "," + src;
                        return n + ".v" + kc + "m " + vr(vd) + "," + vr(s2) + "," + src + ",v0";
    }
    return "";
}

#define VOPS(name) static const Op name[] =
#define COUNT(t) (sizeof(t) / sizeof(t[0]))

inline std::string int_op(uint32_t insn, char k) {           // OPIVV / OPIVI / OPIVX
    uint32_t f6 = insn >> 26, vm = (insn >> 25) & 1;
    unsigned vd = (insn >> 7) & 31, s1 = (insn >> 15) & 31, s2 = (insn >> 20) & 31;
    std::string m = vm ? "" : ",v0.t";
    std::string kc(1, k == 'V' ? 'v' : k == 'X' ? 'x' : 'i');
    std::string src = k == 'V' ? vr(s1) : k == 'X' ? xr(s1) : std::to_string(sext5(s1));

    if (f6 == 0x17) {                                          // vmerge / vmv.v.*
        if (!vm) return "vmerge.v" + kc + "m " + vr(vd) + "," + vr(s2) + "," + src + ",v0";
        if (s2 == 0) return "vmv.v." + kc + " " + vr(vd) + "," + src;
        return "";
    }
    if (f6 == 0x27 && k == 'I') {                              // vmv<nr>r.v
        unsigned nr = s1 + 1;
        if (vm && (nr == 1 || nr == 2 || nr == 4 || nr == 8))
            return "vmv" + std::to_string(nr) + "r.v " + vr(vd) + "," + vr(s2);
        return "";
    }
    if (f6 == 0x0b && k == 'I' && s1 == 31) return "vnot.v " + vr(vd) + "," + vr(s2) + m;
    if (f6 == 0x03 && k == 'X' && s1 == 0)  return "vneg.v " + vr(vd) + "," + vr(s2) + m;
    if (f6 == 0x2c && k == 'X' && s1 == 0)  return "vncvt.x.x.w " + vr(vd) + "," + vr(s2) + m;

    VOPS(T) {
        {0x00,"vadd","VXI",'n'},{0x02,"vsub","VX",'n'},{0x03,"vrsub","XI",'n'},
        {0x04,"vminu","VX",'n'},{0x05,"vmin","VX",'n'},{0x06,"vmaxu","VX",'n'},{0x07,"vmax","VX",'n'},
        {0x09,"vand","VXI",'n'},{0x0a,"vor","VXI",'n'},{0x0b,"vxor","VXI",'n'},
        {0x0c,"vrgather","VXI",'u'},{0x0e,"vrgatherei16","V",'n'},
        {0x0e,"vslideup","XI",'u'},{0x0f,"vslidedown","XI",'u'},
        {0x10,"vadc","VXI",'c'},{0x11,"vmadc","VXI",'C'},{0x12,"vsbc","VX",'c'},{0x13,"vmsbc","VX",'C'},
        {0x18,"vmseq","VXI",'n'},{0x19,"vmsne","VXI",'n'},{0x1a,"vmsltu","VX",'n'},{0x1b,"vmslt","VX",'n'},
        {0x1c,"vmsleu","VXI",'n'},{0x1d,"vmsle","VXI",'n'},{0x1e,"vmsgtu","XI",'n'},{0x1f,"vmsgt","XI",'n'},
        {0x20,"vsaddu","VXI",'n'},{0x21,"vsadd","VXI",'n'},{0x22,"vssubu","VX",'n'},{0x23,"vssub","VX",'n'},
        {0x25,"vsll","VXI",'u'},{0x27,"vsmul","VX",'n'},
        {0x28,"vsrl","VXI",'u'},{0x29,"vsra","VXI",'u'},{0x2a,"vssrl","VXI",'u'},{0x2b,"vssra","VXI",'u'},
        {0x2c,"vnsrl","VXI",'w'},{0x2d,"vnsra","VXI",'w'},{0x2e,"vnclipu","VXI",'w'},{0x2f,"vnclip","VXI",'w'},
        {0x30,"vwredsumu","V",'r'},{0x31,"vwredsum","V",'r'},
    };
    const Op *o = find(T, COUNT(T), f6, k);
    return o ? emit(*o, k, insn) : "";
}

inline std::string mul_op(uint32_t insn, char k) {           // OPMVV / OPMVX
    uint32_t f6 = insn >> 26, vm = (insn >> 25) & 1;
    unsigned vd = (insn >> 7) & 31, s1 = (insn >> 15) & 31, s2 = (insn >> 20) & 31;
    std::string m = vm ? "" : ",v0.t";

    if (k == 'V') {
        if (f6 == 0x10) {                                      // VWXUNARY0
            if (s1 == 0x00 && vm) return "vmv.x.s " + xr(vd) + "," + vr(s2);
            if (s1 == 0x10) return "vcpop.m " + xr(vd) + "," + vr(s2) + m;
            if (s1 == 0x11) return "vfirst.m " + xr(vd) + "," + vr(s2) + m;
            return "";
        }
        if (f6 == 0x12) {                                      // VXUNARY0
            static const char *n[8] = {nullptr,nullptr,"vzext.vf8","vsext.vf8","vzext.vf4","vsext.vf4","vzext.vf2","vsext.vf2"};
            if (s1 < 8 && n[s1]) return std::string(n[s1]) + " " + vr(vd) + "," + vr(s2) + m;
            return "";
        }
        if (f6 == 0x14) {                                      // VMUNARY0
            if (s1 == 0x01) return "vmsbf.m " + vr(vd) + "," + vr(s2) + m;
            if (s1 == 0x02) return "vmsof.m " + vr(vd) + "," + vr(s2) + m;
            if (s1 == 0x03) return "vmsif.m " + vr(vd) + "," + vr(s2) + m;
            if (s1 == 0x10) return "viota.m " + vr(vd) + "," + vr(s2) + m;
            if (s1 == 0x11 && s2 == 0) return "vid.v " + vr(vd) + m;
            return "";
        }
        if (f6 == 0x17) return vm ? "vcompress.vm " + vr(vd) + "," + vr(s2) + "," + vr(s1) : "";
        if (vm) {                                              // mask-register pseudos
            if (f6 == 0x19 && s1 == s2) return "vmmv.m " + vr(vd) + "," + vr(s2);
            if (f6 == 0x1b && s1 == vd && s2 == vd) return "vmclr.m " + vr(vd);
            if (f6 == 0x1f && s1 == vd && s2 == vd) return "vmset.m " + vr(vd);
            if (f6 == 0x1d && s1 == s2) return "vmnot.m " + vr(vd) + "," + vr(s2);
        }
    } else {
        if (f6 == 0x10) return (s2 == 0 && vm) ? "vmv.s.x " + vr(vd) + "," + xr(s1) : "";
        if (f6 == 0x30 && s1 == 0) return "vwcvtu.x.x.v " + vr(vd) + "," + vr(s2) + m;
        if (f6 == 0x31 && s1 == 0) return "vwcvt.x.x.v " + vr(vd) + "," + vr(s2) + m;
    }
    VOPS(T) {
        {0x00,"vredsum","V",'r'},{0x01,"vredand","V",'r'},{0x02,"vredor","V",'r'},{0x03,"vredxor","V",'r'},
        {0x04,"vredminu","V",'r'},{0x05,"vredmin","V",'r'},{0x06,"vredmaxu","V",'r'},{0x07,"vredmax","V",'r'},
        {0x08,"vaaddu","VX",'n'},{0x09,"vaadd","VX",'n'},{0x0a,"vasubu","VX",'n'},{0x0b,"vasub","VX",'n'},
        {0x0e,"vslide1up","X",'n'},{0x0f,"vslide1down","X",'n'},
        {0x18,"vmandn","V",'M'},{0x19,"vmand","V",'M'},{0x1a,"vmor","V",'M'},{0x1b,"vmxor","V",'M'},
        {0x1c,"vmorn","V",'M'},{0x1d,"vmnand","V",'M'},{0x1e,"vmnor","V",'M'},{0x1f,"vmxnor","V",'M'},
        {0x20,"vdivu","VX",'n'},{0x21,"vdiv","VX",'n'},{0x22,"vremu","VX",'n'},{0x23,"vrem","VX",'n'},
        {0x24,"vmulhu","VX",'n'},{0x25,"vmul","VX",'n'},{0x26,"vmulhsu","VX",'n'},{0x27,"vmulh","VX",'n'},
        {0x29,"vmadd","VX",'m'},{0x2b,"vnmsub","VX",'m'},{0x2d,"vmacc","VX",'m'},{0x2f,"vnmsac","VX",'m'},
        {0x30,"vwaddu","VX",'n'},{0x31,"vwadd","VX",'n'},{0x32,"vwsubu","VX",'n'},{0x33,"vwsub","VX",'n'},
        {0x34,"vwaddu","VX",'w'},{0x35,"vwadd","VX",'w'},{0x36,"vwsubu","VX",'w'},{0x37,"vwsub","VX",'w'},
        {0x38,"vwmulu","VX",'n'},{0x3a,"vwmulsu","VX",'n'},{0x3b,"vwmul","VX",'n'},
        {0x3c,"vwmaccu","VX",'m'},{0x3d,"vwmacc","VX",'m'},{0x3e,"vwmaccus","X",'m'},{0x3f,"vwmaccsu","VX",'m'},
    };
    const Op *o = find(T, COUNT(T), f6, k);
    return o ? emit(*o, k, insn) : "";
}

inline std::string fp_op(uint32_t insn, char k) {            // OPFVV / OPFVF  (k = 'V' or 'F')
    uint32_t f6 = insn >> 26, vm = (insn >> 25) & 1;
    unsigned vd = (insn >> 7) & 31, s1 = (insn >> 15) & 31, s2 = (insn >> 20) & 31;
    std::string m = vm ? "" : ",v0.t";

    if (f6 == 0x10) {
        if (k == 'V') return (s1 == 0 && vm) ? "vfmv.f.s " + fr(vd) + "," + vr(s2) : "";
        return (s2 == 0 && vm) ? "vfmv.s.f " + vr(vd) + "," + fr(s1) : "";
    }
    if (k == 'V' && (f6 == 0x12 || f6 == 0x13)) {
        struct U { uint8_t f6, s1; const char *n; };
        static const U u[] = {
            {0x12,0x00,"vfcvt.xu.f.v"},{0x12,0x01,"vfcvt.x.f.v"},{0x12,0x02,"vfcvt.f.xu.v"},{0x12,0x03,"vfcvt.f.x.v"},
            {0x12,0x06,"vfcvt.rtz.xu.f.v"},{0x12,0x07,"vfcvt.rtz.x.f.v"},
            {0x12,0x08,"vfwcvt.xu.f.v"},{0x12,0x09,"vfwcvt.x.f.v"},{0x12,0x0a,"vfwcvt.f.xu.v"},{0x12,0x0b,"vfwcvt.f.x.v"},
            {0x12,0x0c,"vfwcvt.f.f.v"},{0x12,0x0e,"vfwcvt.rtz.xu.f.v"},{0x12,0x0f,"vfwcvt.rtz.x.f.v"},
            {0x12,0x10,"vfncvt.xu.f.w"},{0x12,0x11,"vfncvt.x.f.w"},{0x12,0x12,"vfncvt.f.xu.w"},{0x12,0x13,"vfncvt.f.x.w"},
            {0x12,0x14,"vfncvt.f.f.w"},{0x12,0x15,"vfncvt.rod.f.f.w"},
            {0x12,0x16,"vfncvt.rtz.xu.f.w"},{0x12,0x17,"vfncvt.rtz.x.f.w"},
            {0x13,0x00,"vfsqrt.v"},{0x13,0x04,"vfrsqrt7.v"},{0x13,0x05,"vfrec7.v"},{0x13,0x10,"vfclass.v"},
        };
        for (const U &e : u) if (e.f6 == f6 && e.s1 == s1) return std::string(e.n) + " " + vr(vd) + "," + vr(s2) + m;
        return "";
    }
    if (f6 == 0x17 && k == 'F') {
        if (!vm) return "vfmerge.vfm " + vr(vd) + "," + vr(s2) + "," + fr(s1) + ",v0";
        return s2 == 0 ? "vfmv.v.f " + vr(vd) + "," + fr(s1) : "";
    }
    if (k == 'V' && s1 == s2) {
        if (f6 == 0x09) return "vfneg.v " + vr(vd) + "," + vr(s2) + m;
        if (f6 == 0x0a) return "vfabs.v " + vr(vd) + "," + vr(s2) + m;
    }
    VOPS(T) {
        {0x00,"vfadd","VF",'n'},{0x01,"vfredusum","V",'r'},{0x02,"vfsub","VF",'n'},{0x03,"vfredosum","V",'r'},
        {0x04,"vfmin","VF",'n'},{0x05,"vfredmin","V",'r'},{0x06,"vfmax","VF",'n'},{0x07,"vfredmax","V",'r'},
        {0x08,"vfsgnj","VF",'n'},{0x09,"vfsgnjn","VF",'n'},{0x0a,"vfsgnjx","VF",'n'},
        {0x0e,"vfslide1up","F",'n'},{0x0f,"vfslide1down","F",'n'},
        {0x18,"vmfeq","VF",'n'},{0x19,"vmfle","VF",'n'},{0x1b,"vmflt","VF",'n'},{0x1c,"vmfne","VF",'n'},
        {0x1d,"vmfgt","F",'n'},{0x1f,"vmfge","F",'n'},
        {0x20,"vfdiv","VF",'n'},{0x21,"vfrdiv","F",'n'},{0x24,"vfmul","VF",'n'},{0x27,"vfrsub","F",'n'},
        {0x28,"vfmadd","VF",'m'},{0x29,"vfnmadd","VF",'m'},{0x2a,"vfmsub","VF",'m'},{0x2b,"vfnmsub","VF",'m'},
        {0x2c,"vfmacc","VF",'m'},{0x2d,"vfnmacc","VF",'m'},{0x2e,"vfmsac","VF",'m'},{0x2f,"vfnmsac","VF",'m'},
        {0x30,"vfwadd","VF",'n'},{0x31,"vfwredusum","V",'r'},{0x32,"vfwsub","VF",'n'},{0x33,"vfwredosum","V",'r'},
        {0x34,"vfwadd","VF",'w'},{0x36,"vfwsub","VF",'w'},{0x38,"vfwmul","VF",'n'},
        {0x3c,"vfwmacc","VF",'m'},{0x3d,"vfwnmacc","VF",'m'},{0x3e,"vfwmsac","VF",'m'},{0x3f,"vfwnmsac","VF",'m'},
    };
    const Op *o = find(T, COUNT(T), f6, k);
    return o ? emit(*o, k, insn) : "";
}

// Entry point for opcode 0x57 (OP-V)
inline std::string op_v(uint32_t insn) {
    uint32_t f3 = (insn >> 12) & 7;
    unsigned rd = (insn >> 7) & 31, rs1 = (insn >> 15) & 31, rs2 = (insn >> 20) & 31;
    switch (f3) {
    case 0: return int_op(insn, 'V');
    case 3: return int_op(insn, 'I');
    case 4: return int_op(insn, 'X');
    case 1: return fp_op(insn, 'V');
    case 5: return fp_op(insn, 'F');
    case 2: return mul_op(insn, 'V');
    case 6: return mul_op(insn, 'X');
    case 7:                                                    // OPCFG
        if ((insn >> 30) == 0)
            return "vsetvli " + xr(rd) + "," + xr(rs1) + "," + vtype_str((insn >> 20) & 0x7ff);
        if ((insn >> 30) == 3)
            return "vsetivli " + xr(rd) + "," + std::to_string(rs1) + "," + vtype_str((insn >> 20) & 0x3ff);
        if ((insn >> 25) == 0x40)
            return "vsetvl " + xr(rd) + "," + xr(rs1) + "," + xr(rs2);
    }
    return "";
}

}} // namespace riscv::vec
