# ADR 0014: Deterministic tint, dim, and opacity transforms

Status: accepted

## Context

RasterForge needs small background operations that callers can compose without
introducing terminal, theme, or layout policy. Public pixels use straight alpha,
so changing RGB intensity and changing coverage must remain visibly different
operations. Scalar handling, byte rounding, transparent colored pixels,
allocation limits, ordering, and exceptions also need one stable contract.

## Decision

- `tint`, `dim`, and `adjust_opacity` are out-of-place operations. Each accepts
  one borrowed `ImageView`, leaves it read-only, and returns a separately owned
  `Image`. No in-place overload is provided.
- `tint` takes an `Rgb8` multiplier. Each straight RGB channel is multiplied by
  its corresponding byte while alpha is copied exactly. `Rgb8{255,255,255}` is
  identity and `Rgb8{0,0,0}` produces black RGB without changing coverage.
- `dim` takes one floating-point factor, applies it uniformly to straight RGB,
  and copies alpha exactly. `adjust_opacity` applies the same factor rule only
  to alpha and copies RGB exactly, including RGB carried by alpha-zero pixels.
- Scalar factors must be finite. NaN and either infinity return
  `invalid_argument` before image validation or allocation. Finite factors are
  clamped to `[0,1]`, so negative values select zero and values above one select
  identity.
- A clamped scalar is converted to an 8-bit multiplier by rounding
  `factor * 255` half up. Promotion from `float` to `double` is exact, and the
  product needs at most 32 binary significant bits, so this conversion is exact
  on the supported implementations. Pixel channels then use the integer rule
  `(channel * multiplier + 127) / 255`. No platform math-library rounding mode
  participates in pixel conversion.
- A transform allocates its final result through `Image::create` before writing
  rows. The caller's dimension, pixel, and output-byte limits apply. The input
  and temporary budgets are not charged because the operation owns neither
  encoded input nor temporary storage.
- Allocation, representability, and row failures retain the existing stable
  error categories. A failure never returns a partial image. `Image::create`
  remains the allocation-failure boundary; these operations add no allocator or
  exception surface.
- The operations have no shared state and are safe to call concurrently when
  their borrowed inputs remain alive and are not concurrently mutated. They do
  not claim cancellation.

## Consequences

Every successful result remains row-major, 8-bit, straight-alpha sRGBA. Tint and
dim retain colored transparent pixels instead of canonicalizing them; opacity
can produce a colored transparent pixel deliberately. This differs from an
alpha-aware filter or composition, which may canonicalize a zero-alpha mixed
result to transparent black.

Each call rounds to a new 8-bit image. Repeated RGB multiplication is therefore
not generally commutative at byte boundaries, and tinting or dimming before
composition is not equivalent to doing so afterward. Opacity commutes exactly
with tint and dim because they modify disjoint channels. Callers choose and
document the sequence appropriate to their presentation policy.
