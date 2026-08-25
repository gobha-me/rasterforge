add_executable(37color-metadata-test
  ../main.cpp
  test.cpp
)

target_include_directories(37color-metadata-test
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PUBLIC ${PROJECT_SOURCE_DIR}/test/include
)

target_link_libraries(37color-metadata-test
  PRIVATE
    Catch2::Catch2
    rasterforge::lib
    ZLIB::ZLIB
)
