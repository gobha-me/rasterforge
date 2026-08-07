option(${PROJECT_NAME}_BUILD_LIB "Build the RasterForge library" ON)

if (${PROJECT_NAME}_BUILD_LIB)
  add_library(${PROJECT_NAME}_lib STATIC
    image.cpp
  )
  add_library(${PROJECT_NAME}::lib ALIAS ${PROJECT_NAME}_lib)

  target_include_directories(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_features(${PROJECT_NAME}_lib PUBLIC cxx_std_23)
endif ()
