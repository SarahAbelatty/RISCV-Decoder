# Tests

## Run

```bash
sudo apt install libgtest-dev cmake                  # once
sudo apt install binutils-riscv64-linux-gnu          # optional: objdump reference

make test            # quick: builds + runs all GoogleTest tests (ASan + UBSan)
make objdump-diff    # compare against real riscv objdump on test.elf and hello.elf
make coverage        # needs: pip install gcovr

# or with CMake / CTest
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

## What is tested

| File | What it checks |
|---|---|
| `test_decode32.cpp` | ~180 real 32-bit encodings (table) + exhaustive immediates (I, S, B, J, U) + all register names + RV32/RV64 differences + invalid encodings |
| `test_decode16.cpp` | ~65 compressed encodings (table) + all 65,536 halfwords on RV32 and RV64 + reserved/illegal forms |
| `test_decode_one.cpp` | 16/32-bit length detection, little-endian order, truncated input, walking a mixed stream |
| `test_elf_reader.cpp` | ELF32/ELF64 parsing, exec sections, symbols, rejected inputs, corrupt/truncated files |
| `test_cli.cpp` | Runs the real `./riscv_decoder`: exit codes, error messages, output layout, golden file, `hello.elf` |
| `test_utils.cpp` | `sext`, `reg`, `hexi`, `csr_name`, ELF struct sizes and field offsets |
| `tools/diff_objdump.py` | Instruction-by-instruction diff against real `objdump` (about 97,000 instructions in `hello.elf`) |

## Where the expected values come from

Encodings in `cases_*.inc` are produced by the **real RISC-V assembler**
(`tools/gen_cases.py`), not by the decoder, so the tests do not repeat the decoder's own mistakes.
Regenerate them with `python3 tests/tools/gen_cases.py` (needs `binutils-riscv64-linux-gnu`).

The property tests use independent instruction **encoders** (`test_helpers.hpp`)
and an in-memory **ELF builder** for malformed-file tests.

## Known gaps (not failures)

`DISABLED_Decode16ZcbGaps` lists the Zcb compressed instructions (`c.mul`, `c.zext.b`, `c.sh`, ...)
the decoder does not implement yet. Remove the `DISABLED_` prefix when you add them.
Vector (V) and float (F/D) instructions print as `.word`; `diff_objdump.py` reports how many.

## Adding a test for a new instruction

1. Add `("asm text", "expected decoder text")` to a list in `tools/gen_cases.py`.
2. Run `python3 tests/tools/gen_cases.py`.
3. Run `make test`.
