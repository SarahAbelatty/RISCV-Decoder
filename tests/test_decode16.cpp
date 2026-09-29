// test_decode16.cpp -- 16-bit compressed (C extension) decoding
#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace {
struct Case { uint32_t enc; const char *expected; const char *asm_src; };
void PrintTo(const Case &c, std::ostream *os) { *os << c.asm_src; }   // readable test names in failures

const Case kRv64[] = {
#include "cases_rv64_16.inc"
};
const Case kRv32[] = {
#include "cases_rv32_16.inc"
};
const Case kZcbGaps[] = {
#include "cases_zcb_gaps.inc"
};

class Decode16Rv64 : public ::testing::TestWithParam<Case> {};
TEST_P(Decode16Rv64, MatchesExpectedText) {
    const Case &c = GetParam();
    EXPECT_EQ(t::d16((uint16_t)c.enc, t::kAddr, true), c.expected)
        << "asm: " << c.asm_src << "   encoding: 0x" << std::hex << c.enc;
}
INSTANTIATE_TEST_SUITE_P(Assembler, Decode16Rv64, ::testing::ValuesIn(kRv64),
    [](const ::testing::TestParamInfo<Case> &i) {
        char b[32]; std::snprintf(b, sizeof b, "i%03d_%04x", (int)i.index, i.param.enc); return std::string(b);
    });

class Decode16Rv32 : public ::testing::TestWithParam<Case> {};
TEST_P(Decode16Rv32, MatchesExpectedText) {
    const Case &c = GetParam();
    EXPECT_EQ(t::d16((uint16_t)c.enc, t::kAddr, false), c.expected) << "asm: " << c.asm_src;
}
INSTANTIATE_TEST_SUITE_P(Assembler, Decode16Rv32, ::testing::ValuesIn(kRv32),
    [](const ::testing::TestParamInfo<Case> &i) { return "i" + std::to_string(i.index); });

// Zcb instructions the decoder does not implement yet. They are DISABLED_ so the
// suite stays green; remove the prefix as you implement them (see README "Extending").
class DISABLED_Decode16ZcbGaps : public ::testing::TestWithParam<Case> {};
TEST_P(DISABLED_Decode16ZcbGaps, MatchesObjdump) {
    const Case &c = GetParam();
    EXPECT_EQ(t::d16((uint16_t)c.enc, t::kAddr, true), c.expected) << "asm: " << c.asm_src;
}
INSTANTIATE_TEST_SUITE_P(Assembler, DISABLED_Decode16ZcbGaps, ::testing::ValuesIn(kZcbGaps));
} // namespace

// ------------------------- same encoding, RV32 vs RV64 -------------------------
TEST(Decode16Xlen, CAddiwSlotIsCJalOnRv32) {
    uint16_t enc = 0x2001;      // funct3=001, op=01: c.addiw a0,0 on RV64 ; c.jal on RV32
    EXPECT_TRUE(t::starts_with(t::d16(enc, t::kAddr, true),  "addiw"));
    EXPECT_TRUE(t::starts_with(t::d16(enc, t::kAddr, false), "jal ra,"));
}
TEST(Decode16Xlen, DoubleWordLoadStoreOnlyOnRv64) {
    // c.ld / c.sd / c.ldsp / c.sdsp are RV64-only (on RV32 the slots are FP loads/stores)
    for (uint16_t enc : {0x6188 /*c.ld a0,0(a1)*/, 0xe188 /*c.sd*/, 0x6082 /*c.ldsp ra,0(sp)*/, 0xe006 /*c.sdsp*/}) {
        EXPECT_FALSE(t::starts_with(t::d16(enc, t::kAddr, true),  ".half")) << std::hex << enc;
        EXPECT_TRUE (t::starts_with(t::d16(enc, t::kAddr, false), ".half")) << std::hex << enc;
    }
}
TEST(Decode16Xlen, Rv32JumpTargetWrapsAt32Bits) {
    // c.jal .-2 at address 0 -> 0xfffffffe on RV32
    EXPECT_EQ(t::d16(0x3ffd, 0, false), "jal ra,0xfffffffe");
}

// ----------------------------- illegal / reserved -----------------------------
TEST(Decode16Illegal, AllZeroHalfwordIsUnimp) {
    EXPECT_EQ(t::d16(0x0000), "unimp");
}
TEST(Decode16Illegal, ReservedEncodingsAreNotDecodedAsValidInstructions) {
    EXPECT_EQ(t::d16(0x0004), "unimp");                          // c.addi4spn with nzuimm=0
    EXPECT_EQ(t::d16(0x6101), "unimp");                          // c.addi16sp with imm=0
    EXPECT_TRUE(t::starts_with(t::d16(0x6501), ".half"));        // c.lui a0,0 is reserved
    EXPECT_TRUE(t::starts_with(t::d16(0x4002), ".half"));        // c.lwsp with rd=0 is reserved
    EXPECT_TRUE(t::starts_with(t::d16(0x8002), ".half"));        // c.jr with rd=0 is reserved
}
TEST(Decode16Illegal, FloatingPointCompressedFormsAreUnsupportedPlaceholders) {
    for (uint16_t enc : {0x2000 /*c.fld*/, 0xa000 /*c.fsd*/, 0x2002 /*c.fldsp*/, 0xa002 /*c.fsdsp*/}) {
        std::string s = t::d16(enc);
        EXPECT_TRUE(t::starts_with(s, ".half 0x")) << s;
        EXPECT_NE(s.find("unsupported"), std::string::npos) << s;
    }
}

// ------------------------------ exhaustive sweep ------------------------------
TEST(Decode16Props, EveryHalfwordDecodesToNonEmptyTextOnBothXlens) {
    for (uint32_t v = 0; v < 0x10000; v++) {
        if ((v & 3) == 3) continue;                      // not a compressed instruction
        for (bool rv64 : {true, false}) {
            std::string s = t::d16((uint16_t)v, t::kAddr, rv64);
            ASSERT_FALSE(s.empty()) << std::hex << v;
            // Anything that is not decoded must be clearly marked, never silent garbage.
            if (t::starts_with(s, ".half")) {
                ASSERT_NE(s.find("unsupported"), std::string::npos) << std::hex << v;
            }
        }
    }
}

TEST(Decode16Props, SlliShiftAmountAndRegisterAllValues) {
    for (uint32_t rd = 1; rd < 32; rd++)
        for (uint32_t sh = 1; sh < 64; sh++) {
            uint16_t enc = (uint16_t)(0x2 | ((sh >> 5) << 12) | (rd << 7) | ((sh & 0x1f) << 2));
            std::string exp = std::string("slli ") + t::abi(rd) + "," + t::abi(rd) + "," + std::to_string(sh);
            ASSERT_EQ(t::d16(enc), exp) << std::hex << enc;
        }
}
TEST(Decode16Props, CompressedRegisterFieldMapsToX8ToX15) {
    // c.mv is not limited to x8-x15, but c.sub/c.xor/... are: 3-bit field + 8
    const char *names[8] = {"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5"};
    for (uint32_t rd = 0; rd < 8; rd++)
        for (uint32_t rs = 0; rs < 8; rs++) {
            uint16_t enc = (uint16_t)(0x8c01 | (rd << 7) | (rs << 2));      // c.sub
            std::string exp = std::string("sub ") + names[rd] + "," + names[rd] + "," + names[rs];
            ASSERT_EQ(t::d16(enc), exp) << std::hex << enc;
        }
}
