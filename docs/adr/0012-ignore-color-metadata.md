# ADR 0012: decoded samples are interpreted as sRGB without color conversion

- Status: accepted
- Date: 2026-08-25
- Issue: [RF-04d](https://github.com/gobha-me/rasterforge/issues/19)

## Context

RasterForge exposes row-major, 8-bit, straight-alpha sRGBA pixels, but its PNG,
JPEG, and WebP adapters have not defined whether that label describes a color
conversion or an interpretation of codec output. Codec defaults must not become
an accidental color-management promise, and hostile profiles must not create an
unbounded metadata or decompression path.

## Decision

Decoded RGB sample values are interpreted as sRGB. When source color metadata
is absent, RasterForge assumes sRGB. When it is present, RasterForge does not
validate, convert, preserve, or expose it. The maximum retained color-metadata
payload in `DecodedImage` is therefore zero bytes, and no public API is added.

This applies consistently to PNG `iCCP`, `gAMA`, `cHRM`, `sRGB`, and newer
colorimetry chunks; JPEG APP2 ICC marker sequences; and WebP `ICCP`. WebP XMP is
also ignored as non-pixel semantic metadata. PNG routes recognized color chunks
through its bounded discard path before libpng can decompress or interpret
them. JPEG does not register or save APP2 markers. WebP validates RIFF framing
but does not parse ICC or XMP payload contents.

Well-framed metadata with invalid or incomplete profile contents is ignored and
does not change decoded pixels. This tolerance does not hide corruption in the
surrounding image container: PNG chunk framing and ordering, JPEG marker
framing, WebP RIFF framing, and pixel payloads continue to follow each codec's
existing validation and stable error contract.

All metadata remains part of the caller-owned encoded span and counts toward
`max_input_bytes`. Temporary codec allocations remain subject to
`max_temporary_bytes`; no independent retained-metadata budget is needed because
the retained size is always zero.

The sRGBA label is therefore an explicit working-space interpretation, not
evidence that RasterForge honored a source profile or performed a colorimetric
conversion. Fit and compositing operations consume those interpreted sRGBA
values according to their separately documented transfer-function contracts.

## Consequences

Decode behavior no longer depends on whether a supported codec happens to parse
or preserve a color record. Profile-aware applications must perform color
management before or after RasterForge through an explicit component and cannot
recover source profiles from `DecodedImage`. This is intentionally narrower
than color-management perfection and keeps the core dependency and resource
contract unchanged.
