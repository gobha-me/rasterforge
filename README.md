# RasterForge

[![CI](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml)

RasterForge is a C++23 library for safely turning untrusted, in-memory raster
media into predictable RGBA pixels. It owns decoding, resource limits, image
fit, resize, and compositing; it deliberately does not own filesystems,
networking, terminal protocols, or application presentation policy.

The bootstrap release establishes the core contracts: checked image creation,
tightly packed straight-alpha sRGBA pixels, owned images, borrowing views,
structured errors, caller-controlled limits, and an installable CMake target.
PNG/JPEG decoding and transforms are planned next; see [the design](docs/DESIGN.md)
and the GitHub roadmap issues.

## Requirements

- CMake 3.28 or newer
- GCC 13+ or Clang 19+
- A C++23 standard library with `std::expected`

The library links libpng and zlib privately; Catch2 is used only when building
tests. CMake looks for system packages first and falls back to pinned
FetchContent checkouts. Set `rasterforge_FORCE_FETCH_DEPS=ON` to exercise the
pinned path explicitly.

## Build and test

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake
cmake --build build-clang --parallel
ctest --test-dir build-clang --output-on-failure
```

The `rasterforge` executable is intentionally small; it exposes build metadata
for packaging checks:

```bash
./build/src/bin/rasterforge --version
```

## Use the library

```cpp
#include <rasterforge/rasterforge.hpp>

auto image = rasterforge::Image::create({640, 480});
if (!image) {
  // image.error().code is stable program logic; message is diagnostic text.
}
```

Link the same target whether RasterForge is vendored, fetched, or installed:

```cmake
find_package(rasterforge CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE rasterforge::lib)
```

To install a library-only package:

```bash
cmake -B build-install \
  -Drasterforge_BUILD_BIN=OFF -Drasterforge_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/desired/prefix
cmake --build build-install --parallel
cmake --install build-install
```

The public headers install under `include/rasterforge/`; the package config is
installed under `lib/cmake/rasterforge/`.

## Design constraints

- Encoded data is untrusted; reject invalid dimensions and resource-limit
  violations before allocation.
- Public pixels are row-major 8-bit sRGBA with straight alpha.
- Malformed data is returned as `std::expected` errors, never process aborts.
- Keep codec state and dependencies behind the RasterForge boundary.
- Write adversarial and boundary tests before happy-path smoke checks.
- Preserve clean consumption through `add_subdirectory`, FetchContent, and
  installed `find_package` packages.

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture, non-goals, security
contract, and milestone plan.

## License

BSD 3-Clause. See [LICENSE.md](LICENSE.md).
