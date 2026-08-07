// The minimum a test directory has to be.
//
// This dir contains no CMakeLists.txt, and that is the point: test/CMakeLists.txt
// globs test/*/ at configure time, and any dir holding a test.cpp becomes the
// target and ctest name <dir>-test. Everything else is supplied for you — main()
// from test/main.cpp, Catch2, and the project library (<PROJECT>::lib) whenever
// that target exists. So a test can include a public header from include/ and
// call straight into src/lib/ with no build wiring of its own.
//
// That last part is what this file stands guard over. It calls a real symbol out
// of the library archive rather than only reading the constexpr values in
// <version.hpp>, because a test built against header constants alone would keep
// passing if the library link were ever dropped.
//
// Every *.cpp in this directory is globbed into the target, so helpers can live
// beside test.cpp — the empty file.cpp is here to show that. What does not belong
// here is a copy of a library source: link the library instead, which is now
// automatic.
//
// This dir is about mechanics. For how to write tests worth having, see
// test/20failure-testing/ and test/10example/.

#include <catch2/catch_test_macros.hpp>

#include <lib.hpp>
#include <version.hpp>

#include <string_view>

TEST_CASE("the project library is linked and reachable", "[lib][smoke]") {
  const auto* name = template_lib::version_string();

  REQUIRE(name != nullptr);
  REQUIRE(std::string_view{name} == PROGRAM_NAME);
}
