# ADR 0008: libjpeg-turbo provides bounded static JPEG decoding

- Status: accepted
- Date: 2026-08-11
- Issue: [RF-04a](https://github.com/gobha-me/rasterforge/issues/16)

## Context

RasterForge needs baseline and progressive JPEG decoding behind the same
byte-span, owned-RGBA, structured-error boundary as PNG. The adapter must
silence codec diagnostics, reject partial output, avoid filesystem-backed
working storage, and preserve installable static-package consumption. JPEG's
lossy reconstruction also needs a narrower determinism claim than RasterForge's
integer pixel transforms.

Libjpeg-turbo was the leading candidate recorded in ADR 0001. It is widely
packaged, maintains the libjpeg API, supports progressive input and direct RGBA
output, and its no-backing-store memory implementation fails rather than
opening temporary files. Upstream deliberately does not support
`add_subdirectory`, so its fallback integration needs an isolated build.

## Decision

RasterForge requires libjpeg-turbo 2.1.5 or newer and pins the official 3.2.0
source tarball, including its SHA-256 digest, for fallback builds. System
discovery verifies the libjpeg-turbo version rather than accepting an arbitrary
IJG-compatible library. FetchContent owns fallback acquisition; a CMake
ExternalProject builds only the static libjpeg API with shared libraries,
tools, tests, TurboJPEG API, SIMD, and arithmetic coding disabled. When
RasterForge installs a fetched static dependency, it installs the archive,
headers, license, and a package finder that recreates `JPEGTurbo::JPEG` for
downstream consumers.

The public format enum adds `jpeg = 2`; `png = 1` remains unchanged. Detection
uses the two-byte JPEG SOI signature while retaining the existing maximum
eight-byte detection prefix. A single matching byte remains `truncated_data`;
a complete SOI reaches the JPEG adapter.

The adapter accepts 8-bit baseline or progressive grayscale, RGB, and YCbCr
input, including ordinary chroma-subsampling layouts. It converts directly to
tightly packed RGBA with alpha 255. CMYK/YCCK, arithmetic-coded, lossless,
non-8-bit, and other unsupported JPEG processes return `unsupported_feature`.
JPEG reports `has_alpha=false`, preserves its encoded extent, and reports
orientation `not_present`. EXIF orientation and ICC behavior remain owned by
RF-04c and RF-04d; markers are not retained or applied here, and decoded values
continue to be treated as sRGB.

Each call owns a separate decompressor and error manager. Input comes only from
`jpeg_mem_src`; stdio entry points are forbidden. Codec messages are never
formatted or written. Fatal errors and warnings jump only to narrow frames
containing trivial automatic state, then map numeric message codes to stable
RasterForge categories. Any warning fails the operation and the owned image is
discarded, so recovered or partial pixels never become success.

Dimensions, pixels, RGBA row bytes, and output storage are checked after the
header and before pixel allocation. JPEG charges a deterministic conservative
control/scanline reservation derived from output width against
`max_temporary_bytes`; progressive coefficient arrays are additionally subject
to libjpeg-turbo's per-context in-memory ceiling. Its no-backing-store port
turns an insufficient ceiling into `resource_limit` instead of filesystem I/O.
This differs from PNG's exact cumulative allocator requests but remains an
explicit, inclusive, testable per-operation contract.

JPEG reconstruction uses the slow integer DCT and disables progressive block
smoothing. RasterForge tests exact bytes for tiny invariant fixtures, but does
not promise identical lossy reconstruction across every supported
libjpeg-turbo version or future optimized backend. Public layout, metadata,
alpha, and error behavior are stable.

## Consequences

PNG and JPEG now share one byte-only public decode shape without codec handles
or filenames. Independent JPEG calls have no shared mutable state and cannot
spill work to disk. Fetched builds are slower because upstream must be built as
an isolated project, but the system-first path remains cheap and both paths are
verified as installed static packages.

Arithmetic JPEG is rejected even when a system codec could decode it, keeping
system and fallback behavior aligned. Orientation and color-profile limitations
remain explicit follow-on work rather than accidental codec behavior.
