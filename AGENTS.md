# AGENTS.md — conventions for AI agents working in this repo

If you're an LLM (or an LLM-driven editor) about to make changes here, read
this first. This is a **C++ project template** — a copy/paste starter. Changes
here should keep it generic, buildable, and useful as a teaching scaffold for
the projects that get bootstrapped from it.

## What this repo is

A CMake-based C++ starter kit. Its job is to give a *new* C++ project a working
build, test harness, toolchain options, and dependency pattern out of the box —
with opinionated-but-swap-able defaults, not a finished application. When you
edit it, optimize for "does this help the next project start faster and
correctly," not for any one downstream project.

## Current baseline (keep these in sync if you change them)

- **CMake ≥ 3.28**, **C++23** (GCC 13+ / Clang 19+ — see the Clang note under
  "How to verify a change" for why 19, not 17).
- **Compiler respects the environment** by default. Do **not** re-introduce a
  forced compiler in `cmake/toolchain/default.cmake`. Prefer clang? That's what
  `cmake/toolchain/clang.cmake` is for (opt-in, like the sanitizer toolchains).
- **Catch2 v3** for tests, fetched via `FetchContent` (see `cmake/deps/`).
- Dependencies: `find_package` first, `FetchContent` fallback, **100% CMake**
  (no conan/vcpkg). Keep it that way unless the maintainer asks.
- **Deps are opt-in via a list, not the filesystem.** A recipe in `cmake/deps/`
  is fetched only if its name is in `${PROJECT_NAME}_DEPS` in the root
  `CMakeLists.txt`. Dropping a file in `cmake/deps/` does **not** activate it;
  adding a dep means a recipe file **and** a line in that list. The two
  `list(REMOVE_ITEM …)` blocks under that list drop deps whose only consumer is
  a component that is switched off — they subtract from the list, never add, so
  the list stays the single prune point.
- **The library installs and exports — at install time only.** `cmake/install.cmake`
  generates the package config, the target export set and the header install;
  the exported target must keep spelling `${PROJECT_NAME}::lib`, identical to
  the in-tree `ALIAS`, and must keep working for **both** library variants
  without an edit (it detects the target type). **Do not add an `export(EXPORT …)`
  build-tree export back.** One was removed (#29) because it cannot be copied
  into a fork whose library links a dependency that ships no build export set —
  generation stops with "not in any export set", and the condition cannot be
  guarded for, because link interfaces are generator expressions that do not
  resolve until after all CMake code has run. The file says this at length;
  read it before reintroducing the feature.
- **A fetched dependency's install rules follow where it is linked**, not a
  fixed default. Private to the executable or the tests → `<DEP>_INSTALL OFF`
  (the `FMT_INSTALL` note in `cmake/deps/fmtlib.cmake`). Linked into the
  library → `set(<DEP>_INSTALL ${${PROJECT_NAME}_INSTALL})`, plus a
  `find_dependency()` line in `cmake/project-config.cmake.in`; a fixed `OFF`
  there breaks `install(EXPORT)`, and leaving it alone leaks the dependency into
  a consumer's prefix. Visibility is *not* the test — a PRIVATE link into a
  static library reaches the exported target too. Written up in
  `cmake/deps/catch2.cmake`; proved by `example/public-dep/`.

## Conventions that matter here

- **Toolchains are opt-in files** in `cmake/toolchain/`: `default.cmake`
  (respects env), `clang.cmake`, `address.cmake`, `thread.cmake`,
  `undefined.cmake`. To add a configuration, add a file that `include()`s
  `default.cmake` and layers its flags — don't edit `default.cmake` to force a
  specific setup.
- **Library pattern** in `src/lib/`: a compiled `STATIC` lib by default
  (toggle `${PROJECT_NAME}_BUILD_LIB`), public API in `include/lib.hpp`, with the
  header-only (`INTERFACE`) variant shown commented. Flipping to `INTERFACE`
  means every function in that header has to become an `inline` definition, or
  nothing that calls it links. Keep both patterns present and buildable — the
  template teaches by having both. That is a rule for *this* repo; a project
  bootstrapped from it picks one and deletes the other (see `NEW_PROJECT.md`).
- **Consumer-clean is a rule, not a nicety.** This project has to keep working
  when it is *not* the top-level one. Concretely:
  - Never `CMAKE_SOURCE_DIR` / `CMAKE_PROJECT_VERSION` / `CMAKE_PROJECT_NAME` —
    the `CMAKE_`-prefixed forms describe the top-level build, which belongs to
    someone else the moment we are consumed. Use `PROJECT_SOURCE_DIR`,
    `PROJECT_VERSION`, `PROJECT_NAME`.
  - A new `option()` defaults to `${PROJECT_IS_TOP_LEVEL}` unless it gates the
    library itself. Anything that builds an application, registers tests, or
    writes install rules is the consumer's business, not ours.
  - Public include directories are always
    `$<BUILD_INTERFACE:…>` / `$<INSTALL_INTERFACE:…>` genexes. A bare source
    path in a `PUBLIC` include directory makes `install(EXPORT)` fail at
    generate time — that failure is the feature, not the bug.
  - Anything the public header needs in order to compile (the C++ standard,
    a public dependency) travels on the target via `target_compile_features` /
    `PUBLIC` links. A consumer uses their own toolchain, so
    `cmake/toolchain/default.cmake` reaches them not at all.
  - `example/consumer/verify.sh` is what proves all of this; it is in "How to
    verify a change" below for that reason.
- **Tests are auto-discovered**: `test/CMakeLists.txt` loops over `test/*/`.
  A new test is just `test/<name>/test.cpp` (no CMakeLists needed); it gets
  `main()` from `test/main.cpp`, plus Catch2 and `${PROJECT_NAME}::lib` behind an
  `if (TARGET ...)` guard, so `-D<PROJECT>_BUILD_LIB=OFF` still configures. Add a
  `CMakeLists.txt` in the dir only if the test needs custom build control — that
  dir then owns its own wiring, the library link included. Directory names sort
  the glob, which sets registration order, not execution order; use fixtures or
  `DEPENDS` when order actually matters. After adding a test dir, re-run
  `cmake -B`.

## Testing philosophy (the important one)

**Test how code fails, not just that it produces the right output.** A
happy-path assertion (`REQUIRE(fun(10 / 5) == 2)`) only proves the code returns
what you already knew it returned, on input chosen because it works. The
valuable tests are the adversarial ones — bad input, boundaries, overflow,
malformed external data, error paths. Write the **failure matrix first**; the
happy-path check is the last, least-interesting test (a smoke check that the
harness runs). See `test/20failure-testing/` for the canonical example, and
`test/10example/` for the same discipline applied to this repo's own library
through its public header. `test/01example/` and `test/02example/` are
deliberately thin — they demonstrate discovery and custom build control, not how
to write a test.

## How to verify a change (do this before opening a PR)

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
# and cross-compiler, since the template supports both:
cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  && cmake --build build-clang && ctest --test-dir build-clang

# and, for anything touching the build's shape, the consumed path — which no
# ctest covers, because every test runs with this repo as the top-level project:
example/consumer/verify.sh
CXX=clang++ example/consumer/verify.sh

# and, for anything touching install/export or the dependency recipes, the path
# this project cannot exercise on its own — a fork whose library links a public
# dependency. One compiler is enough: it fails during generation, if it fails.
example/public-dep/verify.sh
```

Both must build clean and pass all tests. A change that only builds on one
compiler is not done. (This is how the fmt-under-clang-20 breakage was caught —
build on both, always.)

CI (`.github/workflows/ci.yml`) enforces this on every push and pull request:
GCC and Clang × {default, address, thread, undefined} toolchains, plus the
`library disabled`, `consumer` (×2 compilers), `public dependency` and
`version-parse-selftest` jobs.
A one-compiler change turns that compiler's jobs red, so the template can't rot
unnoticed — run the commands above locally first.

CI pins its Clang jobs to Clang 20: Ubuntu 24.04's stock Clang 18 cannot compile
the C++23 `std::expected` example (`test/20failure-testing`) against libstdc++ —
use Clang 19+ (libstdc++) or any Clang with libc++. Same class of compiler/stdlib
break as the fmt-under-clang-20 note above; CI surfaced it. If you develop with
Clang, verify with a version CI would accept, not just whatever `clang++` resolves to.

**The same applies to GCC, and it is the easier one to get wrong**, because
`g++` on a dev box is usually *newer* than CI's, so a change can pass locally and
fail on the supported floor. CI runs GCC 13; `g++-13` is packaged alongside
`g++-14` on Ubuntu 24.04, so reach for `CXX=g++-13` before opening a PR that
touches language-level or usage-requirement behaviour. Worked example: C++23 mode
on GCC 13 reports `__cplusplus` as **202100L**, not 202302L (CMake selects
`-std=c++2b` there), so a `__cplusplus >= 202302L` check passes on GCC 14 and
Clang 20 and fails on the floor. Prefer a feature-test macro to a `__cplusplus`
comparison — see the note in `example/consumer/main.cpp`.

## Attribution

Follow the convention used across this org's repos: agent-authored commits
carry a trailer naming the model, e.g.

```
Co-authored-by: Kimi K3 (vcoder via Venice) <noreply@venice.ai>
Agent: vcoder / Kimi K3
```

and PRs note what was actually run to verify (per "How to verify" above).

## Notes for agents

- `include/version.hpp.in.cmake` is configured into `include/version.hpp` at
  build time; edit the `.in.cmake` source, not the generated file. If you touch
  it, keep the `#include <cstdint>` (std::uint32_t needs it).
- Version parsing is pure string logic in `cmake/version_parse.cmake`
  (`parse_git_describe`); `cmake/version.cmake` just runs `git describe` and calls
  it. If you change the parsing, add a row to and re-run the self-test:
  `cmake -P cmake/version_selftest.cmake` (also runs in ctest as
  `version-parse-selftest`). Failure-matrix-first, like the other tests.
- `NEW_PROJECT.md` is **fork-facing**: it instructs a project being bootstrapped
  out of this template, not this repo. Don't put template-maintenance rules in
  it, and don't let it drift from the file paths and line numbers it cites.
- `cmake/check_artifacts.cmake` looks for leftover template artifacts. It runs
  inverted here (ctest: `artifact-check-selftest`) — every Class-A rule must
  still MATCH something, because this repo legitimately contains all of them.
  Rename or delete an artifact that a rule targets and that rule matches
  nothing, the self-test goes red, and you update the rule to match. A fork that
  has deleted `NEW_PROJECT.md` runs the same script in plain enforcement mode
  instead. Class-B rules check wiring that can drift, are never inverted, and
  must stay green on both sides.
  **Never write one of the searched-for tokens into prose.** A rule counts hits
  across all tracked files, so a doc that quotes the token it is hunting keeps
  that rule green forever, whatever happened to the real artifact. The checker
  and `NEW_PROJECT.md` are excluded from the scan for exactly this reason; the
  fix anywhere else is to describe the token, not spell it.
- Build dirs (`build*/`) are gitignored — don't commit them.
- The dep pins in `cmake/deps/` are only audited when something breaks on a
  supported compiler; bump deliberately and say why in the commit.
