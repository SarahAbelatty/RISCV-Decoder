// test_cli.cpp -- end-to-end tests: run the real ./riscv_decoder binary
#include <gtest/gtest.h>
#include <sys/wait.h>
#include "test_helpers.hpp"
#include <sstream>

using namespace t;

#ifndef RISCV_DECODER_BIN
#error "RISCV_DECODER_BIN must be defined by the build system"
#endif

namespace {
struct Result { std::string out; int code; };

// Runs the decoder; stdout and stderr are merged so error messages can be checked too.
Result run_decoder(const std::string &args) {
    std::string cmd = std::string("\"") + RISCV_DECODER_BIN + "\" " + args + " 2>&1";
    FILE *p = popen(cmd.c_str(), "r");
    Result r{"", -1};
    if (!p) return r;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
    int st = pclose(p);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1000 - (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return r;
}
std::vector<uint8_t> nops(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i++) push32(v, 0x00000013); return v; }
bool contains(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
std::vector<std::string> lines(const std::string &s) {
    std::vector<std::string> v; std::istringstream is(s); std::string l;
    while (std::getline(is, l)) v.push_back(l);
    return v;
}
}

// ------------------------------ command line ------------------------------
TEST(Cli, NoArgumentsPrintsUsageAndFails) {
    Result r = run_decoder("");
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "usage"));
}
TEST(Cli, TooManyArgumentsPrintsUsageAndFails) {
    Result r = run_decoder("a b");
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "usage"));
}
TEST(Cli, MissingFileReportsErrorAndFails) {
    Result r = run_decoder("/no/such/file.elf");
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "error: cannot open file"));
}
TEST(Cli, NonElfFileReportsBadMagic) {
    TempFile f(std::vector<uint8_t>(64, 'A'));
    Result r = run_decoder(f.path());
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "bad magic"));
}
TEST(Cli, NonRiscvElfIsRejected) {
    ElfBuilder b; b.machine = 62; b.text(nops(1));
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "RISC-V"));
}
TEST(Cli, CorruptElfGivesCleanErrorNotACrash) {
    ElfBuilder b; b.text(nops(4));
    auto img = b.build();
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    put<uint64_t>(img, shoff + sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_size), 0x10000000);   // .text far too big
    TempFile f(img);
    Result r = run_decoder(f.path());
    EXPECT_EQ(r.code, 1) << r.out.substr(0, 200);               // -1000-11 would mean SIGSEGV
    EXPECT_TRUE(contains(r.out, "error:"));
}

// ------------------------------ output format ------------------------------
TEST(Cli, Elf64OutputLayout) {
    ElfBuilder b; b.entry = 0x10000;
    std::vector<uint8_t> code; push32(code, 0x00000513); push16(code, 0x8082);   // li a0,0 ; ret
    b.text(code, 0x10000);
    b.syms = {{"_start", 0x10000, 1}};
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, f.path() + ":\tfile format elf64-littleriscv\n"));
    EXPECT_TRUE(contains(r.out, "entry point: 0x10000\n"));
    EXPECT_TRUE(contains(r.out, "Disassembly of section .text:\n"));
    EXPECT_TRUE(contains(r.out, "\n10000 <_start>:\n"));
    EXPECT_TRUE(contains(r.out, "    000000010000:\t00000513\tli a0,0\n"));      // 12-digit address, 8-digit raw
    EXPECT_TRUE(contains(r.out, "    000000010004:\t8082\tret\n"));              // 4-digit raw for 16-bit
}

TEST(Cli, Elf32OutputUsesEightDigitAddresses) {
    ElfBuilder b; b.is64 = false; b.entry = 0x8000;
    b.text(nops(1), 0x8000);
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "file format elf32-littleriscv"));
    EXPECT_TRUE(contains(r.out, "    00008000:\t00000013\tnop\n"));
}

TEST(Cli, EveryExecutableSectionGetsItsOwnHeader) {
    ElfBuilder b;
    b.text(nops(1), 0x10000, ".init").text(nops(1), 0x10100, ".text");
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "Disassembly of section .init:"));
    EXPECT_TRUE(contains(r.out, "Disassembly of section .text:"));
    EXPECT_LT(r.out.find(".init:"), r.out.find(".text:"));
}

TEST(Cli, FileWithNoCodePrintsMessageAndSucceeds) {
    ElfBuilder b; b.section(".data", SHT_PROGBITS, 0x3, 0x30000, {1, 2, 3, 4});
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "No executable"));
}

TEST(Cli, LabelIsPrintedOnlyAtTheExactSymbolAddress) {
    ElfBuilder b; b.text(nops(3), 0x10000);
    b.syms = {{"middle", 0x10004, 1}};
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    ASSERT_EQ(r.code, 0);
    auto ls = lines(r.out);
    int label_at = -1, insn_at = -1;
    for (size_t i = 0; i < ls.size(); i++) {
        if (ls[i] == "10004 <middle>:") label_at = (int)i;
        if (ls[i].find("000000010004:") != std::string::npos) insn_at = (int)i;
    }
    ASSERT_GE(label_at, 0);
    ASSERT_GE(insn_at, 0);
    EXPECT_EQ(insn_at, label_at + 1);                        // label directly above its instruction
}

TEST(Cli, OddTrailingByteInSectionDoesNotCrash) {
    std::vector<uint8_t> code; push32(code, 0x00000013); code.push_back(0x13);   // 1 stray byte
    ElfBuilder b; b.text(code, 0x10000);
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "truncated"));
}

TEST(Cli, InstructionMixWithBranchTargetsPrintsAbsoluteAddresses) {
    std::vector<uint8_t> code;
    push32(code, enc_b(0b000, 10, 11, 8));    // beq a0,a1,.+8   @0x10000 -> 0x10008
    push32(code, enc_j(0, -4));               // j .-4           @0x10004 -> 0x10000
    push32(code, 0x00008067);                 // ret             @0x10008
    ElfBuilder b; b.text(code, 0x10000);
    TempFile f(b.build());
    Result r = run_decoder(f.path());
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "beq a0,a1,0x10008"));
    EXPECT_TRUE(contains(r.out, "j 0x10000"));
}

// ------------------------------ sample files ------------------------------
#ifdef TEST_DATA_DIR
TEST(CliGolden, TestElfMatchesGoldenOutput) {
    Result r = run_decoder(std::string("\"") + TEST_DATA_DIR + "/test.elf\"");
    ASSERT_EQ(r.code, 0);
    std::ifstream g(std::string(TEST_DATA_DIR) + "/tests/golden/test_elf.txt");
    ASSERT_TRUE(g) << "golden file missing";
    std::stringstream ss; ss << g.rdbuf();
    // first line contains the (machine specific) input path; compare from line 2
    std::string got = r.out.substr(r.out.find('\n') + 1);
    std::string exp = ss.str(); exp = exp.substr(exp.find('\n') + 1);
    EXPECT_EQ(got, exp);
}

TEST(CliReal, HelloElfDecodesEverythingWithoutCrashing) {
    Result r = run_decoder(std::string("\"") + TEST_DATA_DIR + "/hello.elf\"");
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "Disassembly of section .text:"));
    EXPECT_TRUE(contains(r.out, "<main>:"));
    EXPECT_GT(lines(r.out).size(), 90000u);
}

TEST(CliReal, HelloElfLuiAndAuipcNeverPrintNegativeImmediates) {
    // Regression: `lui a5,0xfffff` used to print as `lui a5,-0x1`.
    Result r = run_decoder(std::string("\"") + TEST_DATA_DIR + "/hello.elf\"");
    ASSERT_EQ(r.code, 0);
    int bad = 0;
    for (auto &l : lines(r.out))
        if ((l.find("\tlui ") != std::string::npos || l.find("\tauipc ") != std::string::npos) &&
            l.find(",-0x") != std::string::npos) bad++;
    EXPECT_EQ(bad, 0);
}

TEST(CliReal, HelloElfEntryPointIsLabelled) {
    Result r = run_decoder(std::string("\"") + TEST_DATA_DIR + "/hello.elf\"");
    ASSERT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "entry point: 0x103d8"));
    EXPECT_TRUE(contains(r.out, "103d8 <_start>:"));
}
#endif
