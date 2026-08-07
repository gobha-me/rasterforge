// The failure-first discipline of test/20failure-testing, applied to this repo's
// own library through its public header.
//
// 20failure-testing makes the argument on a self-contained helper. This file is
// the same argument aimed at src/lib/: enumerate how version_at_least() could
// give a wrong answer, and make those the tests. The ways a version comparison
// goes wrong are well known — an off-by-one on the inclusive boundary (>= vs >),
// and components compared out of order, so that a large minor rescues a version
// whose major is already too small.
//
// The matrix is written *relative to this build's own version* rather than
// against literals, because that version comes from `git describe` at configure
// time and changes with every tag. Each case is derived from
// VERSION_MAJOR/MINOR/PATCH, so every assertion below holds at any tag —
// including the untagged 0.0.0 fallback, where the components are all zero and
// the "older" case simply coincides with the "exact" one.

#include <catch2/catch_test_macros.hpp>

#include <lib.hpp>
#include <version.hpp>

#include <cstdint>

namespace {

// One less, without wrapping: these are unsigned, and 0u - 1u is not "before
// zero", it is four billion. Saturating keeps the request valid at version 0.
constexpr auto older(std::uint32_t component) -> std::uint32_t {
  return component == 0 ? 0 : component - 1;
}

}  // namespace

using template_lib::version_at_least;

TEST_CASE("version_at_least: failure modes are the contract", "[version][failure]") {
  SECTION("boundary: the build satisfies its own exact version (>= is inclusive)") {
    REQUIRE(version_at_least(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH));
  }

  SECTION("boundary: one patch above is not satisfied") {
    REQUIRE_FALSE(version_at_least(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH + 1));
  }

  SECTION("one minor above is not satisfied, even asking for patch 0") {
    // Catches a comparison that consults patch before minor: patch 0 is
    // trivially satisfied, so an out-of-order implementation answers true here.
    REQUIRE_FALSE(version_at_least(VERSION_MAJOR, VERSION_MINOR + 1, 0));
  }

  SECTION("one major above is not satisfied, even asking for minor and patch 0") {
    // Precedence, stated the way it can be checked at any tag: a request that is
    // smaller in every other component still loses on major alone.
    REQUIRE_FALSE(version_at_least(VERSION_MAJOR + 1, 0, 0));
  }

  SECTION("an older version is satisfied") {
    REQUIRE(version_at_least(older(VERSION_MAJOR), older(VERSION_MINOR), older(VERSION_PATCH)));
  }

  SECTION("patch 0 of the current major.minor is satisfied") {
    REQUIRE(version_at_least(VERSION_MAJOR, VERSION_MINOR, 0));
  }

  // The happy path — present, but deliberately last and least interesting.
  SECTION("happy path: 0.0.0 is satisfied by every build") {
    REQUIRE(version_at_least(0, 0, 0));
  }
}
