// elf_types.hpp
// Minimal ELF32 / ELF64 structure definitions, taken from the ELF
// specification (Portable Formats Specification, "elf.pdf").
// Only the fields needed to locate executable sections are included.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---- e_ident indices ----
enum {
    EI_MAG0 = 0, EI_MAG1, EI_MAG2, EI_MAG3,
    EI_CLASS, EI_DATA, EI_VERSION, EI_OSABI, EI_ABIVERSION,
    EI_PAD, EI_NIDENT = 16
};

enum { ELFCLASSNONE = 0, ELFCLASS32 = 1, ELFCLASS64 = 2 };
enum { ELFDATANONE = 0, ELFDATA2LSB = 1, ELFDATA2MSB = 2 };

// ---- e_machine ----
static const uint16_t EM_RISCV = 243;

// ---- section header types / flags we care about ----
static const uint32_t SHT_NULL     = 0;
static const uint32_t SHT_PROGBITS = 1;
static const uint32_t SHT_NOBITS   = 8;
static const uint64_t SHF_EXECINSTR = 0x4;

#pragma pack(push, 1)

struct Elf32_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

// Symbol table entries (used so we can label addresses, e.g. <main>:)
struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
};

struct Elf64_Sym {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

#pragma pack(pop)

// A machine-independent view of a section we found, filled in by the
// ELF32/ELF64 reader so the rest of the program doesn't care which
// class the file was.
struct SectionView {
    std::string name;
    uint64_t addr;      // virtual address
    uint64_t offset;    // file offset
    uint64_t size;      // size in bytes
    bool execinstr;
};

struct SymbolView {
    std::string name;
    uint64_t value;
    uint16_t shndx;
};
