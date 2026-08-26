add_executable(${PROJECT_NAME}-benchmark
  allocation_tracker.cpp
  main.cpp
)

target_include_directories(${PROJECT_NAME}-benchmark
  PRIVATE
    ${PROJECT_SOURCE_DIR}/example/consumer
)
target_compile_features(${PROJECT_NAME}-benchmark PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME}-benchmark
  PRIVATE
    RASTERFORGE_BENCHMARK_BUILD_TYPE="$<CONFIG>"
)
target_link_libraries(${PROJECT_NAME}-benchmark
  PRIVATE
    ${PROJECT_NAME}::lib
    ZLIB::ZLIB
)
