# ADR 0006: Deterministic nearest-neighbor fit sampling

Status: accepted

## Context

The checked fit plan defines integer source and destination rectangles, but an
executor still needs an exact sampling convention, matte behavior, and resource
contract. Those choices must agree across compilers and must not let an invalid
destination or allocation failure escape as partial output.

## Decision

- The initial public `fit` operation executes every fit policy with
  nearest-neighbor sampling into an exact destination `Image`.
- Each destination pixel center maps into the planned source rectangle as if by
  `floor((index + 0.5) * source_length / destination_length)`. Exact ties select
  the greater source index, toward the right or bottom. Quotient/remainder
  arithmetic avoids overflowing products wider than two public dimensions.
- The destination is created through `Image::create` with caller-supplied
  limits before sampling. Dimension, pixel-count, and output-byte limits apply;
  input and temporary-byte limits account no resource because fit consumes an
  existing view and creates no temporary filtering buffer.
- Every destination pixel is initialized to the caller's matte before the
  planned rectangle is sampled. Cover overwrites the whole destination;
  contain and no-scale retain matte where their plans leave slack.
- Sampling copies the selected `Rgba8` value exactly. It performs no blending,
  premultiplication, transfer-function conversion, or color-metadata handling;
  transparent pixels with non-zero RGB and translucent mattes retain all bytes.
- Geometry, allocation, and row errors propagate through `std::expected`. The
  output remains local until complete, so no partial result is reported as
  success and no diagnostic is written.

## Consequences

Identity and exact-byte fixtures are deterministic across GCC and Clang, and
all source indices remain within the checked half-open plan. Downsampling may
prefer the right or bottom sample on an exact center tie. Filters that mix
samples, temporary-buffer accounting, premultiplied alpha, and any sRGB versus
linear-light choice remain separate quality-filter decisions.
