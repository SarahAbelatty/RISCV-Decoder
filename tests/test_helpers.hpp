// test_helpers.hpp
// Shared helpers for the decoder tests:
//   * tiny wrappers around riscv::decode32 / decode16
//   * instruction ENCODERS (build a 32-bit word from fields) used by the
//     property tests, so expected values never come from the decoder itself
//   * ElfBuilder: builds small ELF32/ELF64 files in memory for the reader tests
//   * TempFile: writes bytes to a temp file and deletes it afterwards
#pragma once
#include "riscv_disasm.hpp"
#include "elf_reader.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace t {

// Address used for all decode tests. Big enough that "addr - 1 MiB" stays positive.
constexpr uint64_t kAddr = 0x200000;

inline std::string d32(uint32_t insn, uint64_t addr = kAddr, bool rv64 = true) {
    return riscv::decode32(insn, addr, rv64);
}
inline std::string d16(uint16_t insn, uint64_t addr = kAddr, bool rv64 = true) {
    return riscv::decode16(insn, addr, rv64);
}
inline std::string hex(uint64_t v) {
    char b[32]; std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)v); return b;
}
inline bool starts_with(const std::string &s, const char *p) {
    return s.rfind(p, 0) == 0;
}

// ABI register names written out independently of riscv::reg().
inline const char *abi(int r) {
    static const char *n[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","t3","t4","t5","t6"};
    return n[r];
}

// ------------------------- instruction encoders -------------------------
inline uint32_t enc_r(uint32_t op, uint32_t f3, uint32_t f7, uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
inline uint32_t enc_i(uint32_t op, uint32_t f3, uint32_t rd, uint32_t rs1, int32_t imm) {
    return (((uint32_t)imm & 0xfff) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
inline uint32_t enc_s(uint32_t op, uint32_t f3, uint32_t rs1, uint32_t rs2, int32_t imm) {
    uint32_t u = (uint32_t)imm & 0xfff;
    return ((u >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((u & 0x1f) << 7) | op;
}
inline uint32_t enc_b(uint32_t f3, uint32_t rs1, uint32_t rs2, int32_t imm) {
    uint32_t u = (uint32_t)imm & 0x1fff;
    return (((u >> 12) & 1) << 31) | (((u >> 5) & 0x3f) << 25) | (rs2 << 20) | (rs1 << 15) |
           (f3 << 12) | (((u >> 1) & 0xf) << 8) | (((u >> 11) & 1) << 7) | 0x63;
}
inline uint32_t enc_u(uint32_t op, uint32_t rd, uint32_t imm20) {
    return (imm20 << 12) | (rd << 7) | op;
}
inline uint32_t enc_j(uint32_t rd, int32_t imm) {
    uint32_t u = (uint32_t)imm & 0x1fffff;
    return (((u >> 20) & 1) << 31) | (((u >> 1) & 0x3ff) << 21) | (((u >> 11) & 1) << 20) |
           (((u >> 12) & 0xff) << 12) | (rd << 7) | 0x6f;
}

// ----------------------------- ELF builder ------------------------------
struct BSection {
    std::string name;
    uint32_t type = SHT_PROGBITS;
    uint64_t flags = 0x6;            // ALLOC | EXECINSTR
    uint64_t addr = 0x10000;
    std::vector<uint8_t> data;
};
struct BSymbol { std::string name; uint64_t value; uint16_t shndx; };

class ElfBuilder {
public:
    bool is64 = true;
    uint16_t machine = EM_RISCV;
    uint8_t ei_class = 0;            // 0 = derive from is64
    uint8_t ei_data = ELFDATA2LSB;
    uint64_t entry = 0x10000;
    std::vector<BSection> secs;      // user sections (index 1..n)
    std::vector<BSymbol> syms;       // -> .symtab/.strtab
    std::vector<BSymbol> dynsyms;    // -> .dynsym/.dynstr (emitted BEFORE .symtab)

    ElfBuilder &text(const std::vector<uint8_t> &code, uint64_t addr = 0x10000,
                     const std::string &name = ".text") {
        BSection s; s.name = name; s.addr = addr; s.data = code; secs.push_back(s); return *this;
    }
    ElfBuilder &section(const std::string &name, uint32_t type, uint64_t flags, uint64_t addr,
                        const std::vector<uint8_t> &data) {
        BSection s; s.name = name; s.type = type; s.flags = flags; s.addr = addr; s.data = data;
        secs.push_back(s); return *this;
    }
    std::vector<uint8_t> build() const { return is64 ? build_impl<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>()
                                                     : build_impl<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(); }
private:
    template <class Ehdr, class Shdr, class Sym>
    std::vector<uint8_t> build_impl() const {
        struct Out { std::string name; uint32_t type; uint64_t flags, addr; std::vector<uint8_t> data;
                     uint32_t link; uint64_t entsize; };
        std::vector<Out> all;
        all.push_back({"", SHT_NULL, 0, 0, {}, 0, 0});
        for (auto &s : secs) all.push_back({s.name, s.type, s.flags, s.addr, s.data, 0, 0});

        auto add_symtab = [&](const std::vector<BSymbol> &v, const char *symname, const char *strname, uint32_t type) {
            std::vector<uint8_t> str{0}, tab(sizeof(Sym), 0);   // entry 0 is the null symbol
            for (auto &s : v) {
                Sym e{}; e.st_name = (uint32_t)str.size();
                for (char c : s.name) str.push_back((uint8_t)c);
                str.push_back(0);
                e.st_value = (decltype(e.st_value))s.value; e.st_shndx = s.shndx;
                auto p = (const uint8_t *)&e; tab.insert(tab.end(), p, p + sizeof e);
            }
            uint32_t strndx = (uint32_t)all.size() + 1;
            all.push_back({symname, type, 0, 0, tab, strndx, sizeof(Sym)});
            all.push_back({strname, 3 /*STRTAB*/, 0, 0, str, 0, 0});
        };
        if (!dynsyms.empty()) add_symtab(dynsyms, ".dynsym", ".dynstr", 11 /*DYNSYM*/);
        if (!syms.empty())    add_symtab(syms, ".symtab", ".strtab", 2 /*SYMTAB*/);

        std::vector<uint8_t> shstr{0};
        std::vector<uint32_t> name_off;
        for (auto &s : all) {
            if (s.name.empty()) { name_off.push_back(0); continue; }
            name_off.push_back((uint32_t)shstr.size());
            for (char c : s.name) shstr.push_back((uint8_t)c);
            shstr.push_back(0);
        }
        uint32_t shstr_name = (uint32_t)shstr.size();
        for (char c : std::string(".shstrtab")) shstr.push_back((uint8_t)c);
        shstr.push_back(0);
        all.push_back({".shstrtab", 3, 0, 0, shstr, 0, 0});
        name_off.push_back(shstr_name);

        std::vector<uint8_t> out(sizeof(Ehdr), 0);
        std::vector<uint64_t> offs;
        for (auto &s : all) {
            while (out.size() % 8) out.push_back(0);
            offs.push_back(out.size());
            if (s.type != SHT_NOBITS) out.insert(out.end(), s.data.begin(), s.data.end());
        }
        while (out.size() % 8) out.push_back(0);
        uint64_t shoff = out.size();
        for (size_t i = 0; i < all.size(); i++) {
            Shdr h{};
            h.sh_name = name_off[i]; h.sh_type = all[i].type; h.sh_flags = (decltype(h.sh_flags))all[i].flags;
            h.sh_addr = (decltype(h.sh_addr))all[i].addr; h.sh_offset = (decltype(h.sh_offset))offs[i];
            h.sh_size = (decltype(h.sh_size))all[i].data.size(); h.sh_link = all[i].link;
            h.sh_entsize = (decltype(h.sh_entsize))all[i].entsize;
            auto p = (const uint8_t *)&h; out.insert(out.end(), p, p + sizeof h);
        }
        Ehdr eh{};
        eh.e_ident[0] = 0x7f; eh.e_ident[1] = 'E'; eh.e_ident[2] = 'L'; eh.e_ident[3] = 'F';
        eh.e_ident[EI_CLASS] = ei_class ? ei_class : (uint8_t)(is64 ? ELFCLASS64 : ELFCLASS32);
        eh.e_ident[EI_DATA] = ei_data; eh.e_ident[EI_VERSION] = 1;
        eh.e_type = 2; eh.e_machine = machine; eh.e_version = 1;
        eh.e_entry = (decltype(eh.e_entry))entry; eh.e_shoff = (decltype(eh.e_shoff))shoff;
        eh.e_ehsize = sizeof(Ehdr); eh.e_shentsize = sizeof(Shdr);
        eh.e_shnum = (uint16_t)all.size(); eh.e_shstrndx = (uint16_t)(all.size() - 1);
        std::memcpy(out.data(), &eh, sizeof eh);
        return out;
    }
};

// Patch a field of a built image: put<uint16_t>(img, offsetof(Elf64_Ehdr, e_machine), 62)
template <class T> void put(std::vector<uint8_t> &img, size_t off, T v) {
    std::memcpy(img.data() + off, &v, sizeof v);
}
template <class T> T get(const std::vector<uint8_t> &img, size_t off) {
    T v; std::memcpy(&v, img.data() + off, sizeof v); return v;
}

// ------------------------------ temp file -------------------------------
class TempFile {
public:
    explicit TempFile(const std::vector<uint8_t> &bytes) {
        static int n = 0;
        path_ = (std::filesystem::temp_directory_path() /
                 ("riscv_dec_test_" + std::to_string(::getpid()) + "_" + std::to_string(n++) + ".elf")).string();
        std::ofstream f(path_, std::ios::binary);
        f.write((const char *)bytes.data(), (std::streamsize)bytes.size());
    }
    ~TempFile() { std::remove(path_.c_str()); }
    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;
    const std::string &path() const { return path_; }
private:
    std::string path_;
};

inline ElfFile load_image(const std::vector<uint8_t> &img) {
    TempFile f(img);
    ElfFile e; e.load(f.path()); return e;
}

// Little-endian byte helpers for building code buffers.
inline void push32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xff); }
inline void push16(std::vector<uint8_t> &v, uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); }

} // namespace t
