# Clang libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer toolchain.
# Usage:
#   cmake -B build-fuzz -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fuzz.cmake
include(${CMAKE_CURRENT_LIST_DIR}/clang.cmake)

if (NOT CLANG_CXX)
  message(FATAL_ERROR "the fuzz toolchain requires clang++")
endif ()

message(STATUS "fuzz sanitizers: -fsanitize=fuzzer-no-link,address,undefined")

# Instrument every RasterForge translation unit. The fuzz executable replaces
# fuzzer-no-link with fuzzer at link time so exactly one libFuzzer main exists.
string(APPEND CMAKE_CXX_FLAGS
       " -fsanitize=fuzzer-no-link,address,undefined"
       " -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address,undefined")
