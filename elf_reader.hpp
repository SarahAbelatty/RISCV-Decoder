// elf_reader.hpp
// Reads a real ELF file from disk and extracts:
//   - class (32/64 bit), entry point
//   - every section with SHF_EXECINSTR set (i.e. actual code: .text,
//     .init, .fini, .plt, ...)
//   - the symbol table (.symtab or .dynsym), so we can label addresses
#pragma once
#include "elf_types.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>

class ElfFile {
public:
    bool is64 = false;
    bool little_endian = true;
    uint64_t entry = 0;
    std::vector<SectionView> exec_sections;
    std::vector<SymbolView> symbols;
    std::vector<unsigned char> raw; // whole file, for pulling bytes by offset

    void load(const std::string &path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("cannot open file: " + path);
        std::streamsize size = f.tellg();
        if (size < (std::streamsize)EI_NIDENT)
            throw std::runtime_error("file too small to be an ELF file");
        f.seekg(0);
        raw.resize((size_t)size);
        if (!f.read((char*)raw.data(), size))
            throw std::runtime_error("failed reading file");

        if (!(raw[0] == 0x7f && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F'))
            throw std::runtime_error("not an ELF file (bad magic)");

        int eclass = raw[EI_CLASS];
        int edata  = raw[EI_DATA];
        if (eclass != ELFCLASS32 && eclass != ELFCLASS64)
            throw std::runtime_error("unsupported ELF class");
        if (edata != ELFDATA2LSB)
            throw std::runtime_error("only little-endian ELF files are supported "
                                      "(RISC-V is virtually always little-endian)");

        is64 = (eclass == ELFCLASS64);
        little_endian = true;

        if (is64) parse64();
        else      parse32();
    }

private:
    // Throws unless [off, off+len) lies inside the file. Written so it cannot overflow.
    void check_range(uint64_t off, uint64_t len, const char *what) const {
        if (off > raw.size() || len > raw.size() - off)
            throw std::runtime_error(std::string("truncated/corrupt ELF file (") + what +
                                     " lies outside the file)");
    }

    template <typename T>
    T at(uint64_t offset) const {
        check_range(offset, sizeof(T), "structure");
        T v;
        std::memcpy(&v, raw.data() + offset, sizeof(T));
        return v; // host is little-endian in this container, matches ELFDATA2LSB
    }

    std::string cstr(const unsigned char *strtab, uint64_t strtab_size, uint32_t idx) const {
        if (idx >= strtab_size) return "";
        const char *p = (const char*)strtab + idx;
        size_t maxlen = strtab_size - idx;
        size_t len = strnlen(p, maxlen);
        return std::string(p, len);
    }

    // One implementation for both classes: the Elf32_* and Elf64_* structs use the same field names.
    template <typename Ehdr, typename Shdr, typename Sym>
    void parse() {
        Ehdr eh = at<Ehdr>(0);
        if (eh.e_machine != EM_RISCV)
            throw std::runtime_error("e_machine is not EM_RISCV (this file is not a RISC-V ELF)");
        entry = eh.e_entry;

        if (eh.e_shentsize < sizeof(Shdr))
            throw std::runtime_error("bad section header entry size");
        check_range(eh.e_shoff, (uint64_t)eh.e_shnum * eh.e_shentsize, "section header table");

        std::vector<Shdr> shdrs(eh.e_shnum);
        for (int i = 0; i < eh.e_shnum; i++)
            shdrs[i] = at<Shdr>(eh.e_shoff + (uint64_t)i * eh.e_shentsize);

        if (eh.e_shstrndx >= shdrs.size())
            throw std::runtime_error("bad section-header string table index");
        const Shdr &shstr = shdrs[eh.e_shstrndx];
        check_range(shstr.sh_offset, shstr.sh_size, "section name table");
        const unsigned char *shstrtab = raw.data() + shstr.sh_offset;
        uint64_t shstrtab_size = shstr.sh_size;

        for (auto &sh : shdrs) {
            if (sh.sh_type == SHT_NULL) continue;
            if (sh.sh_flags & SHF_EXECINSTR) {
                SectionView sv;
                sv.name = cstr(shstrtab, shstrtab_size, sh.sh_name);
                sv.addr = sh.sh_addr;
                sv.offset = sh.sh_offset;
                sv.size = sh.sh_size;
                sv.execinstr = true;
                if (sh.sh_type != SHT_NOBITS) { // skip .bss-like exec sections (rare)
                    check_range(sv.offset, sv.size, "executable section");
                    exec_sections.push_back(sv);
                }
            }
        }

        // Symbol table: prefer .symtab, fall back to .dynsym
        int symtab_idx = -1;
        for (size_t i = 0; i < shdrs.size(); i++) {
            std::string nm = cstr(shstrtab, shstrtab_size, shdrs[i].sh_name);
            if (nm == ".symtab") { symtab_idx = (int)i; break; }
            if (nm == ".dynsym" && symtab_idx < 0) symtab_idx = (int)i;
        }
        if (symtab_idx >= 0) {
            const Shdr &symsh = shdrs[symtab_idx];
            if (symsh.sh_link >= shdrs.size())
                throw std::runtime_error("symbol table points to a missing string table");
            const Shdr &strsh = shdrs[symsh.sh_link];
            check_range(strsh.sh_offset, strsh.sh_size, "string table");
            check_range(symsh.sh_offset, symsh.sh_size, "symbol table");
            const unsigned char *strtab = raw.data() + strsh.sh_offset;
            uint64_t strtab_size = strsh.sh_size;
            uint64_t count = symsh.sh_size / sizeof(Sym);
            for (uint64_t i = 0; i < count; i++) {
                Sym sym = at<Sym>(symsh.sh_offset + i * sizeof(Sym));
                std::string nm = cstr(strtab, strtab_size, sym.st_name);
                if (nm.empty()) continue;
                SymbolView s{nm, sym.st_value, sym.st_shndx};
                symbols.push_back(s);
            }
        }
    }

    void parse64() { parse<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(); }
    void parse32() { parse<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(); }
};
