// test_elf_reader.cpp -- ElfFile::load() on hand-built and real ELF files
#include <gtest/gtest.h>
#include "test_helpers.hpp"

using namespace t;

namespace {
std::vector<uint8_t> nops(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i++) push32(v, 0x00000013); return v; }
}

// ------------------------------- valid files -------------------------------
TEST(ElfReader, Elf64BasicTextSection) {
    ElfBuilder b; b.text(nops(4), 0x10000); b.entry = 0x10004;
    ElfFile e = load_image(b.build());
    EXPECT_TRUE(e.is64);
    EXPECT_EQ(e.entry, 0x10004u);
    ASSERT_EQ(e.exec_sections.size(), 1u);
    EXPECT_EQ(e.exec_sections[0].name, ".text");
    EXPECT_EQ(e.exec_sections[0].addr, 0x10000u);
    EXPECT_EQ(e.exec_sections[0].size, 16u);
    EXPECT_TRUE(e.exec_sections[0].execinstr);
    // the bytes at sec.offset are the code we put in
    EXPECT_EQ(std::memcmp(e.raw.data() + e.exec_sections[0].offset, nops(4).data(), 16), 0);
}

TEST(ElfReader, Elf32BasicTextSection) {
    ElfBuilder b; b.is64 = false; b.text(nops(2), 0x8000); b.entry = 0x8000;
    ElfFile e = load_image(b.build());
    EXPECT_FALSE(e.is64);
    EXPECT_EQ(e.entry, 0x8000u);
    ASSERT_EQ(e.exec_sections.size(), 1u);
    EXPECT_EQ(e.exec_sections[0].name, ".text");
    EXPECT_EQ(e.exec_sections[0].addr, 0x8000u);
    EXPECT_EQ(e.exec_sections[0].size, 8u);
}

TEST(ElfReader, OnlyExecutableSectionsAreReturned) {
    ElfBuilder b;
    b.section(".rodata", SHT_PROGBITS, 0x2, 0x20000, {1, 2, 3, 4});     // ALLOC
    b.section(".data",   SHT_PROGBITS, 0x3, 0x30000, {5, 6, 7, 8});     // ALLOC|WRITE
    b.text(nops(1), 0x10000);
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.exec_sections.size(), 1u);
    EXPECT_EQ(e.exec_sections[0].name, ".text");
}

TEST(ElfReader, MultipleExecutableSectionsKeepFileOrder) {
    ElfBuilder b;
    b.text(nops(1), 0x10000, ".init").text(nops(2), 0x10100, ".text").text(nops(1), 0x10200, ".fini");
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.exec_sections.size(), 3u);
    EXPECT_EQ(e.exec_sections[0].name, ".init");
    EXPECT_EQ(e.exec_sections[1].name, ".text");
    EXPECT_EQ(e.exec_sections[2].name, ".fini");
}

TEST(ElfReader, ExecutableNobitsSectionIsSkipped) {
    ElfBuilder b;
    b.section(".weird", SHT_NOBITS, 0x6, 0x40000, std::vector<uint8_t>(16));
    b.text(nops(1), 0x10000);
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.exec_sections.size(), 1u);
    EXPECT_EQ(e.exec_sections[0].name, ".text");
}

TEST(ElfReader, FileWithNoExecutableSectionLoadsWithEmptyList) {
    ElfBuilder b; b.section(".data", SHT_PROGBITS, 0x3, 0x30000, {1, 2, 3, 4});
    ElfFile e = load_image(b.build());
    EXPECT_TRUE(e.exec_sections.empty());
}

// --------------------------------- symbols ---------------------------------
TEST(ElfReader, SymbolsAreReadFromSymtab64) {
    ElfBuilder b; b.text(nops(4), 0x10000);
    b.syms = {{"_start", 0x10000, 1}, {"main", 0x10008, 1}};
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.symbols.size(), 2u);
    EXPECT_EQ(e.symbols[0].name, "_start"); EXPECT_EQ(e.symbols[0].value, 0x10000u); EXPECT_EQ(e.symbols[0].shndx, 1);
    EXPECT_EQ(e.symbols[1].name, "main");   EXPECT_EQ(e.symbols[1].value, 0x10008u);
}

TEST(ElfReader, SymbolsAreReadFromSymtab32) {
    ElfBuilder b; b.is64 = false; b.text(nops(4), 0x8000);
    b.syms = {{"reset", 0x8000, 1}, {"loop", 0x8004, 1}};
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.symbols.size(), 2u);
    EXPECT_EQ(e.symbols[0].name, "reset");
    EXPECT_EQ(e.symbols[1].value, 0x8004u);
}

TEST(ElfReader, NullSymbolAndEmptyNamesAreSkipped) {
    ElfBuilder b; b.text(nops(1));
    b.syms = {{"", 0x10000, 1}, {"named", 0x10000, 1}};
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.symbols.size(), 1u);
    EXPECT_EQ(e.symbols[0].name, "named");
}

TEST(ElfReader, FallsBackToDynsymWhenThereIsNoSymtab) {
    ElfBuilder b; b.text(nops(1));
    b.dynsyms = {{"dyn_fn", 0x10000, 1}};
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.symbols.size(), 1u);
    EXPECT_EQ(e.symbols[0].name, "dyn_fn");
}

TEST(ElfReader, SymtabIsPreferredOverDynsym) {
    ElfBuilder b; b.text(nops(1));
    b.dynsyms = {{"from_dynsym", 0x10000, 1}};
    b.syms    = {{"from_symtab", 0x10000, 1}};
    ElfFile e = load_image(b.build());
    ASSERT_EQ(e.symbols.size(), 1u);
    EXPECT_EQ(e.symbols[0].name, "from_symtab");
}

TEST(ElfReader, StrippedFileHasNoSymbols) {
    ElfBuilder b; b.text(nops(1));
    ElfFile e = load_image(b.build());
    EXPECT_TRUE(e.symbols.empty());
}

// ------------------------------ real sample files ------------------------------
#ifdef TEST_DATA_DIR
TEST(ElfReaderReal, TestElfFromMakeScript) {
    ElfFile e; e.load(std::string(TEST_DATA_DIR) + "/test.elf");
    EXPECT_TRUE(e.is64);
    EXPECT_EQ(e.entry, 0x10000u);
    ASSERT_EQ(e.exec_sections.size(), 1u);
    EXPECT_EQ(e.exec_sections[0].name, ".text");
    EXPECT_EQ(e.exec_sections[0].addr, 0x10000u);
    EXPECT_EQ(e.exec_sections[0].size, 42u);
}

TEST(ElfReaderReal, HelloElfStaticLinuxBinary) {
    ElfFile e; e.load(std::string(TEST_DATA_DIR) + "/hello.elf");
    EXPECT_TRUE(e.is64);
    EXPECT_EQ(e.entry, 0x103d8u);
    EXPECT_GE(e.exec_sections.size(), 2u);                 // .plt + .text (+ maybe .init/.fini)
    bool has_text = false;
    for (auto &s : e.exec_sections) has_text |= (s.name == ".text");
    EXPECT_TRUE(has_text);
    EXPECT_GT(e.symbols.size(), 100u);                      // static glibc: not stripped
    bool has_main = false;
    for (auto &s : e.symbols) has_main |= (s.name == "main");
    EXPECT_TRUE(has_main);
}
#endif

// ------------------------------ rejected inputs ------------------------------
TEST(ElfReaderErrors, MissingFileThrows) {
    ElfFile e;
    EXPECT_THROW(e.load("/definitely/not/here.elf"), std::runtime_error);
}

TEST(ElfReaderErrors, EmptyAndTinyFilesThrow) {
    for (size_t n : {0u, 1u, 4u, 15u}) {
        TempFile f(std::vector<uint8_t>(n, 0x7f));
        ElfFile e;
        EXPECT_THROW(e.load(f.path()), std::runtime_error) << "size " << n;
    }
}

TEST(ElfReaderErrors, BadMagicThrowsWithHelpfulMessage) {
    auto img = ElfBuilder().text(nops(1)).build();
    img[1] = 'X';
    TempFile f(img); ElfFile e;
    try { e.load(f.path()); FAIL() << "expected exception"; }
    catch (const std::runtime_error &ex) { EXPECT_NE(std::string(ex.what()).find("magic"), std::string::npos); }
}

TEST(ElfReaderErrors, UnknownElfClassThrows) {
    ElfBuilder b; b.text(nops(1)); b.ei_class = 3;
    TempFile f(b.build()); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfReaderErrors, BigEndianFileIsRejected) {
    ElfBuilder b; b.text(nops(1)); b.ei_data = ELFDATA2MSB;
    TempFile f(b.build()); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfReaderErrors, NonRiscvMachineIsRejectedBothClasses) {
    for (bool is64 : {true, false}) {
        ElfBuilder b; b.is64 = is64; b.machine = 62 /*x86-64*/; b.text(nops(1));
        TempFile f(b.build()); ElfFile e;
        try { e.load(f.path()); FAIL() << "expected exception"; }
        catch (const std::runtime_error &ex) { EXPECT_NE(std::string(ex.what()).find("RISC"), std::string::npos); }
    }
}

TEST(ElfReaderErrors, FileWithoutSectionHeadersThrows) {
    auto img = ElfBuilder().text(nops(1)).build();
    put<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff), 0);
    put<uint16_t>(img, offsetof(Elf64_Ehdr, e_shnum), 0);
    put<uint16_t>(img, offsetof(Elf64_Ehdr, e_shstrndx), 0);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfReaderErrors, BadSectionNameTableIndexThrows) {
    auto img = ElfBuilder().text(nops(1)).build();
    put<uint16_t>(img, offsetof(Elf64_Ehdr, e_shstrndx), 200);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfReaderErrors, SectionHeaderTableCutOffThrows) {
    auto img = ElfBuilder().text(nops(1)).build();
    img.resize(img.size() - 40);                     // chop the tail of the header table
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

// ---------------- robustness: corrupt files must throw, never crash ----------------
// These check the reader against malformed input. Each one must end in a
// std::runtime_error (a clean "error: ..." message from main), not a segfault.
TEST(ElfRobustness, SectionHeaderOffsetNearUint64MaxDoesNotWrapAround) {
    auto img = ElfBuilder().text(nops(1)).build();
    put<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff), 0xFFFFFFFFFFFFFFF0ull);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, ExecutableSectionExtendingPastEndOfFileThrows) {
    ElfBuilder b; b.text(nops(4));
    auto img = b.build();
    // section header #1 is .text; make its size far bigger than the file
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    put<uint64_t>(img, shoff + sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_size), 0x100000);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, ExecutableSectionOffsetPastEndOfFileThrows) {
    ElfBuilder b; b.text(nops(4));
    auto img = b.build();
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    put<uint64_t>(img, shoff + sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_offset), 0xFFFFFFFFFFFFFF00ull);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, SectionNameTableOutsideFileThrows) {
    ElfBuilder b; b.text(nops(1));
    auto img = b.build();
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    uint16_t shstrndx = get<uint16_t>(img, offsetof(Elf64_Ehdr, e_shstrndx));
    put<uint64_t>(img, shoff + shstrndx * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_offset), 0x7fffffffull);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, SymtabLinkPointingToNonexistentSectionThrows) {
    ElfBuilder b; b.text(nops(1)); b.syms = {{"x", 0x10000, 1}};
    auto img = b.build();
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    uint16_t n = get<uint16_t>(img, offsetof(Elf64_Ehdr, e_shnum));
    for (uint16_t i = 0; i < n; i++)                          // find .symtab (type 2)
        if (get<uint32_t>(img, shoff + i * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_type)) == 2)
            put<uint32_t>(img, shoff + i * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_link), 999);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, SymbolStringOffsetPastStringTableGivesEmptyNameNotCrash) {
    ElfBuilder b; b.text(nops(1)); b.syms = {{"abc", 0x10000, 1}};
    auto img = b.build();
    size_t shoff = get<uint64_t>(img, offsetof(Elf64_Ehdr, e_shoff));
    uint16_t n = get<uint16_t>(img, offsetof(Elf64_Ehdr, e_shnum));
    for (uint16_t i = 0; i < n; i++)
        if (get<uint32_t>(img, shoff + i * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_type)) == 2) {
            size_t symoff = get<uint64_t>(img, shoff + i * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_offset));
            put<uint32_t>(img, symoff + sizeof(Elf64_Sym) + offsetof(Elf64_Sym, st_name), 0x00ffffff);
        }
    TempFile f(img); ElfFile e;
    EXPECT_NO_THROW(e.load(f.path()));
    EXPECT_TRUE(e.symbols.empty());                          // empty name => skipped
}

TEST(ElfRobustness, SectionHeaderEntrySizeSmallerThanStructThrows) {
    auto img = ElfBuilder().text(nops(1)).build();
    put<uint16_t>(img, offsetof(Elf64_Ehdr, e_shentsize), 8);
    TempFile f(img); ElfFile e;
    EXPECT_THROW(e.load(f.path()), std::runtime_error);
}

TEST(ElfRobustness, TruncatedAtEveryLengthNeverCrashes) {
    // Cut a valid file at every possible length: each attempt must either load or throw.
    ElfBuilder b; b.text(nops(4)); b.syms = {{"_start", 0x10000, 1}};
    auto img = b.build();
    for (size_t len = 0; len < img.size(); len++) {
        std::vector<uint8_t> cut(img.begin(), img.begin() + len);
        TempFile f(cut); ElfFile e;
        try { e.load(f.path()); } catch (const std::runtime_error &) {}
    }
    SUCCEED();
}

TEST(ElfRobustness, RandomByteFlipsNeverCrash) {
    ElfBuilder b; b.text(nops(4)); b.syms = {{"_start", 0x10000, 1}, {"end", 0x10010, 1}};
    auto base = b.build();
    uint64_t x = 0x243f6a8885a308d3ull;
    for (int i = 0; i < 400; i++) {
        auto img = base;
        for (int k = 0; k < 3; k++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            img[16 + x % (img.size() - 16)] = (uint8_t)(x >> 32);   // keep e_ident intact
        }
        TempFile f(img); ElfFile e;
        try { e.load(f.path()); } catch (const std::exception &) {}
    }
    SUCCEED();
}
