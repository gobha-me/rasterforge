# ADR 0002: the generic decode boundary is codec-neutral and byte-only

- Status: accepted
- Date: 2026-08-07
- Issue: [RF-02b](https://github.com/gobha-me/rasterforge/issues/6)

## Context

The libpng selection in ADR 0001 must not make public types depend on libpng or
make filenames, MIME hints, codec handles, and diagnostic behavior part of the
contract. RasterForge also needs to distinguish incomplete data that may become
a recognized format from bytes that have already disproved every supported
signature.

The API lands one issue before the production adapter so its ownership,
metadata, limits, error precedence, and orientation policy can be tested
without codec behavior obscuring those decisions.

## Decision

`decode()` accepts only `std::span<const std::byte>` and `DecodeOptions`. The
options contain caller-supplied `Limits` and an enum-valued orientation policy;
an enum makes apply versus ignore explicit and lets invalid values be rejected
as `invalid_argument`. Cancellation is not claimed or exposed yet.

`DecodedImage` is a move-only owner. It exposes the decoded `Image`, detected
format, encoded extent, output extent, alpha presence, and orientation status.
The output extent is derived from the owned image rather than stored
independently, so it cannot disagree with pixel storage. Construction remains
private to decoder implementation access. No codec handle crosses the public
boundary.

The output image keeps RasterForge's existing row-major, 8-bit, straight-alpha
sRGBA contract. `has_alpha()` reports whether the encoded source contributes
alpha semantics; it does not change the four-channel output representation.
`OrientationStatus` distinguishes absent metadata, an applied transform, and
metadata deliberately ignored by policy.

PNG is the only recognized format in RF-02. Detection compares at most the
documented eight-byte prefix. A non-empty proper prefix of the PNG signature is
`truncated_data`; a mismatch at any available signature byte is
`unsupported_format`. Bytes after a complete signature do not participate in
format classification. Adding another format may increase the documented
prefix bound and add enum values without adding MIME or filename inputs.

Input validation has deterministic precedence: empty input, inclusive
`max_input_bytes`, option validity, signature classification, then codec work.
The existing 32 MiB default input limit is retained. Dimension, pixel,
output-byte, and temporary-work enforcement remains mandatory when an adapter
can read headers or allocate; RF-02b performs neither. Errors and allocation
failures continue to cross the public boundary through `std::expected`, not
exceptions or diagnostics.

Until RF-02c supplies the adapter, a complete PNG signature returns
`unsupported_feature`. This makes the full API linkable and testable without
claiming a partial decode as success.

## Consequences

Callers can compile and link against the final ownership and option shape before
codec integration. Installed-package tests exercise the same signature/error
path as in-tree users. RF-02c can replace the temporary post-detection error
with a decoded owner while preserving input validation and public types.

Signature detection is intentionally conservative. Very short unknown inputs
are classified as unsupported as soon as a byte disproves PNG, but a matching
prefix remains truncated because more bytes could still identify it. New
formats will need cross-format prefix tests before the maximum bound changes.
