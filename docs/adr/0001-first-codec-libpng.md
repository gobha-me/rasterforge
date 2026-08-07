# ADR 0001: libpng is the first production codec

- Status: accepted
- Date: 2026-08-07
- Issue: [RF-02a](https://github.com/gobha-me/rasterforge/issues/5)

## Context

RasterForge needs one production decoder behind its byte-span API before the
generic decode boundary and resource enforcement can be proven against a real
codec. The first consumer path produces PNG bytes; JPEG remains useful, but is
already isolated as [RF-04a](https://github.com/gobha-me/rasterforge/issues/16).
The codec must tolerate hostile input, expose allocation and diagnostic hooks,
work without global mutable state, and survive static CMake package exports.

## Decision

Use libpng 1.6 for PNG-only decoding in RF-02. Prefer a maintained system
package and pin the FetchContent fallback to v1.6.58, with zlib pinned to
v1.3.2 when it also needs to be fetched.

Each decode will own distinct `png_struct` and `png_info` instances. The adapter
will use `png_create_read_struct_2()` for a per-operation bounded allocator,
`png_set_user_limits()` for dimensions, and the chunk allocation/cache limits
before reading image data. RasterForge will allocate the final RGBA buffer only
after validating the complete IHDR-derived size with checked arithmetic.

The default libpng error and warning handlers are forbidden because they may
write diagnostics. The adapter will install silent callbacks and contain
libpng's fatal-error `longjmp` inside a narrow C-compatible frame. No C++ object
with a non-trivial lifetime may be created or modified between `setjmp` and a
libpng call that can jump. Codec errors become stable RasterForge error codes;
partial pixels never become success.

PNG output will ultimately normalize to the existing row-major, 8-bit,
straight-alpha sRGBA contract. Palette and transparency expansion, color
metadata policy, orientation metadata, default budgets, and public decode types
remain decisions for their dedicated RF-02/RF-04 issues; the dependency spike
does not pre-empt those APIs.

## Alternatives

- libspng has a smaller return-code API, explicit image/chunk limits, and
  continuous fuzzing. Its allocator callbacks have no caller context for clean
  per-decode accounting, its latest release is older, and its CMake install
  rules are harder to suppress safely in embedded builds.
- Wuffs offers the strongest memory-safe, caller-buffer-oriented model, but it
  has no conventional system package plus CMake export path and would make
  RasterForge own a substantial integration layer.
- stb_image and LodePNG are easy to embed, but their package/export story,
  allocation containment, metadata access, and vulnerability-response model do
  not satisfy this library's boundary.
- Shipping JPEG at the same time would widen the first safety milestone without
  unblocking its PNG consumer. libjpeg-turbo remains the leading candidate for
  the separately tracked JPEG issue.

## Consequences and known risks

Libpng documents independent contexts as thread-safe and exposes the required
memory, dimension, chunk, metadata, and error hooks. It is widely packaged and
actively fixes fuzz- and security-discovered defects. That history also shows
that codec defects remain possible: system-package users must consume vendor
security updates, while RasterForge must deliberately review and bump its pin.

The `setjmp` boundary is the principal integration risk. Decoder work may not
use libpng's aborting configuration, default diagnostic handlers, filesystem
functions, or shared codec objects. The decoder and limit issues must test those
constraints under sanitizers and malformed corpora before decode is released.
