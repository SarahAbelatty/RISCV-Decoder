CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
TARGET   := riscv_decoder
SRC      := main.cpp
HDRS     := elf_types.hpp elf_reader.hpp riscv_disasm.hpp

$(TARGET): $(SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# ------------------------------------------------------------------
# Tests.   One-time setup:  sudo apt install libgtest-dev
#          (optional)       sudo apt install binutils-riscv64-linux-gnu   # for objdump-diff
#   make test         build + run all GoogleTest tests (with ASan + UBSan)
#   make objdump-diff compare against a real riscv objdump
#   make coverage     line coverage of the headers (needs gcovr: pip install gcovr)
# ------------------------------------------------------------------
TEST_SRC   := $(wildcard tests/test_*.cpp)
TEST_DEPS  := $(TEST_SRC) $(wildcard tests/*.hpp tests/*.inc) $(HDRS)
TEST_DEFS  := -DRISCV_DECODER_BIN='"$(CURDIR)/$(TARGET)"' -DTEST_DATA_DIR='"$(CURDIR)"'
TEST_LIBS  := -lgtest -lgtest_main -pthread
OBJDUMP    ?= riscv64-linux-gnu-objdump

build-make/unit_tests: $(TEST_DEPS)
	@mkdir -p build-make
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -I. -Itests $(TEST_DEFS) $(TEST_SRC) -o $@ $(TEST_LIBS)

test: $(TARGET) build-make/unit_tests
	./build-make/unit_tests

objdump-diff: $(TARGET)
	python3 tests/tools/diff_objdump.py ./$(TARGET) test.elf  $(OBJDUMP)
	python3 tests/tools/diff_objdump.py ./$(TARGET) hello.elf $(OBJDUMP)

coverage: $(TARGET)
	@mkdir -p build-cov
	$(CXX) -std=c++17 -O0 -g --coverage -I. -Itests $(TEST_DEFS) $(TEST_SRC) -o build-cov/unit_tests $(TEST_LIBS)
	cd build-cov && ./unit_tests > /dev/null
	gcovr -r . --filter '.*\.hpp$$' --exclude 'tests/.*' --object-directory build-cov --print-summary

clean:
	rm -rf $(TARGET) build-make build-cov build

.PHONY: clean test objdump-diff coverage
