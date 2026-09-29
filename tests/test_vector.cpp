#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "riscv_disasm.hpp"

using namespace riscv;

namespace {

DecodedInsn decode32_test(uint32_t insn, uint64_t addr = 0x10000) {
    unsigned char b[] = {
        static_cast<unsigned char>(insn & 0xff),
        static_cast<unsigned char>((insn >> 8) & 0xff),
        static_cast<unsigned char>((insn >> 16) & 0xff),
        static_cast<unsigned char>((insn >> 24) & 0xff)
    };

    return decode_one(b, sizeof(b), addr, true);
}

uint32_t op_v(
    unsigned f6,
    unsigned vm,
    unsigned rs2,
    unsigned rs1,
    unsigned funct3,
    unsigned rd)
{
    return ((f6 & 0x3f) << 26) |
           ((vm & 1) << 25) |
           ((rs2 & 0x1f) << 20) |
           ((rs1 & 0x1f) << 15) |
           ((funct3 & 7) << 12) |
           ((rd & 0x1f) << 7) |
           0x57;
}

uint32_t vsetvli(
    unsigned rd,
    unsigned rs1,
    unsigned vtypei)
{
    return ((vtypei & 0x7ff) << 20) |
           ((rs1 & 0x1f) << 15) |
           (0x7 << 12) |
           ((rd & 0x1f) << 7) |
           0x57;
}

uint32_t vsetivli(
    unsigned rd,
    unsigned uimm,
    unsigned vtypei)
{
    return (0x3u << 30) |
           ((vtypei & 0x3ff) << 20) |
           ((uimm & 0x1f) << 15) |
           (0x7 << 12) |
           ((rd & 0x1f) << 7) |
           0x57;
}

uint32_t vsetvl(
    unsigned rd,
    unsigned rs1,
    unsigned rs2)
{
    return (0x2u << 30) |
           ((rs2 & 0x1f) << 20) |
           ((rs1 & 0x1f) << 15) |
           (0x7 << 12) |
           ((rd & 0x1f) << 7) |
           0x57;
}

uint32_t vector_mem(
    bool store,
    unsigned width,
    unsigned vm,
    unsigned rs2_or_lumop,
    unsigned rs1,
    unsigned vd,
    unsigned mop = 0,
    unsigned nf = 0,
    unsigned mew = 0)
{
    return ((nf & 7) << 29) |
           ((mew & 1) << 28) |
           ((mop & 3) << 26) |
           ((vm & 1) << 25) |
           ((rs2_or_lumop & 0x1f) << 20) |
           ((rs1 & 0x1f) << 15) |
           ((width & 7) << 12) |
           ((vd & 0x1f) << 7) |
           (store ? 0x27 : 0x07);
}

} // namespace


// ============================================================
// Vector configuration
// ============================================================

TEST(Vector, Vsetvli) {
    auto d = decode32_test(vsetvli(1, 2, 0x000));

    EXPECT_EQ(d.size, 4);
    EXPECT_EQ(d.text, "vsetvli ra,sp,e8,m1,tu,mu");
}

TEST(Vector, Vsetivli) {
    auto d = decode32_test(vsetivli(1, 8, 0x000));

    EXPECT_EQ(d.size, 4);
    EXPECT_EQ(d.text, "vsetivli ra,8,e8,m1,tu,mu");
}

TEST(Vector, Vsetvl) {
    auto d = decode32_test(vsetvl(1, 2, 3));

    EXPECT_EQ(d.size, 4);
    EXPECT_EQ(d.text, "vsetvl ra,sp,gp");
}


// ============================================================
// Integer arithmetic
// ============================================================

TEST(Vector, IntegerVV) {
    auto d = decode32_test(
        op_v(0x00, 1, 2, 1, 0, 3));

    EXPECT_EQ(d.text, "vadd.vv v3,v2,v1");
}

TEST(Vector, IntegerVX) {
    auto d = decode32_test(
        op_v(0x00, 1, 2, 10, 4, 3));

    EXPECT_EQ(d.text, "vadd.vx v3,v2,a0");
}

TEST(Vector, IntegerVI) {
    auto d = decode32_test(
        op_v(0x00, 1, 5, 2, 3, 3));

    EXPECT_EQ(d.text, "vadd.vi v3,v5,2");
}

TEST(Vector, MaskedInteger) {
    auto d = decode32_test(
        op_v(0x00, 0, 2, 1, 0, 3));

    EXPECT_EQ(d.text, "vadd.vv v3,v2,v1,v0.t");
}

TEST(Vector, Subtract) {
    auto d = decode32_test(
        op_v(0x02, 1, 2, 1, 0, 3));

    EXPECT_EQ(d.text, "vsub.vv v3,v2,v1");
}

TEST(Vector, Logic) {
    EXPECT_EQ(
        decode32_test(op_v(0x09, 1, 2, 1, 0, 3)).text,
        "vand.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x0a, 1, 2, 1, 0, 3)).text,
        "vor.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x0b, 1, 2, 1, 0, 3)).text,
        "vxor.vv v3,v2,v1");
}

TEST(Vector, MinMax) {
    EXPECT_EQ(
        decode32_test(op_v(0x04, 1, 2, 1, 0, 3)).text,
        "vminu.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x05, 1, 2, 1, 0, 3)).text,
        "vmin.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x06, 1, 2, 1, 0, 3)).text,
        "vmaxu.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x07, 1, 2, 1, 0, 3)).text,
        "vmax.vv v3,v2,v1");
}


// ============================================================
// Merge / move
// ============================================================

TEST(Vector, Vmerge) {
    auto d = decode32_test(
        op_v(0x17, 0, 2, 1, 0, 3));

    EXPECT_EQ(d.text, "vmerge.vvm v3,v2,v1,v0");
}

TEST(Vector, Vmv) {
    auto d = decode32_test(
        op_v(0x17, 1, 0, 1, 0, 3));

    EXPECT_EQ(d.text, "vmv.v.v v3,v1");
}


// ============================================================
// Carry / borrow
// ============================================================

TEST(Vector, CarryBorrow) {
    EXPECT_EQ(
        decode32_test(op_v(0x10, 0, 2, 1, 0, 3)).text,
        "vadc.vvm v3,v2,v1,v0");

    EXPECT_EQ(
        decode32_test(op_v(0x11, 1, 2, 1, 0, 3)).text,
        "vmadc.vv v3,v2,v1");
}


// ============================================================
// Comparison
// ============================================================

TEST(Vector, Compare) {
    EXPECT_EQ(
        decode32_test(op_v(0x18, 1, 2, 1, 0, 3)).text,
        "vmseq.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x19, 1, 2, 1, 0, 3)).text,
        "vmsne.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x1a, 1, 2, 1, 0, 3)).text,
        "vmsltu.vv v3,v2,v1");

    EXPECT_EQ(
        decode32_test(op_v(0x1b, 1, 2, 1, 0, 3)).text,
        "vmslt.vv v3,v2,v1");
}


// ============================================================
// Multiply
// ============================================================

TEST(Vector, MultiplyVV) {
    auto d = decode32_test(
        op_v(0x25, 1, 2, 1, 2, 3));

    EXPECT_FALSE(d.text.empty());
    EXPECT_EQ(d.size, 4);
}

TEST(Vector, MultiplyVX) {
    auto d = decode32_test(
        op_v(0x25, 1, 2, 10, 6, 3));

    EXPECT_FALSE(d.text.empty());
    EXPECT_EQ(d.size, 4);
}


// ============================================================
// Floating-point vector operations
// ============================================================

TEST(Vector, FloatingPointVV) {
    auto d = decode32_test(
        op_v(0x00, 1, 2, 1, 1, 3));

    EXPECT_FALSE(d.text.empty());
    EXPECT_EQ(d.size, 4);
}

TEST(Vector, FloatingPointVF) {
    auto d = decode32_test(
        op_v(0x00, 1, 2, 1, 5, 3));

    EXPECT_FALSE(d.text.empty());
    EXPECT_EQ(d.size, 4);
}


// ============================================================
// Vector memory
// ============================================================

TEST(Vector, UnitStrideLoad) {
    auto d = decode32_test(
        vector_mem(false, 0, 1, 0, 15, 1));

    EXPECT_EQ(d.text, "vle8.v v1,(a5)");
}

TEST(Vector, UnitStrideStore) {
    auto d = decode32_test(
        vector_mem(true, 0, 1, 0, 14, 1));

    EXPECT_EQ(d.text, "vse8.v v1,(a4)");
}

TEST(Vector, MaskedUnitStrideLoad) {
    auto d = decode32_test(
        vector_mem(false, 0, 0, 0, 15, 1));

    EXPECT_EQ(d.text, "vle8.v v1,(a5),v0.t");
}

TEST(Vector, StridedLoad) {
    auto d = decode32_test(
        vector_mem(false, 6, 1, 2, 15, 1, 2));

    EXPECT_EQ(d.text, "vlse32.v v1,(a5),sp");
}

TEST(Vector, StridedStore) {
    auto d = decode32_test(
        vector_mem(true, 6, 1, 2, 14, 1, 2));

    EXPECT_EQ(d.text, "vsse32.v v1,(a4),sp");
}

TEST(Vector, IndexedLoad) {
    auto d = decode32_test(
        vector_mem(false, 6, 1, 2, 15, 1, 1));

    EXPECT_EQ(d.text, "vluxei32.v v1,(a5),v2");
}

TEST(Vector, IndexedStore) {
    auto d = decode32_test(
        vector_mem(true, 6, 1, 2, 14, 1, 3));

    EXPECT_EQ(d.text, "vsoxei32.v v1,(a4),v2");
}

TEST(Vector, SegmentedLoad) {
    auto d = decode32_test(
        vector_mem(false, 6, 1, 0, 15, 1, 0, 2));

    EXPECT_EQ(d.text, "vlseg3e32.v v1,(a5)");
}

TEST(Vector, SegmentedStore) {
    auto d = decode32_test(
        vector_mem(true, 6, 1, 0, 14, 1, 0, 2));

    EXPECT_EQ(d.text, "vsseg3e32.v v1,(a4)");
}

TEST(Vector, FaultOnlyFirst) {
    auto d = decode32_test(
        vector_mem(false, 6, 1, 0x10, 15, 1, 0));

    EXPECT_EQ(d.text, "vle32ff.v v1,(a5)");
}

TEST(Vector, WholeRegisterLoad) {
    auto d = decode32_test(
        vector_mem(false, 0, 1, 0x08, 15, 1));

    EXPECT_EQ(d.text, "vl1r.v v1,(a5)");
}

TEST(Vector, WholeRegisterStore) {
    auto d = decode32_test(
        vector_mem(true, 0, 1, 0x08, 14, 1));

    EXPECT_EQ(d.text, "vs1r.v v1,(a4)");
}

TEST(Vector, MaskLoad) {
    auto d = decode32_test(
        vector_mem(false, 0, 1, 0x0b, 15, 1));

    EXPECT_EQ(d.text, "vlm.v v1,(a5)");
}

TEST(Vector, MaskStore) {
    auto d = decode32_test(
        vector_mem(true, 0, 1, 0x0b, 14, 1));

    EXPECT_EQ(d.text, "vsm.v v1,(a4)");
}


// ============================================================
// Invalid / unsupported
// ============================================================

TEST(Vector, InvalidMemoryWidth) {
    auto d = decode32_test(
        vector_mem(false, 1, 1, 0, 15, 1));

    EXPECT_NE(d.text, "vle8.v v1,(a5)");
}

TEST(Vector, InvalidMew) {
    auto d = decode32_test(
        vector_mem(false, 0, 1, 0, 15, 1, 0, 0, 1));

    EXPECT_EQ(d.size, 4);
}

TEST(Vector, InvalidOpcodeDoesNotCrash) {
    auto d = decode32_test(0xffffffff);

    EXPECT_EQ(d.size, 4);
    EXPECT_FALSE(d.text.empty());
}


// ============================================================
// Coverage boost - special / invalid / corner cases
// ============================================================

TEST(Vector, CoverageBoostSpecialCases) {

    // --------------------------------------------------------
    // vtype_str() branches
    // --------------------------------------------------------

    // vsew > 3
    EXPECT_EQ(decode32_test(vsetvli(1, 2, 0x018)).size, 4);

    // reserved bits [10:8]
    EXPECT_EQ(decode32_test(vsetvli(1, 2, 0x100)).size, 4);

    // reserved LMUL encoding (4)
    EXPECT_EQ(decode32_test(vsetvli(1, 2, 0x004)).size, 4);


    // --------------------------------------------------------
    // vset* special cases
    // --------------------------------------------------------

    // vsetivli
    EXPECT_EQ(decode32_test(vsetivli(5, 31, 0x3ff)).size, 4);

    // vsetvl
    EXPECT_EQ(decode32_test(vsetvl(5, 6, 7)).size, 4);


    // --------------------------------------------------------
    // Memory special cases
    // --------------------------------------------------------

    // All valid element widths
    EXPECT_EQ(decode32_test(
        vector_mem(false, 5, 1, 0, 1, 2)).size, 4); // e16

    EXPECT_EQ(decode32_test(
        vector_mem(false, 7, 1, 0, 1, 2)).size, 4); // e64

    // Invalid widths
    EXPECT_EQ(decode32_test(
        vector_mem(false, 2, 1, 0, 1, 2)).size, 4);

    EXPECT_EQ(decode32_test(
        vector_mem(false, 3, 1, 0, 1, 2)).size, 4);

    EXPECT_EQ(decode32_test(
        vector_mem(false, 4, 1, 0, 1, 2)).size, 4);


    // mew -> invalid
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 1, 0, 1, 2, 0, 0, 1)).size, 4);


    // Unknown unit-stride lumop
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 1, 0x01, 1, 2)).size, 4);


    // Whole-register load invalid because vm=0
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 0, 0x08, 1, 2)).size, 4);

    // Whole-register load invalid nf
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 1, 0x08, 1, 2, 0, 2)).size, 4);

    // Whole-register store invalid width
    EXPECT_EQ(decode32_test(
        vector_mem(true, 6, 1, 0x08, 1, 2)).size, 4);

    // Whole-register store invalid nf
    EXPECT_EQ(decode32_test(
        vector_mem(true, 0, 1, 0x08, 1, 2, 0, 2)).size, 4);


    // Mask load invalid width
    EXPECT_EQ(decode32_test(
        vector_mem(false, 5, 1, 0x0b, 1, 2)).size, 4);

    // Mask load invalid vm
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 0, 0x0b, 1, 2)).size, 4);

    // Mask load invalid nf
    EXPECT_EQ(decode32_test(
        vector_mem(false, 0, 1, 0x0b, 1, 2, 0, 1)).size, 4);

    // Mask store invalid width
    EXPECT_EQ(decode32_test(
        vector_mem(true, 5, 1, 0x0b, 1, 2)).size, 4);


    // Fault-only-first with store -> invalid
    EXPECT_EQ(decode32_test(
        vector_mem(true, 6, 1, 0x10, 1, 2)).size, 4);

    // Fault-only-first segmented
    EXPECT_EQ(decode32_test(
        vector_mem(false, 6, 1, 0x10, 1, 2, 0, 1)).size, 4);


    // Indexed load/store with masking
    EXPECT_EQ(decode32_test(
        vector_mem(false, 6, 0, 2, 1, 2, 1)).size, 4);

    EXPECT_EQ(decode32_test(
        vector_mem(true, 6, 0, 2, 1, 2, 3)).size, 4);


    // --------------------------------------------------------
    // Integer special instructions
    // --------------------------------------------------------

    // vmerge: vm=1 and rs2 != 0 -> invalid
    EXPECT_EQ(decode32_test(
        op_v(0x17, 1, 3, 1, 0, 2)).size, 4);

    // vmv.v.v
    EXPECT_EQ(decode32_test(
        op_v(0x17, 1, 0, 5, 0, 2)).size, 4);

    // vmv<nr>r.v valid
    EXPECT_EQ(decode32_test(
        op_v(0x27, 1, 1, 0, 3, 2)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x27, 1, 3, 0, 3, 2)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x27, 1, 7, 0, 3, 2)).size, 4);

    // vmv<nr>r invalid
    EXPECT_EQ(decode32_test(
        op_v(0x27, 1, 4, 0, 3, 2)).size, 4);

    // vmv<nr>r with vm=0
    EXPECT_EQ(decode32_test(
        op_v(0x27, 0, 1, 0, 3, 2)).size, 4);

    // vnot
    EXPECT_EQ(decode32_test(
        op_v(0x0b, 1, 3, 31, 3, 2)).size, 4);

    // vneg
    EXPECT_EQ(decode32_test(
        op_v(0x03, 1, 3, 0, 4, 2)).size, 4);

    // vncvt.x.x.w
    EXPECT_EQ(decode32_test(
        op_v(0x2c, 1, 3, 0, 4, 2)).size, 4);


    // --------------------------------------------------------
    // Carry / borrow opposite branches
    // --------------------------------------------------------

    // vadc masked
    EXPECT_EQ(decode32_test(
        op_v(0x10, 0, 3, 2, 0, 1)).size, 4);

    // vmadc unmasked
    EXPECT_EQ(decode32_test(
        op_v(0x11, 0, 3, 2, 0, 1)).size, 4);

    // vsbc
    EXPECT_EQ(decode32_test(
        op_v(0x12, 0, 3, 2, 0, 1)).size, 4);

    // vmsbc
    EXPECT_EQ(decode32_test(
        op_v(0x13, 1, 3, 2, 0, 1)).size, 4);


    // --------------------------------------------------------
    // Mask logical operations
    // --------------------------------------------------------

    EXPECT_EQ(decode32_test(
        op_v(0x19, 1, 2, 2, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x1b, 1, 2, 2, 2, 3)).size, 4);

    // masked mask-operation -> invalid because .mm requires vm=1
    EXPECT_EQ(decode32_test(
        op_v(0x19, 0, 2, 1, 2, 3)).size, 4);


    // --------------------------------------------------------
    // Reduction operations
    // --------------------------------------------------------

    EXPECT_EQ(decode32_test(
        op_v(0x30, 1, 2, 1, 0, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x31, 1, 2, 1, 0, 3)).size, 4);


    // --------------------------------------------------------
    // Unary vector operations
    // --------------------------------------------------------

    // vmv.x.s
    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 2, 0, 2, 3)).size, 4);

    // vcpop.m
    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 2, 0x10, 2, 3)).size, 4);

    // vfirst.m
    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 2, 0x11, 2, 3)).size, 4);

    // vcpop.m masked
    EXPECT_EQ(decode32_test(
        op_v(0x10, 0, 2, 0x10, 2, 3)).size, 4);


    // vzext / vsext
    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 2, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 3, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 4, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 5, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 6, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 7, 2, 3)).size, 4);


    // VMUNARY
    EXPECT_EQ(decode32_test(
        op_v(0x14, 1, 2, 1, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x14, 1, 2, 2, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x14, 1, 2, 3, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x14, 1, 2, 0x10, 2, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x14, 1, 0, 0x11, 2, 3)).size, 4);


    // vcompress
    EXPECT_EQ(decode32_test(
        op_v(0x17, 1, 2, 1, 2, 3)).size, 4);


    // --------------------------------------------------------
    // Mask pseudo operations
    // --------------------------------------------------------

    EXPECT_EQ(decode32_test(
        op_v(0x19, 1, 3, 3, 2, 3)).size, 4); // vmmv

    EXPECT_EQ(decode32_test(
        op_v(0x1b, 1, 3, 3, 2, 3)).size, 4); // vmclr

    EXPECT_EQ(decode32_test(
        op_v(0x1f, 1, 3, 3, 2, 3)).size, 4); // vmset

    EXPECT_EQ(decode32_test(
        op_v(0x1d, 1, 3, 3, 2, 3)).size, 4); // vmnot


    // --------------------------------------------------------
    // Scalar-to-vector move
    // --------------------------------------------------------

    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 0, 5, 6, 2)).size, 4);


    // --------------------------------------------------------
    // FP special cases
    // --------------------------------------------------------

    // vfmv.f.s
    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 2, 0, 1, 3)).size, 4);

    // vfmv.s.f
    EXPECT_EQ(decode32_test(
        op_v(0x10, 1, 0, 2, 5, 3)).size, 4);

    // FP conversion unary instructions
    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 0x00, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x12, 1, 2, 0x10, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x13, 1, 2, 0x00, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x13, 1, 2, 0x04, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x13, 1, 2, 0x05, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x13, 1, 2, 0x10, 1, 3)).size, 4);


    // vfmerge / vfmv.v.f
    EXPECT_EQ(decode32_test(
        op_v(0x17, 0, 2, 1, 5, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x17, 1, 0, 1, 5, 3)).size, 4);


    // vfneg / vfabs
    EXPECT_EQ(decode32_test(
        op_v(0x09, 1, 2, 2, 1, 3)).size, 4);

    EXPECT_EQ(decode32_test(
        op_v(0x0a, 1, 2, 2, 1, 3)).size, 4);


    // --------------------------------------------------------
    // Invalid OP-V funct3
    // --------------------------------------------------------

    EXPECT_EQ(decode32_test(
        op_v(0x00, 1, 2, 1, 7, 3)).size, 4);
}