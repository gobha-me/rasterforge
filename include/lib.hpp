#pragma once
#include <cstdint>

// Public API of the library target built from src/lib/.
//
// This header is how the default build proves its own wiring. An auto-discovered
// test in test/<name>/ can include it, call a function that is *compiled into the
// archive*, and link — with no CMakeLists.txt of its own. The include directory
// arrives as a usage requirement of the library target; the symbol arrives from
// the archive. Break either half and the example tests fail to link, rather than
// passing while silently testing nothing.
//
// That distinction is why this header exists at all: the generated <version.hpp>
// is entirely constexpr, so a test that uses only it compiles and passes even
// when the library is switched off. Only a declaration-here/definition-there
// function proves the link.
//
// A project bootstrapped from this template replaces the declarations below with
// its own API and renames the namespace to match (see NEW_PROJECT.md, Step 2).

namespace template_lib {

// The project name fixed at configure time, as a NUL-terminated string.
// Declared here, defined in the library's translation unit — consumers include a
// header, never a .cpp.
auto version_string() -> const char*;

// Is the build at least the given version? Compared component-major-first, so a
// larger minor never rescues a smaller major.
//
// This is the worked example of an API worth writing a failure matrix against:
// the interesting cases are the boundaries (exact equality, one below, one above,
// per component) and precedence between components — not the happy path. See
// test/10example/.
auto version_at_least(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) -> bool;

}  // namespace template_lib
