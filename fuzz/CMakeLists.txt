if (NOT TARGET ${PROJECT_NAME}::lib)
  message(STATUS "${PROJECT_NAME}::lib is not built — skipping fuzz targets")
  return()
endif ()

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "libFuzzer requires Clang — skipping fuzz targets")
  return()
endif ()

add_executable(${PROJECT_NAME}-decode-fuzzer EXCLUDE_FROM_ALL
  decode_fuzzer.cpp
)

target_link_libraries(${PROJECT_NAME}-decode-fuzzer
  PRIVATE ${PROJECT_NAME}::lib
)

# The fuzz toolchain instruments RasterForge itself with fuzzer-no-link plus
# ASan/UBSan. This target supplies libFuzzer's main and sanitizer runtimes.
target_compile_options(${PROJECT_NAME}-decode-fuzzer
  PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
)
target_link_options(${PROJECT_NAME}-decode-fuzzer
  PRIVATE -fsanitize=fuzzer,address,undefined
)
