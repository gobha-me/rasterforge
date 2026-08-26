# ADR 0013: Deterministic source-over compositing

Status: accepted

## Context

RasterForge exposes straight-alpha, 8-bit sRGBA pixels, but compositing is most
naturally defined with premultiplied values. A public operation also needs to
make backdrop extent compatibility, transparent colors, byte rounding,
allocation limits, aliasing, and exceptions explicit. Leaving those choices to
floating-point expressions or caller policy would make translucent edges and
fully transparent pixels platform-dependent.

## Decision

- `composite_over` is an out-of-place Porter-Duff source-over operation. It
  accepts a source view and either a same-extent backdrop view or one `Rgba8`
  value used uniformly across the result. The inputs are borrowed and read-only,
  so they may alias; no mutable or in-place overload is provided.
- Image backdrops must exactly match the source extent. A mismatch returns
  `invalid_argument`. Zero extents retain the ordinary `Image::create`
  `invalid_dimensions` result.
- Compositing uses the same private, explicitly named gamma-encoded
  premultiplied sRGBA products as quality filtering. It does not claim
  linear-light or color-managed composition.
- For source alpha `s`, backdrop alpha `b`, source channel `S`, and backdrop
  channel `B`, all represented as bytes, the exact common alpha numerator is
  `D = 255*s + b*(255-s)`. Output alpha is `D/255`, rounded half up. When `D` is
  nonzero, each output straight channel is
  `(255*(S*s) + (B*b)*(255-s))/D`, also rounded half up. All intermediates fit
  in `uint32_t`; floating-point arithmetic and platform math libraries do not
  participate.
- When `D` is zero, the output is deterministically transparent black. A
  zero-alpha source otherwise preserves the backdrop exactly, an opaque source
  replaces it exactly, and a zero-alpha backdrop preserves a nonzero-alpha
  source. RGB carried only by fully transparent pixels cannot leak into a
  visible result.
- The result is allocated through `Image::create` before any rows are written.
  `max_dimension`, `max_pixels`, and `max_output_bytes` apply to that final
  image. The operation owns no encoded input or temporary allocation, so
  `max_input_bytes` and `max_temporary_bytes` are not charged.
- Allocation and representability failures retain `Image::create`'s stable
  `resource_limit` or `allocation_failure` categories. Row failures are
  propagated and a failing operation never returns partial output.
- The operation has no shared state and is safe to call concurrently when the
  borrowed inputs remain alive and are not concurrently mutated. No
  cancellation parameter or cancellation claim is added.

## Consequences

Supported GCC and Clang builds agree on every result byte, including exact
rounding ties and translucent edges. The scalar implementation prioritizes a
small auditable oracle; any later vectorized implementation must reproduce it
exactly. Callers needing a flattened opaque image can use an opaque uniform
backdrop, while callers may also preserve translucency with an RGBA backdrop.
In-place composition remains a possible future API only if mutable-view
aliasing and failure semantics are designed explicitly.
