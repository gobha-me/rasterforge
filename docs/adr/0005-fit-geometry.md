# ADR 0005: Checked integer fit geometry

Status: accepted

## Context

Contain, cover, stretch, and no-scale transforms need one geometry result that
later sampling kernels can share. Computing each policy inside each sampler
would duplicate overflow checks and allow focal-point or rounding behavior to
drift. Pixel rectangles are discrete, so aspect-preserving dimensions and crop
positions also need an explicit deterministic rounding rule.

## Decision

- Public fit policy and focal-point values are `Fit` and `FocalPoint`; the
  rectangle plan remains an implementation detail.
- Plans use half-open integer source and destination rectangles and retain both
  validated extents. They contain no pixel storage and perform no allocation.
- Zero source or destination dimensions are invalid. NaN and infinite focal
  coordinates are invalid arguments; other focal coordinates are clamped to
  `[0, 1]` independently.
- Aspect comparisons and dimension ratios use 64-bit integer intermediates,
  which are sufficient for products of two public 32-bit dimensions.
- Derived dimensions and focal offsets round to the nearest integer. Exact
  halves select the greater coordinate, toward the right or bottom, and a
  derived dimension is never reduced below one pixel.
- Contain keeps the full source and positions a fitted destination rectangle;
  cover positions a source crop and fills the full destination; stretch uses
  both complete rectangles; no-scale crops or positions each axis without
  resizing.

## Consequences

GCC and Clang receive the same integral plan before any sampling decisions are
made. Later samplers can allocate the exact destination extent, fill matte only
outside the planned destination rectangle, and cannot choose their own crop
rounding. Integer pixel geometry may differ slightly from an ideal fractional
aspect ratio at very small extents; subpixel filter behavior remains a separate
quality-kernel decision.
