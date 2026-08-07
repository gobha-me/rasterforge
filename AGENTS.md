# AGENTS.md — RasterForge contributor conventions

RasterForge is a C++23 media-safety library. Changes should keep its core
independent of filesystems, networking, terminal protocols, UI semantics, and
provider APIs. Treat encoded images as hostile input and preserve an explicit,
testable resource contract.

## Product boundary

- RasterForge owns validated decode, orientation, crop/fit/resize, pixel
  transforms, compositing, and caller-supplied limits.
- Callers own byte acquisition, caching policy, semantic layout, and display.
- Public pixels are row-major, 8-bit, straight-alpha sRGBA. Internal algorithms
  may use premultiplied linear values when their names and conversions are clear.
- Errors cross the public boundary through `std::expected`. No decode path may
  terminate the process, write diagnostics, or report partial output as success.
- The core API consumes byte spans. Do not introduce implicit filesystem access.

Read [docs/DESIGN.md](docs/DESIGN.md) before changing the public model or adding
a codec. Record codec, alpha/color, limit, filter, determinism, and exception
decisions as short ADRs under `docs/adr/`.

## Build baseline

- CMake 3.28+, C++23, GCC 13+, and Clang 19+.
- The compiler follows the environment by default. Toolchains in
  `cmake/toolchain/` are opt-in layers; do not force a compiler in the default.
- Dependencies remain 100% CMake: `find_package` first, pinned FetchContent
  fallback. Add a recipe and list it in `${PROJECT_NAME}_DEPS`; a recipe file by
  itself must remain inert.
- Catch2 v3 is test-only. Dependencies used only by the app or tests must not
  install into RasterForge's prefix.
- The compiled library target is `rasterforge::lib` in-tree and when installed.

If a dependency links into the static library, even `PRIVATE`, its fetched
install option must follow `${${PROJECT_NAME}_INSTALL}` and the installed
package config must call `find_dependency()`. Keep the public-dependency harness
green; do not add a build-tree `export(EXPORT ...)` because many dependencies
provide only install export sets.

## Public API and safety rules

- Public headers live under `include/rasterforge/` and use
  `$<BUILD_INTERFACE:...>` / `$<INSTALL_INTERFACE:...>` include paths.
- Anything public headers require, including C++23 and public dependencies,
  travels on the target as usage requirements.
- Image dimensions, stride, and storage length must not be independently
  constructible into disagreement.
- Validate dimensions, pixel counts, strides, byte counts, and temporary
  budgets with checked arithmetic before allocating.
- Stable error codes are program logic. Diagnostic messages are not API keys.
- Avoid shared global codec state and unbounded caches. Pure operations on
  distinct outputs should be safe to run concurrently.
- Do not claim cancellation until codecs and inner loops actually observe it.

## Consumer cleanliness

RasterForge must remain well-behaved through `add_subdirectory`, FetchContent,
and installed `find_package`:

- Use `PROJECT_SOURCE_DIR`, `PROJECT_VERSION`, and `PROJECT_NAME`, never the
  top-level `CMAKE_` variants for project-local facts.
- Options that build the app, tests, install rules, fuzzers, benchmarks, or
  examples default to `${PROJECT_IS_TOP_LEVEL}`.
- A consuming build gets the library only; it must not fetch Catch2 or create
  RasterForge's executable/tests.
- Package exports are install-tree only. Side-by-side development uses
  `add_subdirectory`.

Run `example/consumer/verify.sh` after build-shape, usage-requirement, or install
changes. Run `example/public-dep/verify.sh` after dependency/export changes.

## Testing philosophy

Write the failure matrix first. For image work, cover malformed/truncated input,
zero and extreme dimensions, arithmetic overflow, each configured limit, short
storage, bad rows/strides, transparent colored pixels, focal endpoints, and
allocation failures where injection is practical. A normal decode or resize is
the final smoke test, not the center of the suite.

Tests are auto-discovered from `test/*/`. A directory containing `test.cpp`
needs no CMake file and automatically links Catch2 and `rasterforge::lib`.
Re-run CMake after adding a directory. Keep `test/30sanitizer-smoke/` and its
matching guard in `test/CMakeLists.txt` synchronized.

Store tiny generated or hand-built fixtures where possible. Add every
fuzz-discovered crash to the regression corpus. Exact golden bytes are suitable
only for algorithms whose determinism is part of the contract.

## Verification

Before pushing a change, run:

```bash
cmake -B build && cmake --build build --parallel \
  && ctest --test-dir build --output-on-failure

cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  && cmake --build build-clang --parallel \
  && ctest --test-dir build-clang --output-on-failure

example/consumer/verify.sh
CXX=clang++ example/consumer/verify.sh
example/public-dep/verify.sh
cmake -P cmake/version_selftest.cmake
```

CI runs GCC and Clang across default, address, thread, and undefined-behavior
toolchains, plus library-disabled, consumer, public-dependency, artifact, and
version-parser checks. GCC 13 is the supported floor; prefer feature-test macros
over `__cplusplus` thresholds because GCC 13 reports C++23 mode as 202100L.

## Repository workflow

- Build directories are ignored; never commit them.
- `include/rasterforge/version.hpp.in.cmake` is the version source. Its generated
  header lives in the build tree; keep `<cstdint>` when editing its source.
- Version parsing lives in `cmake/version_parse.cmake`; add failure-matrix rows
  to `cmake/version_selftest.cmake` when it changes.
- Do not quote artifact-check search needles in ordinary prose. The checker
  scans tracked files and a quoted needle can hide the real artifact's removal.
- Dependency pins are bumped deliberately, with the compiler/security reason in
  the commit or pull request.
- Follow-on work should have one owner issue, explicit dependencies, adversarial
  acceptance cases, and verification commands. Epic issues coordinate outcomes;
  implementation issues should stay small enough for one focused session.

Agent-authored commits include:

```text
Co-authored-by: OpenAI Codex <noreply@openai.com>
Agent: Codex / GPT-5
```
