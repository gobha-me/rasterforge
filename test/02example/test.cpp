// The same test as test/01example, in a dir that builds itself.
//
// This directory ships a CMakeLists.txt, so the discovery loop hands it to
// add_subdirectory() and stops there — none of the loop's own wiring applies.
// That file therefore has to supply the two things this test needs and would
// otherwise have been given: main() (here from Catch2::Catch2WithMain rather
// than test/main.cpp) and the link against the project library.
//
// Reach for this only when a test genuinely needs custom build control — extra
// sources from outside the dir, a different Catch2 main, its own compile
// definitions. The cost is that the harness stops maintaining the target for
// you, as the duplicated link line next door demonstrates.

#include <catch2/catch_test_macros.hpp>

#include <lib.hpp>
#include <version.hpp>

#include <string_view>

TEST_CASE("a self-built test dir can still reach the library", "[lib][smoke]") {
  REQUIRE(std::string_view{template_lib::version_string()} == PROGRAM_NAME);
}
