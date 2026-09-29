// test_utils.cpp -- small helper functions in riscv_disasm.hpp + ELF struct layout
#include <gtest/gtest.h>
#include "test_helpers.hpp"

TEST(Sext, SignExtendsFromGivenWidth) {
    EXPECT_EQ(riscv::sext(0x7ff, 12), 2047);
    EXPECT_EQ(riscv::sext(0x800, 12), -2048);
    EXPECT_EQ(riscv::sext(0xfff, 12), -1);
    EXPECT_EQ(riscv::sext(0x0, 12), 0);
    EXPECT_EQ(riscv::sext(1, 1), -1);
    EXPECT_EQ(riscv::sext(0x1fffff, 21), -1);
    EXPECT_EQ(riscv::sext(0x100000, 21), -(1 << 20));
    EXPECT_EQ(riscv::sext(0x3f, 6), -1);
    EXPECT_EQ(riscv::sext(0x1f, 6), 31);
}

TEST(RegNames, AllThirtyTwoAbiNamesAndOutOfRange) {
    for (int i = 0; i < 32; i++) EXPECT_EQ(riscv::reg(i), t::abi(i)) << i;
    EXPECT_EQ(riscv::reg(-1), "x?");
    EXPECT_EQ(riscv::reg(32), "x?");
}

TEST(RegNames, CompressedRegisterMapping) {
    EXPECT_EQ(riscv::creg(0), 8);
    EXPECT_EQ(riscv::creg(7), 15);
}

TEST(Hexi, FormatsPositiveNegativeAndZero) {
    EXPECT_EQ(riscv::hexi(0), "0x0");
    EXPECT_EQ(riscv::hexi(255), "0xff");
    EXPECT_EQ(riscv::hexi(-255), "-0xff");
    EXPECT_EQ(riscv::hexi(0xfffff), "0xfffff");
}

TEST(CsrName, KnownNamesAndUnknownFallback) {
    struct { uint32_t n; const char *s; } known[] = {
        {0x001,"fflags"},{0x002,"frm"},{0x003,"fcsr"},{0xc00,"cycle"},{0xc01,"time"},{0xc02,"instret"},
        {0xc80,"cycleh"},{0xc81,"timeh"},{0xc82,"instreth"},
        {0x100,"sstatus"},{0x104,"sie"},{0x105,"stvec"},{0x140,"sscratch"},{0x141,"sepc"},
        {0x142,"scause"},{0x143,"stval"},{0x144,"sip"},{0x180,"satp"},
        {0x300,"mstatus"},{0x301,"misa"},{0x304,"mie"},{0x305,"mtvec"},{0x340,"mscratch"},
        {0x341,"mepc"},{0x342,"mcause"},{0x343,"mtval"},{0x344,"mip"},
        {0xf11,"mvendorid"},{0xf12,"marchid"},{0xf13,"mimpid"},{0xf14,"mhartid"},
        {0xc20,"vl"},{0xc21,"vtype"},{0xc22,"vlenb"}};
    for (auto &k : known) EXPECT_EQ(riscv::csr_name(k.n), k.s) << std::hex << k.n;
    EXPECT_EQ(riscv::csr_name(0x7c0), "0x7c0");
    EXPECT_EQ(riscv::csr_name(0xfff), "0xfff");
    EXPECT_EQ(riscv::csr_name(0x000), "0x0");
}

// The reader memcpy()s these structs straight from the file, so their size must match the ELF spec.
TEST(ElfTypes, StructSizesMatchTheElfSpecification) {
    EXPECT_EQ(sizeof(Elf32_Ehdr), 52u);
    EXPECT_EQ(sizeof(Elf64_Ehdr), 64u);
    EXPECT_EQ(sizeof(Elf32_Shdr), 40u);
    EXPECT_EQ(sizeof(Elf64_Shdr), 64u);
    EXPECT_EQ(sizeof(Elf32_Sym), 16u);
    EXPECT_EQ(sizeof(Elf64_Sym), 24u);
}
TEST(ElfTypes, FieldOffsetsMatchTheElfSpecification) {
    EXPECT_EQ(offsetof(Elf64_Ehdr, e_entry), 24u);
    EXPECT_EQ(offsetof(Elf64_Ehdr, e_shoff), 40u);
    EXPECT_EQ(offsetof(Elf64_Ehdr, e_shentsize), 58u);
    EXPECT_EQ(offsetof(Elf64_Ehdr, e_shstrndx), 62u);
    EXPECT_EQ(offsetof(Elf32_Ehdr, e_entry), 24u);
    EXPECT_EQ(offsetof(Elf32_Ehdr, e_shoff), 32u);
    EXPECT_EQ(offsetof(Elf32_Ehdr, e_shstrndx), 50u);
    EXPECT_EQ(offsetof(Elf64_Shdr, sh_addr), 16u);
    EXPECT_EQ(offsetof(Elf64_Shdr, sh_offset), 24u);
    EXPECT_EQ(offsetof(Elf64_Shdr, sh_size), 32u);
    EXPECT_EQ(offsetof(Elf32_Shdr, sh_offset), 16u);
    EXPECT_EQ(offsetof(Elf64_Sym, st_value), 8u);
    EXPECT_EQ(offsetof(Elf32_Sym, st_shndx), 14u);
}
