// main.cpp
// RISC-V ELF decoder.
// Usage: ./riscv_decoder <path-to-riscv-elf>
#include "elf_types.hpp"
#include "elf_reader.hpp"
#include "riscv_disasm.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <map>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <riscv-elf-file>\n";
        return 1;
    }

    ElfFile elf;
    try {
        elf.load(argv[1]);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << argv[1] << ":\tfile format elf" << (elf.is64 ? "64" : "32") << "-littleriscv\n";
    std::cout << "entry point: 0x" << std::hex << elf.entry << std::dec << "\n\n";

    if (elf.exec_sections.empty()) {
        std::cout << "No executable (SHF_EXECINSTR) sections found.\n";
        return 0;
    }

    // address -> symbol name, for labels like objdump's "<main>:"
    std::map<uint64_t, std::string> labels;
    for (auto &s : elf.symbols)
        if (s.value != 0 && !s.name.empty())
            labels[s.value] = s.name;

    for (auto &sec : elf.exec_sections) {
        std::cout << "Disassembly of section " << sec.name << ":\n\n";
        const unsigned char *data = elf.raw.data() + sec.offset;
        uint64_t off = 0;
        while (off < sec.size) {
            uint64_t vaddr = sec.addr + off;
            auto it = labels.find(vaddr);
            if (it != labels.end())
                std::cout << "\n" << std::hex << vaddr << " <" << it->second << ">:\n" << std::dec;

            size_t avail = sec.size - off;
            riscv::DecodedInsn d = riscv::decode_one(data + off, avail, vaddr, elf.is64);

            std::cout << "    " << std::hex << std::setw(elf.is64 ? 12 : 8) << std::setfill('0')
                      << vaddr << ":\t";
            std::cout << std::setfill('0');
            if (d.size == 2)
                std::cout << std::setw(4) << (d.raw & 0xffff);
            else
                std::cout << std::setw(8) << d.raw;
            std::cout << std::dec << std::setfill(' ') << "\t" << d.text << "\n";

            off += (d.size > 0 ? d.size : 2);
        }
        std::cout << "\n";
    }
    return 0;
}
