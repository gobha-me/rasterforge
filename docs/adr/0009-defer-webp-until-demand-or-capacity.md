# ADR 0009: defer WebP without closing the implementation path

- Status: superseded by [ADR 0011](0011-bounded-static-webp-decoding.md)
- Date: 2026-08-15
- Issue: [RF-04b](https://github.com/gobha-me/rasterforge/issues/17)

## Context

RasterForge evaluated static WebP for the v0.4 format milestone after landing
PNG and JPEG decoding. The intended consumers do not currently establish a
WebP requirement:

- AIForge assigns validated image decoding to RasterForge, but has no
  implemented media-ingestion path or representative WebP workload yet.
- TermForge deliberately does not decode compressed assets. Its opaque encoded
  path accepts PNG for direct terminal transport, while RasterForge-backed
  integrations can supply decoded RGBA.
- venice-cpp preserves JPEG, PNG, or WebP response bytes according to the
  actual media type, but its callers select the requested output format and
  its current image-generation example requests PNG.

No intended C++ consumer contains a representative WebP asset or has an open
delivery issue that requires WebP rather than PNG or JPEG. Provider support for
a selectable format is useful capability evidence, but is not by itself a
measured decode workload.

WebP also cannot reuse either existing codec. The libwebp decoder can inspect
lossy/lossless, alpha, dimensions, and animation before decode and can write
RGBA into caller-owned output storage. Its public decoder API does not expose a
per-operation allocator callback, however, so RasterForge would need to define
and verify a conservative temporary-memory contract for internal codec work.
Metadata inspection may additionally require the demux library. A production
integration would therefore add a system/fallback recipe, static install and
package-export handling, dependency security maintenance, and new malformed
and resource-limit coverage.

## Decision

Do not include WebP in the committed v0.4 scope. Keep returning
`unsupported_format` for WebP input, and do not add an `ImageFormat` value,
signature classification, codec dependency, or decoder in RF-04b.

This is a scheduling decision rather than a permanent product exclusion. WebP
may be reopened through either path:

1. An intended consumer or representative captured workload needs static WebP
   and cannot reasonably negotiate or normalize to PNG or JPEG upstream.
2. The active queue is light and a focused implementation spike proves that
   static libwebp decoding can meet RasterForge's checked output and temporary
   budgets, explicit animation rejection, silent structured-error behavior,
   concurrency contract, and installed static-package verification.

An implementation still requires its own ADR amendment or replacement. It
must cover lossy and lossless input, alpha, truncated and malformed data,
dimension and storage limits, allocation failure, metadata behavior, exact
system/fallback dependency discovery, and reproducible fuzz seeds. Animation
must remain rejected unless a separate public animation model has landed.

## Animation boundary

The current `DecodedImage` represents exactly one fully decoded image. GIF,
animated WebP, and APNG require a codec-neutral model for frame timing,
disposal, loop count, cumulative frame/pixel/temporary budgets, and failure
before partial output can be reported as success. RasterForge may own that
bounded decode and pixel-processing model in future; callers continue to own
playback timing and presentation policy.

RasterForge will not silently decode only the first frame of an animated input.
Animation should be designed once for the public model rather than introduced
through a codec-specific shortcut.

## Downstream integration update

TermForge v0.56.0 now provides the presentation half of this boundary. Its
capability-gated driver API accepts a complete sequence of borrowed raw or PNG
frames, registers the sequence once, places its resident root, and exposes
once/loop playback, seek, stop, status, and release operations through an opaque
handle. Unsupported drivers refuse honestly rather than inventing a
client-driven fallback. See TermForge's
[visible-animation release](https://github.com/gobha-me/termforge/releases/tag/v0.56.0).

This materially reduces the integration work for animated GIF, WebP, or APNG,
but it does not replace RasterForge's missing hostile-input model. RasterForge
must still validate the complete source, apply codec-specific blend and disposal
rules, expose timing and source loop metadata, enforce cumulative limits, and
avoid partial success. Producing same-extent, fully composited straight-alpha
sRGBA frames would align directly with TermForge's registration contract while
keeping codecs and terminal semantics in their respective libraries.

Callers retain presentation policy rather than necessarily implementing the
playback engine themselves. On a capable Kitty path, TermForge can own terminal
residency and commanded playback. AIForge or another caller still chooses
fallback behavior and maps source loop counts to TermForge's once/loop controls;
TermForge reports a client-timeline expected completion, not proof that the
terminal displayed a frame.

## Consequences

The supported decode set remains static PNG and JPEG, so RF-04c can add generic
orientation without first widening the dependency and failure matrix. Current
consumers retain a predictable way to request or transport supported formats.

Deferral leaves some otherwise valid input unsupported. The two reopening
paths prevent that decision from becoming inertia: measured need can prioritize
the codec, while spare project capacity can fund it once the resource and
package contracts are demonstrably sound.

On 2026-08-25, the first reopening condition was met by an AIForge/Venice WebP
delivery requirement. [ADR 0011](0011-bounded-static-webp-decoding.md) records
the resulting implementation decision while retaining the animation boundary
defined here.
