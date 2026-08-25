# ADR 0011: bounded static WebP decoding

- Status: accepted
- Date: 2026-08-25
- Issue: [RF-04b](https://github.com/gobha-me/rasterforge/issues/17)
- Supersedes: [ADR 0009](0009-defer-webp-until-demand-or-capacity.md) for static WebP

## Context

ADR 0009 deferred WebP until an intended consumer established a representative
need or a bounded implementation spike proved the safety and packaging
contract. AIForge's Venice media path now needs to accept provider-delivered
WebP without negotiating every response to PNG or JPEG, satisfying that
reopening condition.

WebP adds a third hostile-input codec and a RIFF container. RasterForge must
classify structure and metadata without unbounded reads, reject animation
rather than silently selecting one frame, preserve straight-alpha RGBA, and
keep installed static-library consumers complete. libwebp exposes direct decode
into caller-owned output but no per-operation allocator callback for measuring
all internal allocations.

## Decision

RasterForge recognizes the 12-byte `RIFF`/`WEBP` signature and supports static
lossy and lossless WebP through libwebp's decoder-only API. `ImageFormat::webp`
has the stable value 3. Decode writes `MODE_RGBA` directly into a validated,
tightly packed RasterForge image, disables codec threading and dithering, and
preserves straight alpha, including RGB values in fully transparent pixels.

Before feature inspection or decode, RasterForge scans the declared RIFF extent
with checked chunk arithmetic. A declared extent beyond the input is
`truncated_data`; invalid chunk framing is `malformed_data`. The scanner parses
WebP `EXIF` chunks through the codec-neutral orientation parser, accepting
either raw TIFF or an `Exif` identifier prefix. Duplicate or invalid optional
EXIF is reported as ignored. ICC and XMP metadata are not converted; pixels
are treated as sRGB consistently with the existing codecs.

Animated WebP is `unsupported_feature`. RasterForge will not return the first
frame as a successful still decode. A future animation implementation must use
the codec-neutral frame, timing, disposal, loop, and cumulative-limit model
defined by ADR 0009.

libwebp 1.3.2 is the minimum system version. CMake prefers a system
`WebP::webpdecoder` target and otherwise builds the official libwebp 1.6.0
archive pinned by SHA-256
`e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`.
The fallback disables tools, mux, animation utilities, threading, and SIMD,
stages only the decoder archive and public decode headers, and installs enough
metadata for `find_dependency(WebP 1.3.2 MODULE)` to reconstruct the target.

Because libwebp has no allocator hook, decode validates this conservative
working-storage reservation before allocating output:

```text
1 MiB + 8 * encoded_width * encoded_height + 32 * encoded_size
```

All terms use checked 64-bit arithmetic. The input span and final output remain
governed by their existing independent limits. Non-identity orientation also
charges the codec-native RGBA image while the normalized destination coexists.
An internal libwebp out-of-memory result within the reservation is
`allocation_failure`; exceeding the reservation limit is `resource_limit`.

libwebp status values map to stable RasterForge categories: insufficient data
is `truncated_data`, bitstream errors are `malformed_data`, unsupported features
are `unsupported_feature`, and unmapped failures are `codec_failure`.
Diagnostics remain silent and bounded. Decoder state is per operation, codec
threads are disabled, and distinct decode calls may run concurrently.

## Consequences

RasterForge accepts provider-delivered static WebP with the same checked output,
orientation, error, and package-consumer contract as PNG and JPEG. Lossy output
is tested with channel tolerances; lossless alpha and orientation transforms are
exact. Malformed RIFF framing, truncation, animation, every public limit,
concurrency, and reproducible fuzz seeds extend the failure matrix.

The conservative reserve can reject a decode that libwebp might complete with
less working memory. This is intentional until upstream exposes measurable
per-operation allocation. RasterForge also acquires libwebp security and pin
maintenance and still does not provide profile conversion or animation.
