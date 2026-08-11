# ADR 0007: Deterministic scale-adaptive triangle filtering

Status: accepted

## Context

Nearest-neighbor fitting establishes checked geometry and an exact sampling
convention, but a quality resize path must mix neighboring samples without
making compiler floating-point behavior part of the byte contract. Ordinary
two-tap bilinear sampling is useful when enlarging but aliases badly under
severe reduction. A scalar oracle also needs explicit edge, normalization,
temporary-storage, color, and cancellation decisions before an optimized or
alpha-aware implementation can be compared against it.

## Decision

- The first quality filter is a separable triangle filter. Its radius is one
  source pixel while enlarging and widens by the source-to-destination scale
  while reducing, so minification integrates a broader footprint instead of
  selecting only two source samples.
- Destination pixel centers use the same half-pixel coordinate convention as
  nearest fitting. Each axis is planned independently with exact integer-
  rational weights; floating-point coefficients and platform math libraries do
  not participate in output pixels.
- Support outside the selected source rectangle clamps to its nearest edge.
  Duplicate clamped samples are coalesced, every stored weight is positive, and
  each destination span retains its exact unnormalized weight sum.
- The scalar reference kernel is separable. It normalizes after the horizontal
  pass and again after the vertical pass, rounding an exact half toward the
  greater integer component value at each pass. Supported GCC and Clang builds
  therefore produce exact matching bytes; SIMD must reproduce this oracle
  before it can replace it.
- Coefficient spans and taps are the kernel's temporary storage. Their exact
  vector payload is calculated with checked arithmetic and compared with
  `max_temporary_bytes` before any vector allocation. The caller-owned input
  and output are excluded. Limit and representability failures return
  `resource_limit`; allocator failure returns `allocation_failure`; no partial
  destination is reported on a failing path.
- RGBA filtering uses the private, explicitly named
  `PremultipliedSrgbaProduct` representation. Each RGB component is the exact
  16-bit product of its straight channel and alpha, while alpha remains 8-bit;
  this avoids the information loss of first quantizing premultiplied RGB to a
  byte. All four components use the same separable coefficients and half-up
  normalization. Conversion back divides each filtered product by filtered
  alpha with half-up rounding and clamps to a byte. Zero-alpha output is
  deterministically transparent black, so RGB carried by transparent inputs
  cannot create a colored fringe.
- Public `fit` defaults to byte-preserving nearest sampling and exposes the
  quality path as `ResizeFilter::triangle`. An unrecognized selector is an
  `invalid_argument`. The selected source crop is filtered directly into the
  planned destination rectangle; matte outside that rectangle is exact and is
  not part of the filter support.
- RasterForge v0.3 filters premultiplied values in gamma-encoded sRGB space.
  This prevents straight-alpha color fringes but is not photometrically correct
  linear-light filtering.
- No cancellation parameter or cancellation claim is added. The coefficient
  builder and scalar loops do not yet observe a cancellation source.

## Consequences

The oracle handles both enlargement and antialiased reduction with deterministic
integer behavior, including one-pixel edges and severe scale ratios. Clamp-to-
edge avoids darkening caused by an implicit transparent border. The scalar
implementation prioritizes auditability over throughput and may revisit
coefficient reuse or vectorization only after representative benchmarks exist.
Nearest remains the compatibility-preserving public default, while triangle
filtering makes the alpha-correct oracle directly usable by callers.
