# RISC-V ELF Decoder

A small, dependency-free **C++17 disassembler** for RISC-V. Point it at a RISC-V ELF binary and it prints an `objdump -d`-style listing: addresses, raw instruction bits and decoded assembly, with symbol labels.

The ELF parser is written from scratch (no libelf, no binutils), and the whole decoder lives in header files, so it is easy to read, test and reuse.

```
$ ./riscv_decoder test.elf
test.elf:	file format elf64-littleriscv
entry point: 0x10000

Disassembly of section .text:

    000000010000:	00000513	li a0,0
    000000010004:	00100593	li a1,1
    000000010008:	00b58633	add a2,a1,a1
    00000001000c:	4501	li a0,0
    00000001000e:	0585	addi a1,a1,1
    000000010018:	ff010113	addi sp,sp,-16
    00000001001c:	00a13423	sd a0,8(sp)
    000000010020:	8082	ret
```

## Features

- **RV32 and RV64**: ELF32 and ELF64 handled by one templated parser
- **32-bit and compressed (16-bit) instructions**, mixed freely
- **Wide ISA coverage** (see table below), including the full **RVV 1.0 vector extension**
- **objdump-style output**: pseudo-instructions (`li`, `mv`, `ret`, `j`, `csrr`, ...), ABI register names, named CSRs
- **Symbol labels** such as `<main>:` taken from `.symtab` (or `.dynsym`)
- **Defensive parsing**: every read is bounds-checked, so corrupt or truncated files give a clear error instead of a crash
- **Verified against a real `objdump`** with a differential test, and unit tests run under ASan + UBSan

## Supported instruction sets

| Extension | Status |
|---|---|
| RV32I / RV64I | Supported |
| M (multiply/divide) | Supported |
| A (atomics) | Supported |
| C (compressed) | Integer forms supported; compressed FP forms print as `.half` |
| Zcb | Partial (`c.lbu` only) |
| Zicsr, Zifencei, Zihintpause | Supported |
| Zba, Zbb, Zbs | Supported |
| Zicond | Supported |
| V (RVV 1.0) | Supported |
| F / D (scalar floating point) | Not decoded (prints `.word`) |
| Privileged | Partial (`mret`, `sret`, `wfi`, M/S-mode CSR names) |

Anything unrecognised is printed as `.word 0x........` / `.half 0x....` so the listing stays aligned.

## Getting started

### Requirements

- A C++17 compiler (g++ or clang++)
- Optional: GoogleTest, CMake, Python 3, a RISC-V `objdump` (only for tests)

### Build

With Make:

```bash
make
```

With CMake:

```bash
cmake -S . -B build
cmake --build build
```

### Run

```bash
./riscv_decoder <path-to-riscv-elf>

./riscv_decoder test.elf
./riscv_decoder hello.elf | less
```

Exit code is `0` on success and `1` on a usage error or invalid/corrupt ELF file.

## Testing

One-time setup on Debian/Ubuntu:

```bash
sudo apt install libgtest-dev
sudo apt install binutils-riscv64-linux-gnu   # optional, for objdump-diff
```

| Command | What it does |
|---|---|
| `make test` | Builds and runs all GoogleTest unit tests with AddressSanitizer + UBSan |
| `make objdump-diff` | Compares output against a real `riscv64-*-objdump` on `test.elf` and `hello.elf` |
| `make coverage` | Line coverage of the headers (needs `pip install gcovr`) |

With CMake, run `ctest --test-dir build`. Sanitizers are on by default (`-DENABLE_SANITIZERS=OFF` to disable) and coverage is enabled with `-DENABLE_COVERAGE=ON`.

## Project structure

```
.
├── main.cpp            Driver: CLI, label map, print loop
├── elf_types.hpp       ELF32/ELF64 structs, SectionView, SymbolView
├── elf_reader.hpp      ElfFile: loads and validates the ELF, finds code + symbols
├── riscv_disasm.hpp    decode_one(), decode32(), decode16(), helpers
├── riscv_vector.hpp    RVV 1.0 decoder (namespace riscv::vec), table-driven
├── Makefile
├── CMakeLists.txt
├── make_test_elf.py    Generates test.elf (hand-built RV64 ELF)
├── hello.c             Source of hello.elf
├── test.elf            320-byte sample input
├── hello.elf           Statically linked RV64GC sample (~577 KB)
└── tests/              GoogleTest suites + tools/diff_objdump.py
```

## How it works

```
ELF file → parse headers → find executable sections → decode each instruction → print
                                   ↑                          ↑
                              elf_reader.hpp           riscv_disasm.hpp
```

1. **`main.cpp`** validates arguments, loads the file, builds an address-to-symbol map and walks every executable section.
2. **`elf_reader.hpp`** checks the magic, class, endianness and `e_machine == EM_RISCV`, then extracts every `SHF_EXECINSTR` section and the symbol table.
3. **`riscv_disasm.hpp`** looks at the two lowest bits of each instruction to decide whether it is 16 or 32 bits wide, then dispatches to `decode16()` or `decode32()`.
4. **`riscv_vector.hpp`** handles vector opcodes with lookup tables instead of large switch statements.

The ELF side and the decoder side never include each other, so the decoder can be reused on any raw byte buffer:

```cpp
#include "riscv_disasm.hpp"

const unsigned char bytes[] = {0x13, 0x01, 0x01, 0xff};
riscv::DecodedInsn d = riscv::decode_one(bytes, sizeof bytes, /*addr=*/0x10000, /*rv64=*/true);
// d.size == 4, d.raw == 0xff010113, d.text == "addi sp,sp,-16"
```

## Limitations

- Only little-endian ELF files are accepted (effectively all RISC-V targets)
- Requires section headers; files without them are not disassembled
- Scalar F/D floating-point instructions are not decoded yet
- Branch and jump targets are printed as absolute addresses, not `<symbol+offset>`

## Roadmap

- [ ] Scalar F/D (and Zfh) instructions, including compressed FP forms
- [ ] Complete Zcb, add Zcmp/Zcmt
- [ ] Privileged and hypervisor instructions (`sfence.vma`, ...)
- [ ] Use program headers when section headers are missing
- [ ] `--section` / address-range options and symbolic branch targets

## Contributing

Issues and pull requests are welcome. Please run `make test` and `make objdump-diff` before submitting a change, and add a unit test for any new instruction you decode.

## License

Add your license here (for example MIT) and include a `LICENSE` file in the repo.
