# ADR 0003: PNG decoding normalizes deterministic straight-alpha RGBA8

- Status: accepted
- Date: 2026-08-07
- Issue: [RF-02c](https://github.com/gobha-me/rasterforge/issues/7)

## Context

ADR 0001 selected libpng and ADR 0002 established the byte-only public decode
boundary. The production adapter must now convert every ordinary static PNG
pixel layout into RasterForge's owned RGBA model without exposing libpng's
error, allocation, metadata, or lifetime rules.

## Decision

Each decode owns distinct libpng read and info structures. Input comes only
from the supplied byte span. Warnings are discarded and fatal errors update a
small per-operation state before jumping to a narrow frame containing no
non-trivial C++ automatic objects. Image ownership and `std::expected` values
remain outside those frames, so a codec jump cannot bypass their lifetimes.

Palette, low-bit grayscale, grayscale-alpha, `tRNS`, RGB, RGBA, 16-bit, and
Adam7 inputs normalize to tightly packed 8-bit RGBA. Sixteen-bit samples use
the high byte. RGB bytes are preserved when alpha is zero; the public result is
straight alpha. `has_alpha()` is true when the encoded color type carries
alpha or a valid `tRNS` chunk supplies it.

RF-02c treats decoded sample values as sRGB without applying ICC, gamma, or
chromaticity metadata. It does not interpret PNG EXIF orientation, so PNG
results report `not_present`. Color conversion and orientation normalization
remain the separately tracked RF-04d and RF-04c decisions.

Input size and option validation retain ADR 0002's precedence. A bounded IHDR
read rejects zero or over-limit dimensions before libpng work. Libpng receives
the caller's dimension limit and an individual chunk-allocation ceiling derived
from the input-byte limit. `Image::create` validates pixel and output-byte
limits before IDAT rows are decoded. Exact aggregate codec-allocation and work
accounting remains RF-02d rather than being implied here.

An exhausted span is `truncated_data`; corrupt standard PNG data is
`malformed_data`; an unknown critical chunk is `unsupported_feature`; memory
failure is `allocation_failure`; and an impossible post-transform layout is
`codec_failure`. Classification never depends on libpng message text, and
partial rows never become a successful result.

## Consequences

Supported PNG inputs produce exact RGBA bytes on GCC and Clang, and independent
decode contexts can run concurrently. Unknown ancillary metadata is discarded
instead of retained in codec structures. The explicit color, orientation, and
aggregate-budget limitations must remain visible until their owner issues land.
