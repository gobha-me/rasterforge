if (NOT TARGET ${PROJECT_NAME}::lib)
  message(STATUS "${PROJECT_NAME}::lib is not built — skipping fuzz targets")
  return()
endif ()

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "libFuzzer requires Clang — skipping fuzz targets")
  return()
endif ()

function(add_rasterforge_fuzzer TARGET_NAME SOURCE_FILE)
  add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL
    ${SOURCE_FILE}
  )

  target_link_libraries(${TARGET_NAME}
    PRIVATE ${PROJECT_NAME}::lib
  )

  # The fuzz toolchain instruments RasterForge itself with fuzzer-no-link plus
  # ASan/UBSan. Each target supplies libFuzzer's main and sanitizer runtimes.
  target_compile_options(${TARGET_NAME}
    PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
  )
  target_link_options(${TARGET_NAME}
    PRIVATE -fsanitize=fuzzer,address,undefined
  )
endfunction()

add_rasterforge_fuzzer(${PROJECT_NAME}-decode-fuzzer decode_fuzzer.cpp)
add_rasterforge_fuzzer(${PROJECT_NAME}-fit-fuzzer fit_fuzzer.cpp)
