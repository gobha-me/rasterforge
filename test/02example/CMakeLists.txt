  # A test dir with its own CMakeLists.txt opts OUT of the discovery loop's
  # wiring — the loop takes the add_subdirectory() branch and never reaches its
  # own link lines. Whatever that loop does for you, this file does for itself.
  # It inherits TEST_NAME and SRCS from the parent scope and must define a target
  # named exactly ${TEST_NAME}.

  message(STATUS "Configuring test setup ${TEST_NAME}")

  # Note there is no main.cpp here: this target gets main() from Catch2 instead
  # of test/main.cpp, which is one reason to want custom build control.
  add_executable(${TEST_NAME}
    ${SRCS}
  )

  target_include_directories(${TEST_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
  )

  target_link_libraries(${TEST_NAME}
    PRIVATE Catch2::Catch2WithMain
  )

  # The other half of owning your wiring: the automatic ${PROJECT_NAME}::lib link
  # does not reach here either. Same guard as the loop, same reason — the target
  # does not exist under -D<PROJECT>_BUILD_LIB=OFF.
  if (TARGET ${PROJECT_NAME}::lib)
    target_link_libraries(${TEST_NAME}
      PRIVATE ${PROJECT_NAME}::lib
    )
  endif ()
