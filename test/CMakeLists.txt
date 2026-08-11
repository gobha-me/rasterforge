add_test(NAME startup COMMAND ${CMAKE_CURRENT_LIST_DIR}/../cmake/startup.sh)
add_test(NAME shutdown COMMAND ${CMAKE_CURRENT_LIST_DIR}/../cmake/shutdown.sh)

# Pure cmake -P unit test for live and archived version parsing.
# Registered standalone — it is a script, not a `<dir>-test` executable, so it must
# not go through the auto-discovery foreach below. No fixtures: it has no runtime deps.
add_test(
  NAME version-parse-selftest
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/version_selftest.cmake
)

add_test(
  NAME version-archive-selftest
  COMMAND ${CMAKE_COMMAND}
          -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
          -DWORK_DIR=${PROJECT_BINARY_DIR}/version-archive-selftest
          -P ${PROJECT_SOURCE_DIR}/cmake/version_archive_selftest.cmake
)

# Bootstrap-artifact and build-wiring check, run in enforcement mode.
add_test(
  NAME artifact-check
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/check_artifacts.cmake
)

file(GLOB TEST_DIRS ${CMAKE_CURRENT_LIST_DIR}/*)

# Say this once, at configure time, instead of letting it surface as a link error
# buried in a build log. The guard below keeps *configuring* valid with the
# library switched off; it cannot conjure symbols for a test that calls into it.
if (NOT TARGET ${PROJECT_NAME}::lib)
  message(STATUS
    "${PROJECT_NAME}::lib is not built — discovered tests will not link against it. "
    "Pair -D${PROJECT_NAME}_BUILD_LIB=OFF with -D${PROJECT_NAME}_TESTS=OFF, or keep "
    "only tests that do not call into the library."
  )
endif ()

foreach(TEST_DIR ${TEST_DIRS})
  cmake_path(GET TEST_DIR FILENAME TEST_NAME_PATH)
  set(TEST_NAME ${TEST_NAME_PATH}-test)

  # Recompute the source list for THIS dir every iteration, before the branch
  # below. Both branches consume SRCS — the custom-CMakeLists branch inherits it
  # via add_subdirectory() — so setting it only inside the elseif would leak the
  # previous iteration's sources into a custom-CMakeLists test dir.
  file(GLOB SRCS ${TEST_DIR}/*.cpp)

  if(EXISTS ${TEST_DIR}/CMakeLists.txt)
    add_subdirectory(${TEST_DIR})
  elseif (EXISTS ${TEST_DIR}/test.cpp)
    message(STATUS "Configuring test setup ${TEST_NAME}")

    add_executable(${TEST_NAME}
      main.cpp
      ${SRCS}
    )

    target_include_directories(${TEST_NAME}
      PUBLIC ${PROJECT_SOURCE_DIR}/include
      PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
    )

    target_link_libraries(${TEST_NAME}
      PRIVATE Catch2::Catch2
    )

    # Auto-discovered tests get the project library for free. Without this,
    # testing your own code means either a per-dir CMakeLists.txt or copying
    # library sources into the test dir — and the first test written against
    # src/lib/ dies at link time with `undefined reference`, which reads like a
    # broken build rather than a missing line.
    #
    # The guard is not optional: -D<PROJECT>_BUILD_LIB=OFF never creates the
    # target, and an unguarded link line would turn that supported configuration
    # into a hard configure error.
    #
    if (TARGET ${PROJECT_NAME}::lib)
      target_link_libraries(${TEST_NAME}
        PRIVATE ${PROJECT_NAME}::lib
      )
    endif ()
  else()
    continue()
  endif()

  # No -s here: it makes Catch2 report every *successful* assertion too, which
  # buries the interesting output in a full-suite run. Ask for it per invocation
  # when you actually want it — `ctest -R <name> -V`, or `./build/test/<dir>-test -s`.
  add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
  )

  set_tests_properties(${TEST_NAME} PROPERTIES FIXTURES_REQUIRED runners)
endforeach()

# SET FIXTURE DEPENDENCIES
set_tests_properties(startup PROPERTIES FIXTURES_SETUP runners)
set_tests_properties(shutdown PROPERTIES FIXTURES_CLEANUP runners)

# The sanitizer smoke test (test/30sanitizer-smoke) deliberately trips the active
# sanitizer to prove it is actually engaged (the whole point is to catch a
# silently no-op sanitizer toolchain). ASan/TSan abort the process on the injected
# bug, i.e. a NONZERO exit, which ctest would otherwise read as failure. So when
# such a sanitizer is active we assert on its diagnostic instead: the test passes
# iff the sanitizer's own report appears in the output. This turns "sanitizer
# engaged" into a green, enforceable signal — and if the sanitizer were ever
# silenced, the report vanishes and this test goes red. UBSan is recoverable
# (exits 0) and the default build never runs the bug, so neither needs this.
if (TARGET 30sanitizer-smoke-test)
  if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=address")
    set_tests_properties(30sanitizer-smoke-test PROPERTIES PASS_REGULAR_EXPRESSION "AddressSanitizer")
  elseif (CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
    set_tests_properties(30sanitizer-smoke-test PROPERTIES PASS_REGULAR_EXPRESSION "ThreadSanitizer")
  endif ()
endif ()
