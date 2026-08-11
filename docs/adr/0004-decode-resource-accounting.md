# ADR 0004: decode resources are checked before codec work and accounted cumulatively

- Status: accepted
- Date: 2026-08-07
- Issue: [RF-02d](https://github.com/gobha-me/rasterforge/issues/8)

## Context

ADR 0003 bounded PNG input, dimensions, output storage, and individual chunk
allocations, but deliberately deferred exact aggregate codec accounting. A
crafted IHDR could also reach libpng setup before pixel and output-byte limits
were checked. Caller-controlled limits need deterministic, inclusive semantics
that do not depend on a system libpng's private allocation sizes.

## Decision

`Limits` adds `max_temporary_bytes`, defaulting to 64 MiB. For the PNG adapter,
it bounds cumulative bytes requested successfully by codec-owned allocations
during one decode. The budget excludes the caller's encoded span and the final
`Image`, which remain covered by `max_input_bytes` and `max_output_bytes`. Freed
allocations do not replenish the budget, so repeated allocation churn cannot
evade it; a zero-byte request accounts for one byte. Limits are inclusive and
callers override them through `DecodeOptions`. Later codec-specific accounting
must remain explicit; JPEG's reservation and in-memory ceiling are recorded in
ADR 0008.

A shared checked-layout operation validates non-zero encoded and normalized
dimensions, orientation axis swaps, pixel count, row bytes, output bytes, and
platform representability. The PNG adapter applies it to the bounded raw IHDR
before constructing libpng state. `Image::create` uses the same operation as a
second invariant check rather than maintaining parallel arithmetic.

Libpng's per-decode allocator consumes the temporary budget before calling the
system allocator. Budget exhaustion and accounting overflow are
`resource_limit`; a null allocator result within budget is
`allocation_failure`. Both use static `Error` diagnostics, so reporting either
failure cannot require a secondary allocation. The individual chunk ceiling is
the minimum of the input, temporary, and codec-type limits.

Input parsing is bounded by `max_input_bytes`, pixel processing by the checked
pixel/output limits, and codec allocation work by the cumulative temporary
budget. No CPU-time or cancellation claim is introduced.

## Consequences

Invalid or impossible header dimensions are rejected before libpng allocation,
CRC processing, or pixel decode. Exact boundaries and one-past failures can be
tested independently of libpng internals, while the custom allocator still
enforces the same accounting for every codec request. The additional public
aggregate member may affect positional aggregate initializers; named/default
construction remains the intended use before 1.0.
