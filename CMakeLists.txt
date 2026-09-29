cmake_minimum_required(VERSION 3.14)
project(riscv_decoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Build tests with AddressSanitizer + UBSan" ON)
option(ENABLE_COVERAGE   "Build with gcov coverage instrumentation" OFF)

add_compile_options(-Wall -Wextra)

# ---- the program itself ----
add_executable(riscv_decoder main.cpp)
target_include_directories(riscv_decoder PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(riscv_decoder PRIVATE -O2)

# ---- GoogleTest: use the system copy if installed, otherwise download it ----
enable_testing()
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# ---- unit tests (headers are compiled straight into the test binary) ----
add_executable(unit_tests
  tests/test_utils.cpp
  tests/test_decode32.cpp
  tests/test_decode16.cpp
  tests/test_decode_one.cpp
  tests/test_elf_reader.cpp
  tests/test_cli.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(unit_tests PRIVATE
  RISCV_DECODER_BIN="$<TARGET_FILE:riscv_decoder>"
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(unit_tests PRIVATE GTest::gtest GTest::gtest_main)
add_dependencies(unit_tests riscv_decoder)

if(ENABLE_SANITIZERS)
  target_compile_options(unit_tests PRIVATE -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(unit_tests PRIVATE -fsanitize=address,undefined)
endif()
if(ENABLE_COVERAGE)
  target_compile_options(unit_tests PRIVATE --coverage -O0 -g)
  target_link_options(unit_tests PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 60 TEST_FILTER "-*DISABLED_*")

# ---- differential test against a real objdump (skipped if not installed) ----
find_program(RISCV_OBJDUMP NAMES riscv64-linux-gnu-objdump riscv64-unknown-elf-objdump)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(RISCV_OBJDUMP AND Python3_FOUND)
  foreach(elf test.elf hello.elf)
    add_test(NAME objdump_diff_${elf}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/tools/diff_objdump.py
              $<TARGET_FILE:riscv_decoder> ${CMAKE_SOURCE_DIR}/${elf} ${RISCV_OBJDUMP})
  endforeach()
else()
  message(STATUS "riscv objdump or python3 not found: objdump_diff tests not registered")
endif()
