// Placeholder translation unit for the default compiled-library target.
//
// A STATIC library with no sources is ill-formed on some generators, and an
// empty archive teaches nothing. Replace this file, its public header
// (include/lib.hpp), and the add_library() source list in
// src/lib/CMakeLists.txt with the project's real sources.
//
// The generated version header in include/ is pulled in below, so the
// include-dir wiring is exercised by the default build + tests.

// Own header first: including the public header in its implementation unit is
// what catches declaration/definition drift at compile time instead of at link
// time in someone else's build.
#include <lib.hpp>

#include <version.hpp>

namespace template_lib {

// Trivial symbol so the archive is non-empty and linkable. Demonstrates that
// the generated version header is visible to the library target.
auto version_string() -> const char* {
  return PROGRAM_NAME.data();
}

// Component-major-first: each component settles the answer outright unless it
// ties, in which case the next one is consulted. Written as explicit comparisons
// rather than by packing the three components into one integer — packing looks
// tidier and silently gives the wrong answer the moment a component outgrows the
// field width it was assigned.
auto version_at_least(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) -> bool {
  if (VERSION_MAJOR != major) {
    return VERSION_MAJOR > major;
  }

  if (VERSION_MINOR != minor) {
    return VERSION_MINOR > minor;
  }

  return VERSION_PATCH >= patch;
}

}  // namespace template_lib
