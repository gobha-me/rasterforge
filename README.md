# RasterForge

[![CI](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml)

RasterForge is a C++23 library for safely turning untrusted, in-memory raster
media into predictable RGBA pixels. It owns decoding, resource limits, image
fit, resize, and compositing; it deliberately does not own filesystems,
networking, terminal protocols, or application presentation policy.

The current release decodes static PNG bytes into checked, tightly packed
straight-alpha sRGBA images. It provides borrowing views, structured errors,
caller-controlled limits, bounded signature detection, and an installable
CMake target. JPEG decoding and transforms remain tracked in the roadmap; see
[the design](docs/DESIGN.md).

## Requirements

- CMake 3.28 or newer
- GCC 13+ or Clang 19+
- A C++23 standard library with `std::expected`

The library links libpng and zlib privately; Catch2 is used only when building
tests. CMake looks for system packages first and falls back to pinned
FetchContent checkouts. Set `rasterforge_FORCE_FETCH_DEPS=ON` to exercise the
pinned path explicitly.

Release source archives retain their tag-derived version without requiring a
Git checkout. An untagged source snapshot with neither repository nor archive
metadata deliberately reports version `0.0.0` instead of borrowing tags from an
unrelated enclosing repository.

## Build and test

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake
cmake --build build-clang --parallel
ctest --test-dir build-clang --output-on-failure
```

Clang/libFuzzer coverage for signature detection and PNG decoding is available
as an explicit, bounded developer target. It is excluded from normal builds and
CTest; see [the fuzzing guide](fuzz/README.md) for the corpus and smoke command.

The `rasterforge` executable is intentionally small; it exposes build metadata
for packaging checks:

```bash
./build/src/bin/rasterforge --version
```

## Use the library

```cpp
#include <rasterforge/rasterforge.hpp>

auto consume(std::span<const std::byte> encoded_bytes) -> void {
  auto decoded = rasterforge::decode(encoded_bytes);
  if (!decoded) {
    // decoded.error().code is stable program logic; message is diagnostic text.
    return;
  }

  auto pixels = decoded->view();
  // pixels is row-major, 8-bit, straight-alpha RGBA and borrows decoded.
}
```

| Format | Current behavior |
| --- | --- |
| PNG | Static RGB, RGBA, grayscale, palette/`tRNS`, 16-bit, and Adam7 decode |
| JPEG, WebP | `unsupported_format`; tracked for later milestones |

PNG samples are currently treated as sRGB without ICC/gamma conversion, and
PNG EXIF orientation is not yet interpreted. These deliberate limitations are
recorded in [ADR 0003](docs/adr/0003-png-decoder-normalization.md).

### Error contract

`Error::code` is stable program logic; `Error::message` is bounded diagnostic
text and is not an API key. Public operations currently return these categories:

| Code | Meaning |
| --- | --- |
| `empty_input` | The encoded byte span is empty. |
| `input_too_large` | The encoded span exceeds `max_input_bytes`. |
| `unsupported_format` | The bounded signature is not a supported format. |
| `malformed_data` | Recognized input violates the format structure. |
| `truncated_data` | Input ends within a recognized signature or required format data. |
| `invalid_dimensions` | An image dimension is zero or otherwise invalid. |
| `resource_limit` | A configured dimension, pixel, output, temporary, or representability bound was exceeded. |
| `allocation_failure` | Allocation failed without first exhausting a caller limit. |
| `unsupported_feature` | The format is recognized but uses an unsupported feature. |
| `codec_failure` | The codec failed without a more specific stable classification. |
| `invalid_argument` | A public option contains an unrecognized value. |
| `row_out_of_range` | A requested image-view row is outside the image extent. |

### Resource limits

Every limit is inclusive: an input or decoded layout exactly at its configured
limit is accepted. Callers can replace any default through `DecodeOptions`.

| Limit | Default | Accounted resource |
| --- | ---: | --- |
| `max_input_bytes` | 32 MiB | Caller-owned encoded byte span |
| `max_dimension` | 16,384 | Each encoded and orientation-normalized axis |
| `max_pixels` | 64 Mi pixels | Pixels in the normalized output extent |
| `max_output_bytes` | 256 MiB | Final tightly packed RGBA image storage |
| `max_temporary_bytes` | 64 MiB | Cumulative codec allocation requests |

The temporary budget excludes the input span and final image, which have their
own limits. Successful codec allocation requests consume it for the rest of the
decode even after memory is freed, bounding both temporary memory and allocation
churn. Budget exhaustion is `resource_limit`; an allocator returning null within
budget is `allocation_failure`.
These accounting semantics are recorded in
[ADR 0004](docs/adr/0004-decode-resource-accounting.md).

```cpp
rasterforge::DecodeOptions options{};
options.limits.max_input_bytes = 4ULL * 1024ULL * 1024ULL;
options.limits.max_temporary_bytes = 8ULL * 1024ULL * 1024ULL;
auto decoded = rasterforge::decode(encoded_bytes, options);
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
