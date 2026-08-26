# RasterForge

[![CI](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/rasterforge/actions/workflows/ci.yml)

RasterForge is a C++23 library for safely turning untrusted, in-memory raster
media into predictable RGBA pixels. It owns decoding, resource limits, image
fit, resize, and compositing; it deliberately does not own filesystems,
networking, terminal protocols, or application presentation policy.

RasterForge decodes static PNG, JPEG, and WebP bytes into checked, tightly
packed straight-alpha sRGBA images, normalizes EXIF orientation when requested,
and fits image views to exact extents with
deterministic nearest-neighbor or alpha-correct triangle filtering. It provides
borrowing views, structured errors, caller-controlled limits, bounded signature
detection, and an installable CMake target. See
[the design](docs/DESIGN.md).

## Requirements

- CMake 3.28 or newer
- GCC 13+ or Clang 19+
- A C++23 standard library with `std::expected`
- Libpng 1.6.31+ when using a system package
- libjpeg-turbo 2.1.5+ when using a system package
- libwebp 1.3.2+ when using a system package

The library links libpng, zlib, libjpeg-turbo, and libwebp's decoder-only
library privately; Catch2 is used only when building tests. CMake looks for
system packages first and falls back to pinned source builds. Set
`rasterforge_FORCE_FETCH_DEPS=ON` to exercise the pinned path explicitly.

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

Clang/libFuzzer coverage for signature detection, PNG, JPEG, and WebP decoding is
available as an explicit, bounded developer target. It is excluded from normal
builds and CTest; see [the fuzzing guide](fuzz/README.md) for the corpus and
smoke command.

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

  auto fitted = rasterforge::fit(pixels, {640, 480},
                                 rasterforge::Fit::contain,
                                 {}, {0, 0, 0, 0}, {},
                                 rasterforge::ResizeFilter::triangle);
  if (!fitted) {
    return;
  }

  auto composited = rasterforge::composite_over(
      fitted->view(), rasterforge::Rgba8{24, 24, 24, 255});
  if (!composited) {
    return;
  }
}
```

`fit` supports contain, cover, stretch, and no-scale policies. It allocates the
exact destination through `Image::create`, fills uncovered pixels with the
caller matte, and samples at pixel centers. Nearest is the default and copies
complete RGBA bytes, including RGB values in fully transparent pixels; see
[ADR 0006](docs/adr/0006-nearest-fit-sampling.md).

`ResizeFilter::triangle` uses exact integer-rational weights with widened
support for reductions. It filters exact premultiplied sRGBA channel products,
then returns straight-alpha pixels. This prevents colored transparent samples
from creating fringes; a fully transparent filtered result is canonicalized to
transparent black. Filtering remains gamma-encoded rather than linear-light.
Coefficient storage is checked against `max_temporary_bytes` before output is
allocated. See [ADR 0007](docs/adr/0007-scale-adaptive-triangle-filter.md).

`composite_over` applies a source view over either a same-extent image view or
one uniform `Rgba8` backdrop and returns a separately owned image. The operation
uses deterministic Porter-Duff source-over math on exact premultiplied
gamma-encoded sRGBA products, then converts to straight alpha with half-up
integer rounding. A zero-alpha result is transparent black; transparent source
colors cannot leak into the backdrop. Inputs are read-only and may alias, while
image backdrop extents must match. See
[ADR 0013](docs/adr/0013-deterministic-source-over-compositing.md).

| Format | Current behavior |
| --- | --- |
| PNG | Static RGB, RGBA, grayscale, palette/`tRNS`, 16-bit, Adam7, and `eXIf` orientation |
| JPEG | Baseline/progressive 8-bit grayscale and RGB/YCbCr decode; opaque RGBA output and APP1 orientation |
| WebP | Static lossy and lossless decode, straight alpha, and EXIF orientation; animation is rejected |

PNG, JPEG, and WebP sample values are interpreted as sRGB whether color
metadata is missing or present. RasterForge does not validate, convert,
preserve, or expose ICC profiles, PNG gamma/chromaticity records, or WebP XMP;
the maximum retained color metadata is zero bytes. This is a working-space
interpretation, not a color-management promise. See
[ADR 0012](docs/adr/0012-ignore-color-metadata.md). WebP's codec, animation,
and resource decisions remain recorded in
[ADR 0011](docs/adr/0011-bounded-static-webp-decoding.md).
EXIF orientation is exposed generically and normalized by default; malformed
optional orientation metadata is reported and ignored. See
[ADR 0010](docs/adr/0010-bounded-exif-orientation.md). The shared animation
boundary for GIF, animated WebP, and APNG remains recorded in
[ADR 0009](docs/adr/0009-defer-webp-until-demand-or-capacity.md).
TermForge v0.56.0 can register, place, and control a pre-baked sequence of
borrowed raw or encoded frames on terminals that prove Kitty animation support.
That downstream capability makes a future RasterForge animation decoder easier
to integrate, but does not change the current rejection contract or move codec,
disposal, loop-count, and cumulative-limit policy into TermForge.

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
| `invalid_argument` | A public option is unrecognized or image arguments are incompatible. |
| `row_out_of_range` | A requested image-view row is outside the image extent. |

### Resource limits

Every limit is inclusive: an input, decoded layout, or fit destination exactly
at its configured limit is accepted. Callers can replace defaults through
`DecodeOptions`, `Image::create`, or `fit`.

| Limit | Default | Accounted resource |
| --- | ---: | --- |
| `max_input_bytes` | 32 MiB | Caller-owned encoded byte span |
| `max_dimension` | 16,384 | Each encoded and orientation-normalized axis |
| `max_pixels` | 64 Mi pixels | Pixels in the normalized output extent |
| `max_output_bytes` | 256 MiB | Final tightly packed RGBA image storage |
| `max_temporary_bytes` | 64 MiB | Codec work, orientation source storage, or quality-filter coefficients |

The temporary budget excludes the input span and final image, which have their
own limits. PNG accounts cumulative successful allocation requests; JPEG
charges checked control/scanline work and caps progressive coefficient storage;
WebP reserves `1 MiB + 8 * pixels + 32 * encoded bytes` before output allocation
because libwebp has no per-operation allocator hook; quality fitting accounts
the exact payload of its coefficient vectors. Budget exhaustion is
`resource_limit`; non-identity orientation also charges the codec-native RGBA
image while the normalized destination coexists. An allocator returning null within budget is
`allocation_failure`. These codec-specific semantics are recorded in
[ADR 0004](docs/adr/0004-decode-resource-accounting.md) and
[ADR 0008](docs/adr/0008-libjpeg-turbo-jpeg-decoding.md),
[ADR 0011](docs/adr/0011-bounded-static-webp-decoding.md), with orientation's
phase boundary recorded in [ADR 0010](docs/adr/0010-bounded-exif-orientation.md).
Source-over compositing allocates only its final image, so it charges the
dimension, pixel, and output-byte limits but not the input or temporary limits.

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
- Keep codec state and libpng/libjpeg-turbo/libwebp dependencies behind the
  RasterForge boundary.
- Write adversarial and boundary tests before happy-path smoke checks.
- Preserve clean consumption through `add_subdirectory`, FetchContent, and
  installed `find_package` packages.

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture, non-goals, security
contract, and milestone plan.

## License

BSD 3-Clause. See [LICENSE.md](LICENSE.md).
