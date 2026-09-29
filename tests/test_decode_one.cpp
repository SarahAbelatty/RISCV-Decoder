// test_decode_one.cpp -- instruction-length detection, byte order, truncation
#include <gtest/gtest.h>
#include "test_helpers.hpp"

using riscv::decode_one;

TEST(DecodeOne, ThirtyTwoBitInstructionIsLittleEndianAndSizeFour) {
    const unsigned char b[] = {0x13, 0x05, 0x00, 0x00};                       // addi a0,zero,0
    auto d = decode_one(b, sizeof b, 0x10000, true);
    EXPECT_EQ(d.size, 4);
    EXPECT_EQ(d.raw, 0x00000513u);
    EXPECT_EQ(d.text, "li a0,0");
}

TEST(DecodeOne, CompressedInstructionIsSizeTwo) {
    const unsigned char b[] = {0x82, 0x80, 0xff, 0xff};                       // c.ret then junk
    auto d = decode_one(b, sizeof b, 0x10000, true);
    EXPECT_EQ(d.size, 2);
    EXPECT_EQ(d.raw, 0x8082u);
    EXPECT_EQ(d.text, "ret");
}

TEST(DecodeOne, LengthIsDecidedByTheLowTwoBits) {
    // 00,01,10 => 16-bit ; 11 => 32-bit
    for (unsigned lo = 0; lo < 4; lo++) {
        const unsigned char b[] = {(unsigned char)(0x10 | lo), 0x00, 0x00, 0x00};
        auto d = decode_one(b, 4, 0, true);
        EXPECT_EQ(d.size, lo == 3 ? 4 : 2) << "low bits " << lo;
    }
}

TEST(DecodeOne, ThirtyTwoBitInstructionCutShortIsReportedTruncated) {
    const unsigned char b[] = {0x13, 0x05, 0x00};                             // 3 of 4 bytes
    auto d = decode_one(b, 3, 0x10000, true);
    EXPECT_EQ(d.text, ".byte (truncated)");
    EXPECT_LE(d.size, 3);
    EXPECT_GT(d.size, 0);
}

TEST(DecodeOne, SingleTrailingByteNeverClaimsMoreThanIsAvailable) {
    // 1 byte left in the section. Whatever its bits are, decode_one must not
    // report size > avail, otherwise the caller walks past the end of the section.
    for (unsigned v = 0; v < 256; v++) {
        const unsigned char b[] = {(unsigned char)v};
        auto d = decode_one(b, 1, 0x10000, true);
        ASSERT_GE(d.size, 1) << "byte " << v;
        ASSERT_LE(d.size, 1) << "byte " << v << " -> " << d.text;
    }
}

TEST(DecodeOne, MixedStreamIsWalkedToTheExactEnd) {
    // Same bytes as make_test_elf.py
    std::vector<uint8_t> code = {
        0x13,0x05,0x00,0x00, 0x93,0x05,0x10,0x00, 0x33,0x86,0xb5,0x00, 0x01,0x45, 0x85,0x05,
        0x93,0x87,0x07,0x00, 0x63,0x0c,0xc7,0x00, 0x13,0x01,0x01,0xff, 0x23,0x34,0xa1,0x00,
        0x82,0x80, 0x67,0x80,0x00,0x00, 0x6f,0x00,0x00,0x00};
    std::vector<int> sizes;
    size_t off = 0;
    while (off < code.size()) {
        auto d = decode_one(code.data() + off, code.size() - off, 0x10000 + off, true);
        ASSERT_GT(d.size, 0);
        sizes.push_back(d.size);
        off += d.size;
    }
    EXPECT_EQ(off, code.size());
    EXPECT_EQ(sizes, (std::vector<int>{4,4,4,2,2,4,4,4,4,2,4,4}));
}

TEST(DecodeOne, PcRelativeTargetsUseTheGivenAddress) {
    const unsigned char jal_plus8[] = {0x6f, 0x00, 0x80, 0x00};               // j .+8
    EXPECT_EQ(decode_one(jal_plus8, 4, 0x1000, true).text, "j 0x1008");
    EXPECT_EQ(decode_one(jal_plus8, 4, 0x2000, true).text, "j 0x2008");
}

TEST(DecodeOne, RandomBytesNeverCrashAndAlwaysMakeProgress) {
    uint64_t x = 88172645463325252ull;
    std::vector<uint8_t> buf(1 << 16);
    for (auto &b : buf) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b = (uint8_t)x; }
    for (bool rv64 : {true, false}) {
        size_t off = 0;
        while (off < buf.size()) {
            auto d = decode_one(buf.data() + off, buf.size() - off, 0x1000 + off, rv64);
            ASSERT_GE(d.size, 1);
            ASSERT_LE(off + d.size, buf.size());        // never walks past the end
            ASSERT_FALSE(d.text.empty());
            off += d.size;
        }
    }
}
