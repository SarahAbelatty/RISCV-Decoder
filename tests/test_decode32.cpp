// test_decode32.cpp -- 32-bit instruction decoding
#include <gtest/gtest.h>
#include "test_helpers.hpp"

namespace {
struct Case { uint32_t enc; const char *expected; const char *asm_src; };
void PrintTo(const Case &c, std::ostream *os) { *os << c.asm_src; }   // readable test names in failures

// Encodings come from the real assembler (tests/tools/gen_cases.py).
const Case kCases[] = {
#include "cases_rv64_32.inc"
};

class Decode32Table : public ::testing::TestWithParam<Case> {};

TEST_P(Decode32Table, MatchesExpectedText) {
    const Case &c = GetParam();
    EXPECT_EQ(t::d32(c.enc), c.expected)
        << "asm: " << c.asm_src << "   encoding: 0x" << std::hex << c.enc;
}
INSTANTIATE_TEST_SUITE_P(Assembler, Decode32Table, ::testing::ValuesIn(kCases),
    [](const ::testing::TestParamInfo<Case> &i) {
        char b[32]; std::snprintf(b, sizeof b, "i%03d_%08x", (int)i.index, i.param.enc); return std::string(b);
    });
} // namespace

// ---------- exhaustive / property tests (expected values built independently) ----------

TEST(Decode32Props, AllRegisterNamesInRType) {
    for (uint32_t rd = 0; rd < 32; rd++)
        for (uint32_t rs1 = 0; rs1 < 32; rs1++)
            for (uint32_t rs2 = 0; rs2 < 32; rs2++) {
                // xor: no pseudo-instruction form, so it always prints in full
                std::string exp = std::string("xor ") + t::abi(rd) + "," + t::abi(rs1) + "," + t::abi(rs2);
                ASSERT_EQ(t::d32(t::enc_r(0x33, 0b100, 0, rd, rs1, rs2)), exp);
            }
}

TEST(Decode32Props, ITypeImmediateAllValues) {
    for (int32_t imm = -2048; imm <= 2047; imm++) {
        std::string exp = "lw a0," + std::to_string(imm) + "(a1)";
        ASSERT_EQ(t::d32(t::enc_i(0x03, 0b010, 10, 11, imm)), exp) << "imm=" << imm;
        exp = "xori a0,a1," + std::to_string(imm);
        ASSERT_EQ(t::d32(t::enc_i(0x13, 0b100, 10, 11, imm)), exp) << "imm=" << imm;
    }
}

TEST(Decode32Props, STypeImmediateAllValues) {
    for (int32_t imm = -2048; imm <= 2047; imm++) {
        std::string exp = "sw a2," + std::to_string(imm) + "(a1)";
        ASSERT_EQ(t::d32(t::enc_s(0x23, 0b010, 11, 12, imm)), exp) << "imm=" << imm;
    }
}

TEST(Decode32Props, BranchTargetAllOffsets) {
    for (int32_t imm = -4096; imm <= 4094; imm += 2) {
        std::string exp = "bne a0,a1," + t::hex(t::kAddr + imm);
        ASSERT_EQ(t::d32(t::enc_b(0b001, 10, 11, imm)), exp) << "imm=" << imm;
    }
}

TEST(Decode32Props, JalTargetAllOffsets) {
    for (int32_t imm = -(1 << 20); imm < (1 << 20); imm += 2) {
        std::string exp = "jal t0," + t::hex(t::kAddr + imm);
        ASSERT_EQ(t::d32(t::enc_j(5, imm)), exp) << "imm=" << imm;
    }
}

TEST(Decode32Props, LuiAuipcAll20BitImmediates) {
    for (uint32_t imm = 0; imm < (1u << 20); imm += 1) {
        char b[32]; std::snprintf(b, sizeof b, "0x%x", imm);
        ASSERT_EQ(t::d32(t::enc_u(0x37, 10, imm)),  std::string("lui a0,") + b)   << "imm20=" << imm;
        ASSERT_EQ(t::d32(t::enc_u(0x17, 10, imm)),  std::string("auipc a0,") + b) << "imm20=" << imm;
    }
}

// ---------- shifts: RV32 vs RV64 shamt width ----------
TEST(Decode32Shift, Rv32MasksShiftAmountTo5Bits) {
    // slli a0,a1,31 is legal on both
    EXPECT_EQ(t::d32(t::enc_i(0x13, 0b001, 10, 11, 31), t::kAddr, false), "slli a0,a1,31");
    EXPECT_EQ(t::d32(t::enc_i(0x13, 0b001, 10, 11, 31), t::kAddr, true),  "slli a0,a1,31");
    // srai a0,a1,31
    EXPECT_EQ(t::d32(t::enc_i(0x13, 0b101, 10, 11, 0x400 | 31), t::kAddr, false), "srai a0,a1,31");
}

// ---------- RV32: results must stay inside the 32-bit address space ----------
TEST(Decode32Rv32, BackwardJumpWrapsAt32Bits) {
    // j .-4 executed at address 0 on RV32 lands on 0xfffffffc, not 0xfffffffffffffffc
    EXPECT_EQ(t::d32(t::enc_j(0, -4), 0, false), "j 0xfffffffc");
}
TEST(Decode32Rv32, BackwardBranchWrapsAt32Bits) {
    EXPECT_EQ(t::d32(t::enc_b(0b000, 10, 11, -8), 0, false), "beq a0,a1,0xfffffff8");
}

// ---------- A extension: .d forms only exist on RV64 ----------
TEST(Decode32Rv32, AmoDoubleIsNotDecodedOnRv32) {
    uint32_t amoadd_d = t::enc_r(0x2f, 0b011, 0b0000000, 10, 11, 12);   // amoadd.d a0,a2,(a1)
    EXPECT_EQ(t::d32(amoadd_d, t::kAddr, true), "amoadd.d a0,a2,(a1)");
    EXPECT_TRUE(t::starts_with(t::d32(amoadd_d, t::kAddr, false), ".word"));
}

// ---------- invalid / unsupported encodings must never crash ----------
TEST(Decode32Invalid, ReservedFunct3ValuesAreReported) {
    EXPECT_EQ(t::d32(t::enc_i(0x67, 0b001, 1, 2, 0)), "unknown(jalr funct3)");
    EXPECT_EQ(t::d32(t::enc_b(0b010, 1, 2, 8)),       "unknown(branch)");
    EXPECT_EQ(t::d32(t::enc_b(0b011, 1, 2, 8)),       "unknown(branch)");
    EXPECT_EQ(t::d32(t::enc_i(0x03, 0b111, 1, 2, 0)), "unknown(load)");
    EXPECT_EQ(t::d32(t::enc_s(0x23, 0b100, 1, 2, 0)), "unknown(store)");
    EXPECT_EQ(t::d32(t::enc_s(0x23, 0b111, 1, 2, 0)), "unknown(store)");
}

TEST(Decode32Invalid, FloatAndVectorOpcodesBecomeWordPlaceholder) {

    // Unsupported FP encodings
    for (uint32_t w : {
        0x02b57553u,
        0x0009b787u,
        0x09253027u
    }) {
        std::string s = t::d32(w);

        EXPECT_TRUE(t::starts_with(s, ".word 0x")) << s;
        EXPECT_NE(s.find("unsupported"), std::string::npos) << s;
    }
}

TEST(Decode32Invalid, WordPlaceholderPrintsFullEightDigits) {
    std::string s = t::d32(0x0000007b);      // custom-3 opcode, small value
    EXPECT_TRUE(t::starts_with(s, ".word 0x0000007b")) << s;
}

TEST(Decode32Invalid, ReservedSystemEncodingsFallBackToWord) {
    // SYSTEM funct3=0 with an unknown imm12, and reserved funct3=4
    EXPECT_TRUE(t::starts_with(t::d32(t::enc_i(0x73, 0, 0, 0, 0x7ff)), ".word"));
    EXPECT_TRUE(t::starts_with(t::d32(t::enc_i(0x73, 0b100, 1, 2, 0x300)), ".word"));
}

TEST(Decode32Invalid, LrWithNonZeroRs2FallsBackToWord) {
    uint32_t bad_lr = t::enc_r(0x2f, 0b010, 0b00010 << 2, 10, 11, 5);   // rs2 must be 0 for LR
    EXPECT_TRUE(t::starts_with(t::d32(bad_lr), ".word"));
}

TEST(Decode32Invalid, EveryOpcodeWithRandomFieldsNeverCrashes) {
    // deterministic xorshift, no <random> needed
    uint64_t x = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 300000; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        uint32_t w = (uint32_t)x | 0x3;                 // low bits 11 => 32-bit instruction
        for (bool rv64 : {true, false}) {
            std::string s = t::d32(w, (uint64_t)x, rv64);
            ASSERT_FALSE(s.empty()) << std::hex << w;
        }
    }
}
