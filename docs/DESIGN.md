# RasterForge design

Status: accepted bootstrap architecture; implementation is tracked on GitHub

RasterForge is a small C++23 library for safely turning encoded raster media
into predictable RGBA pixels, then fitting and compositing those pixels for a
consumer. It is intentionally unaware of terminals, LLM providers, widgets,
networking, and filesystems.

The initial consumers are expected to be AIForge and TermForge integrations:

```text
Venice image bytes
       |
       v
  RasterForge  -- decode / orient / crop / resize / composite --> RGBA view
       |                                                       |
       |                                                       v
       |                                              TermForge Image
       |
       +-- unchanged PNG may bypass decoding ----------------> Kitty payload
```

The repository began from the organization's C++ starter and now carries its
own metadata, public image contracts, tests, package export, and roadmap.

## Why this is a separate library

TermForge deliberately keeps encoded-media codecs out of its core. It knows
how to place pixels and how to speak terminal graphics protocols, but it should
not grow PNG, JPEG, or WebP dependencies. Venice clients should own API calls
and returned bytes, not display transforms. AIForge should own presentation
policy, not pixel algorithms.

RasterForge is the reusable boundary between those concerns:

- `venice-cpp`: image requests, job state, metadata, encoded response bytes.
- RasterForge: validated decoding, transforms, compositing, and explicit
  resource limits.
- TermForge: terminal capability detection, cell/pixel layout, Kitty upload,
  ANSI half-block rendering, image layering, and capability-gated playback of
  caller-supplied pre-baked animation frames.
- AIForge: semantic image slots, themes, permissions, cost policy, cache
  policy, and the choice to expose image tools to a model.

The separation is justified when at least AIForge and one other application
need the same media path. It must remain useful without either sister project.

## Goals

1. Decode common static image formats from an in-memory byte span.
2. Represent owned RGBA images and non-owning image views without copying.
3. Apply EXIF orientation when the codec exposes it.
4. Crop, contain, cover, stretch, and letterbox into an exact output extent.
5. Support a normalized focal point so `cover` keeps the important region.
6. Resize with alpha-correct filtering and deterministic output.
7. Composite overlays, solid colors, tint, and dimming for backgrounds.
8. Enforce caller-supplied limits before large allocations or expensive work.
9. Return structured errors through `std::expected`; malformed input is data,
   not an exception or process abort.
10. Build and install as a normal CMake package on GCC and Clang.

## Non-goals

- Terminal detection, Kitty escape sequences, ANSI rendering, or cell layout.
- HTTP, API authentication, image generation, or provider-specific models.
- A widget abstraction or semantic concepts such as `hero` and `right-pane`.
- Filesystem policy. The core API consumes bytes; a small optional file helper
  may be added only if multiple callers otherwise duplicate it.
- Vector graphics, video, typography, or general GPU acceleration.
- Color-management perfection. Decoded samples are interpreted as sRGB while
  source profiles and colorimetry metadata are ignored and retained as zero
  bytes; conversion belongs in a separately designed component if demanded.
- Animated output in the current single-image API. GIF, animated WebP, and APNG
  need a later codec-neutral model because frame timing, disposal, memory
  limits, and partial decoding change the data model. TermForge's animation
  transport reduces downstream integration work but does not make one still
  image a valid representation of an animated source.

## Public data model

The public boundary uses 8-bit, row-major, straight-alpha sRGBA. Algorithms may
convert to explicitly named premultiplied representations internally when
filtering or compositing, but must document their transfer function and must not
leak that representation through an ambiguous type.

An indicative API, not a frozen signature:

```cpp
namespace rasterforge {

struct Extent {
  std::uint32_t width{};
  std::uint32_t height{};
};

struct Rgba8 {
  std::uint8_t r{}, g{}, b{}, a{255};
};

class ImageView {
 public:
  [[nodiscard]] auto extent() const noexcept -> Extent;
  [[nodiscard]] auto stride_bytes() const noexcept -> std::size_t;
  [[nodiscard]] auto row(std::uint32_t y) const
      -> std::span<const Rgba8>;
};

class Image {
 public:
  [[nodiscard]] static auto create(Extent extent)
      -> std::expected<Image, Error>;
  [[nodiscard]] auto view() const noexcept -> ImageView;
  [[nodiscard]] auto mutable_view() noexcept -> MutableImageView;
};

enum class Fit { contain, cover, stretch, none };
enum class ResizeFilter { nearest, triangle };

struct FocalPoint {
  float x{0.5F};  // normalized and clamped to [0, 1]
  float y{0.5F};
};

struct Limits {
  std::uint64_t max_input_bytes{32_MiB};
  std::uint64_t max_pixels{64_MiPixels};
  std::uint64_t max_output_bytes{256_MiB};
  std::uint32_t max_dimension{16'384};
  std::uint64_t max_temporary_bytes{64_MiB};
};

enum class ImageFormat : std::uint8_t { png = 1, jpeg = 2, webp = 3 };
enum class OrientationPolicy : std::uint8_t { apply, ignore };
enum class Orientation : std::uint8_t {
  identity = 1,
  mirror_horizontal = 2,
  rotate_180 = 3,
  mirror_vertical = 4,
  transpose = 5,
  rotate_90_clockwise = 6,
  transverse = 7,
  rotate_270_clockwise = 8,
};
enum class OrientationStatus : std::uint8_t {
  not_present,
  applied,
  ignored,
  invalid_ignored,
};

struct DecodeOptions {
  Limits limits{};
  OrientationPolicy orientation{OrientationPolicy::apply};
};

class DecodedImage {
 public:
  [[nodiscard]] auto image() const noexcept -> const Image&;
  [[nodiscard]] auto format() const noexcept -> ImageFormat;
  [[nodiscard]] auto encoded_extent() const noexcept -> Extent;
  [[nodiscard]] auto output_extent() const noexcept -> Extent;
  [[nodiscard]] auto has_alpha() const noexcept -> bool;
  [[nodiscard]] auto source_orientation() const noexcept
      -> std::optional<Orientation>;
  [[nodiscard]] auto orientation_status() const noexcept
      -> OrientationStatus;
};

[[nodiscard]] auto decode(std::span<const std::byte> encoded,
                          const DecodeOptions& options = {})
    -> std::expected<DecodedImage, Error>;

[[nodiscard]] auto fit(ImageView source, Extent destination, Fit fit,
                       FocalPoint focus = {}, Rgba8 matte = {0, 0, 0, 0},
                       const Limits& limits = {},
                       ResizeFilter filter = ResizeFilter::nearest)
    -> std::expected<Image, Error>;

}  // namespace rasterforge
```

The implementation must not use the illustrative size literals until their
types and overflow behavior are made unambiguous. Public constructors must
make it impossible for dimensions, stride, and storage length to disagree.

`DecodedImage` should retain useful, cheap metadata such as detected format,
source extent, applied orientation, and whether alpha is present. It should not
retain a codec object or expose a codec-specific handle.

## Operations

### Decode

The static decode set is PNG, JPEG, and WebP. WebP was initially deferred, then
reopened when an intended consumer established a concrete workload. Its
decoder-only libwebp integration, bounded RIFF inspection, explicit animation
rejection, and conservative temporary-memory reserve are recorded in
[ADR 0011](adr/0011-bounded-static-webp-decoding.md). Codec selection requires
a short implementation spike comparing:

- malformed-input behavior and maintenance record;
- maximum-dimension and allocation hooks;
- CMake package availability and FetchContent behavior;
- thread safety;
- orientation and color metadata support;
- installed-package/export behavior for static-library consumers.

Do not choose a codec solely because it is single-header. Safety limits and
predictable error reporting matter more than saving one CMake recipe.

Format detection should inspect a bounded signature. Filename extensions and
MIME strings may be hints in a higher layer, but never override the bytes.

Decoded sample values are interpreted as sRGB when color metadata is absent or
present. ICC profiles, PNG gamma/chromaticity records, and WebP XMP are not
validated, converted, preserved, or exposed. Well-framed invalid profile
payloads are ignored without weakening container and pixel-data validation.
The complete zero-retention contract is recorded in
[ADR 0012](adr/0012-ignore-color-metadata.md).

PNG `eXIf`, JPEG APP1, and WebP `EXIF` orientation are parsed into one generic
enum. Valid metadata is normalized by default or reported without application
when the caller selects `ignore`. Invalid optional orientation metadata is
reported as ignored rather than turning safe pixel data into a decode failure.
Values 2 through 8 use exact byte-preserving transforms; the codec-native
source image is charged to the temporary budget while the normalized
destination coexists. See
[ADR 0010](adr/0010-bounded-exif-orientation.md).

### Resize and fit

The caller names an exact destination extent and a fit policy:

- `contain`: preserve aspect ratio; use the matte for unused space.
- `cover`: preserve aspect ratio; crop excess around the focal point.
- `stretch`: fill exactly; aspect ratio may change.
- `none`: no scale; crop or letterbox around the focal point.

The public `fit` operation defaults to deterministic nearest-neighbor sampling.
It copies one complete straight-alpha RGBA source pixel per output pixel and
does not mix samples, so transparent colored pixels are preserved without a
premultiplied conversion. Callers may instead select a separable,
scale-adaptive triangle filter: it uses bilinear support while enlarging and
widens its footprint to antialias reductions.

Fit geometry uses half-open integer pixel rectangles. Finite focal coordinates
are clamped independently to `[0, 1]`: zero selects the top or left endpoint,
one selects the bottom or right endpoint, and the default `0.5` centers the
available crop or matte slack. Derived dimensions and offsets round to the
nearest integer, with exact halves placed toward the bottom or right. NaN and
infinite focal coordinates are invalid arguments. Aspect comparisons and ratio
calculations use checked-width integer intermediates and geometry planning does
not allocate pixel storage. The contract and its discrete-pixel tradeoff are
recorded in [ADR 0005](adr/0005-fit-geometry.md).

Nearest sampling maps destination pixel centers into the planned source
rectangle using checked integer arithmetic. Exact half-pixel ties select the
source pixel toward the right or bottom. The full destination is allocated
through `Image::create` with the caller's limits and initialized to the exact
matte value before sampling, so failures never expose partial output. The
sampling and resource contract is recorded in
[ADR 0006](adr/0006-nearest-fit-sampling.md).

Quality-filter coefficients use exact integer-rational weights, clamp support
to the selected source rectangle, and normalize with half-up rounding after
each separable pass. RGB samples use exact 16-bit `channel * alpha` products,
so nonzero-alpha straight colors survive the internal conversion without an
8-bit premultiplication loss. The filtered result is unpremultiplied back to the
public contract; alpha-zero output becomes transparent black. Coefficient
storage is checked against the caller's temporary budget before output
allocation. RasterForge v0.3 deliberately chooses premultiplied gamma-encoded
sRGB filtering rather than linear-light filtering; the determinism, edge,
memory, color, and cancellation decisions are recorded in
[ADR 0007](adr/0007-scale-adaptive-triangle-filter.md).

### Background transforms

The first useful background operations are deliberately small:

- composite over a solid color;
- multiply/tint RGB;
- adjust opacity;
- dim by a scalar;
- optional blur once there is a measured consumer need.

Prefer composable operations over a `make_terminal_background()` convenience
function. Panel opacity, contrast checks, and theme semantics belong to
AIForge/TermForge.

### Encoding

Encoding is optional for the first milestone. A PNG encoder becomes useful
when a transformed result should be cached or sent through TermForge's encoded
Kitty path. If added, make it a separate component/target so decode-only
consumers do not inherit the encoder dependency.

## Resource and security contract

Encoded images are untrusted input. Resource limits are part of the API, not a
late hardening pass.

Before allocating, validate with checked arithmetic:

- width and height are non-zero and within the dimension limit;
- `width * height` fits and is within the pixel limit;
- `width * sizeof(Rgba8)` fits the stride type;
- `stride * height` fits and is within the byte limit;
- codec working storage is bounded through allocator accounting or a checked
  conservative reservation when a codec has no allocator hook;
- orientation cannot swap dimensions past a limit;
- a crafted header cannot trigger allocation before the full header is
  validated.

No decode path may terminate the process, print to stdout/stderr, invoke a
user callback from a codec thread, or silently return a partial image as
success. Codec diagnostics should be translated to stable RasterForge error
categories, with optional bounded detail for logs.

Suggested error categories:

- `empty_input`
- `input_too_large`
- `unsupported_format`
- `malformed_data`
- `truncated_data`
- `invalid_dimensions`
- `resource_limit`
- `allocation_failure`
- `unsupported_feature`
- `codec_failure`
- `invalid_argument`

The category is stable program logic; the human message is not an API key.

## Ownership, concurrency, and cancellation

Images own contiguous storage. Views borrow it and must not outlive the owner.
Avoid shared global codec state and global caches. Pure transforms should be
safe to run concurrently on distinct output images.

Large decodes and resizes eventually need cooperative cancellation. Design the
operation context so a stop token can be added without changing every public
signature, but do not pretend cancellation is supported until codecs and inner
loops actually observe it. A return after all work completed is not useful
cancellation.

Caching is caller policy for v0.1. A later optional cache must be an explicit
object with byte and entry budgets, never an unbounded singleton. Useful cache
keys include a content digest plus decode/transform options and RasterForge's
algorithm version.

## Integration rules

### AIForge and Venice

If Venice returns a PNG and Kitty can consume it unchanged, AIForge should pass
the encoded bytes directly to TermForge. Decode only when AIForge needs a
transform, a generated background, inspection, or an ANSI fallback.

AIForge owns the generated asset and its lifetime. A model should request a
semantic slot and fit policy; it should not manipulate RasterForge buffers or
terminal coordinates directly.

### TermForge

Keep RasterForge out of TermForge's required dependency graph. An adapter in
AIForge, or an optional bridge target, converts a RasterForge view into
TermForge's `Pixel`/`Image` representation. The adapter must make the copy
visible in its name or documentation.

If zero-copy exchange later matters, agree on a small view protocol rather
than making either project's owning image type depend on the other.

TermForge v0.56.0 adds a complete terminal-driven animation path: callers can
register borrowed raw or PNG frames, place the resident animation root, and
control once/loop playback, seeking, stopping, and release through an opaque
handle. The path is gated by `supports_image_animation()` and has no implicit
fallback on unsupported drivers. See TermForge's
[visible-animation release](https://github.com/gobha-me/termforge/releases/tag/v0.56.0).

A future RasterForge animation result should fit that boundary without taking
a TermForge dependency. RasterForge can normalize codec-specific blend and
disposal operations into same-extent, full-canvas straight-alpha sRGBA frames
with explicit gaps and source loop metadata. An optional adapter can copy or
convert those frames into TermForge's `AnimationFrame` sequence. AIForge remains
responsible for capability fallback and presentation policy, including mapping
finite source loop counts onto TermForge's once/loop controls. TermForge owns
terminal residency and commanded playback; RasterForge owns hostile-input
validation, frame composition, and cumulative decode limits.

## Test strategy

Write the failure matrix before happy-path fixtures. At minimum:

| Area | Required adversarial cases |
| --- | --- |
| Input | empty, one-byte, wrong signature, truncated at every header boundary |
| Dimensions | zero, maximum, maximum plus one, multiplication overflow |
| Limits | encoded bytes, pixels, output bytes, and temporary budget exceeded |
| Codec | valid header with corrupt payload, unsupported variant, bogus metadata |
| Views | bad stride, short storage, out-of-range row, moved-from owner |
| Fit | zero destination, one-pixel edges, extreme aspect ratios, focal endpoints |
| Alpha | fully transparent colored pixels, translucent edge, opaque identity |
| Orientation | all rotations/reflections, width/height swap at limit |
| Allocation | deterministic injected failure where practical |

Use tiny hand-built or generated fixtures where possible. Keep fuzz seeds for
every discovered crash. Add libFuzzer targets for detection/decoding and fit
parameter parsing once the first codec lands; fuzz targets need not run in the
ordinary unit-test suite.

Golden image tests should compare exact bytes only for algorithms promised to
be deterministic. Otherwise assert invariants and a documented tolerance. A
single ordinary decode/resize is the final smoke test, not the center of the
suite.

## Proposed milestones

### RF-01: bootstrap and contracts (complete)

- BSD 3-Clause license and RasterForge metadata.
- Checked `Extent`, `Rgba8`, `Image`, and borrowing views.
- Structured `Error` and caller-controlled `Limits` types.
- Install/consumer verification on GCC and Clang.

### RF-02: first decoder

- Land one production codec behind the generic decode API.
- Enforce limits before allocation.
- Add malformed corpus, fuzz harness, and metadata reporting.

### RF-03: fit pipeline

- Crop, contain, cover, stretch, letterbox, and focal point.
- Add byte-preserving nearest and alpha-correct quality filtering.
- Test arithmetic boundaries and transparent-edge behavior.

### RF-04: format coverage and orientation

- Add the remaining consumer-driven static formats.
- Normalize orientation and document color metadata behavior.

### RF-05: compositing and bridge example

- Solid/tint/dim/opacity operations.
- Example adapter to a plain external RGBA view; keep TermForge optional.
- Benchmark the dimensions AIForge actually uses.

### Later, only with evidence

- Optional PNG encoding target.
- Explicit bounded cache.
- Cooperative cancellation.
- Codec-neutral animation model with same-extent composited RGBA frames,
  explicit frame gaps and loop metadata, cumulative frame/pixel/output/
  temporary limits, and atomic failure before partial success. Validate an
  optional TermForge animation adapter without adding TermForge to the core
  dependency graph.
- SIMD or GPU acceleration.

## Decisions the first implementation should record

Use short ADRs for these choices rather than burying them in code review:

1. Codec library/libraries and why their failure behavior is acceptable.
2. Public alpha and color-space contract.
3. Maximum default resource limits and how callers override them.
4. Resize filters and determinism promise.
5. Exception policy, especially allocation failure.
6. Whether the first release includes encoding or keeps it separate.
