# riscv_decoder

A small, self-contained C++17 ELF/RISC-V decoder: it reads a real ELF
file, finds every executable section, and disassembles the machine
code into RISC-V assembly text.

## What it supports

- **ELF**: both ELF32 and ELF64, little-endian only (RISC-V is
  virtually always LE). Reads section headers, finds any section with
  `SHF_EXECINSTR` set (`.text`, `.init`, `.fini`, `.plt`, ...), and
  reads the symbol table (`.symtab` or `.dynsym`) to print `<label>:`
  markers like objdump does.
- **RISC-V instructions**:
  - RV32I / RV64I base integer ISA (all of it: arithmetic, loads,
    stores, branches, jumps, LUI/AUIPC, FENCE, ECALL/EBREAK, CSR ops)
  - **M** extension (MUL/DIV/REM and the W-suffixed 64-bit variants)
  - **C** extension (the 16-bit compressed instructions — this is
    what makes it decode *real* compiler output, since GCC/Clang emit
    compressed instructions by default whenever `-march` includes `C`)
  - Common pseudo-instructions are recognized and printed the way
    objdump prints them: `ret`, `nop`, `mv`, `li`, `j`, `jr`.
- **Not implemented**: F/D (floating point) and A (atomics) extension
  instructions are *recognized structurally* (opcode is matched) but
  printed as a `.word 0x... # unsupported` placeholder rather than
  guessed at. This is a deliberate scope decision, not an oversight —
  see "Extending it" below for exactly where to add them.

## Files

```
elf_types.hpp     ELF32/ELF64 struct definitions (from the ELF spec)
elf_reader.hpp    Loads a file, parses headers, extracts exec sections + symbols
riscv_disasm.hpp  The actual instruction decoder (16-bit and 32-bit)
main.cpp          CLI: ties it together, prints objdump-style output
Makefile
```

## Build

```bash
make
# or directly:
g++ -std=c++17 -O2 -o riscv_decoder main.cpp
```

## Run

```bash
./riscv_decoder /path/to/some/riscv/binary
```

Example output:

```
test.elf:	file format elf64-littleriscv
entry point: 0x10000

Disassembly of section .text:

    000000010000:	00000513	addi a0,zero,0
    000000010004:	00100593	addi a1,zero,1
    000000010008:	00b58633	add a2,a1,a1
    00000001000c:	4501	li a0,0
    00000001000e:	0585	addi a1,a1,1
    ...
```

## Getting a real RISC-V ELF file to test with

This sandbox has no RISC-V cross-compiler and no network access, so
the decoder here was validated against hand-verified machine code
bytes instead (see `make_test_elf.py`, and the independent bit-level
cross-check done during development). On your own machine, get a real
binary any of these ways:

1. **Install a cross toolchain** (Ubuntu/Debian):
   ```bash
   sudo apt install gcc-riscv64-linux-gnu
   riscv64-linux-gnu-gcc -O0 -static hello.c -o hello.elf
   ./riscv_decoder hello.elf
   ```
2. **Compile bare-metal** with `riscv64-unknown-elf-gcc` (from the
   riscv-gnu-toolchain project) if you want RV32 or no libc.
3. **Use an existing RISC-V binary** you already have (e.g. anything
   from a RISC-V Linux root filesystem, `/usr/bin/*` on a RISC-V
   board, or `.ko`/`.o` files — as long as `e_machine == EM_RISCV`).
4. Cross-check against `objdump -d` if you have RISC-V-capable
   binutils installed (`riscv64-linux-gnu-objdump -d hello.elf`) —
   useful while you extend the decoder further.

## Extending it

- **Add F/D (floating point)**: opcodes `0000111`/`0100111` (loads/
  stores: FLW/FLD/FSW/FSD) and `1010011` (FP ALU ops, keyed off
  `funct7`'s top 5 bits for the operation and bottom 2 for
  single/double). Add float register names `f0`-`f31` next to `reg()`
  in `riscv_disasm.hpp`.
- **Add A (atomics)**: opcode `0101111`, keyed off `funct7`'s top 5
  bits (LR/SC/AMOSWAP/AMOADD/...).
- **Add more CSR names**: extend `csr_name()`.
- **Symbolic relocation display**: if you want `call foo` instead of
  a raw hex target for `.plt` entries, cross-reference `.rela.*`
  sections — not implemented here to keep scope focused on decoding.

## How this was validated

Every hand-picked test instruction's decode was independently
recomputed from the RISC-V spec's bit-field definitions in a separate
Python script (not reusing this program's logic) and compared against
this decoder's actual output — arithmetic, branch-target
calculation, store addressing, and both compressed and pseudo-
instruction printing all matched.
