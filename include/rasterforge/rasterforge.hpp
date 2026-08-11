#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rasterforge {

struct Extent {
  std::uint32_t width{};
  std::uint32_t height{};

  friend constexpr auto operator==(const Extent &, const Extent &)
      -> bool = default;
};

struct Rgba8 {
  std::uint8_t r{};
  std::uint8_t g{};
  std::uint8_t b{};
  std::uint8_t a{255};

  friend constexpr auto operator==(const Rgba8 &, const Rgba8 &)
      -> bool = default;
};

static_assert(sizeof(Rgba8) == 4,
              "RasterForge pixels must be tightly packed RGBA8");

enum class Fit : std::uint8_t {
  contain = 0,
  cover = 1,
  stretch = 2,
  none = 3,
};

// A normalized point used to place crops and matte regions. Finite values are
// clamped to [0, 1] by fit operations; (0, 0) selects the top-left endpoint,
// (1, 1) selects the bottom-right endpoint, and the default is centered.
struct FocalPoint {
  float x{0.5F};
  float y{0.5F};

  friend constexpr auto operator==(const FocalPoint &, const FocalPoint &)
      -> bool = default;
};

enum class ErrorCode {
  empty_input,
  input_too_large,
  unsupported_format,
  malformed_data,
  truncated_data,
  invalid_dimensions,
  resource_limit,
  allocation_failure,
  unsupported_feature,
  codec_failure,
  invalid_argument,
  row_out_of_range,
};

struct Error {
  ErrorCode code{};
  std::string_view message{};

  friend constexpr auto operator==(const Error &, const Error &)
      -> bool = default;
};

struct Limits {
  std::uint64_t max_input_bytes{32ULL * 1024ULL * 1024ULL};
  std::uint64_t max_pixels{64ULL * 1024ULL * 1024ULL};
  std::uint64_t max_output_bytes{256ULL * 1024ULL * 1024ULL};
  std::uint32_t max_dimension{16'384U};
  // Cumulative bytes requested by codec-owned temporary allocations. The
  // encoded input span and the final Image storage have separate limits above.
  std::uint64_t max_temporary_bytes{64ULL * 1024ULL * 1024ULL};

  friend constexpr auto operator==(const Limits &, const Limits &)
      -> bool = default;
};

// Format detection examines no more than this many leading encoded bytes.
// Increasing the bound when another format is added is source-compatible, but
// callers must not assume bytes beyond the current value are inspected.
inline constexpr std::size_t decode_signature_prefix_bytes{8};

enum class ImageFormat : std::uint8_t {
  png = 1,
};

enum class OrientationPolicy : std::uint8_t {
  apply = 0,
  ignore = 1,
};

enum class OrientationStatus : std::uint8_t {
  not_present = 0,
  applied = 1,
  ignored = 2,
};

struct DecodeOptions {
  Limits limits{};
  OrientationPolicy orientation{OrientationPolicy::apply};

  friend constexpr auto operator==(const DecodeOptions &,
                                   const DecodeOptions &) -> bool = default;
};

class Image;

class ImageView {
public:
  ImageView() = default;

  [[nodiscard]] constexpr auto extent() const noexcept -> Extent {
    return extent_;
  }
  [[nodiscard]] constexpr auto stride_bytes() const noexcept -> std::size_t {
    return stride_bytes_;
  }
  [[nodiscard]] auto row(std::uint32_t y) const noexcept
      -> std::expected<std::span<const Rgba8>, Error>;

private:
  friend class Image;
  friend class MutableImageView;

  constexpr ImageView(const Rgba8 *pixels, Extent extent,
                      std::size_t stride_bytes) noexcept
      : pixels_{pixels}, extent_{extent}, stride_bytes_{stride_bytes} {}

  const Rgba8 *pixels_{};
  Extent extent_{};
  std::size_t stride_bytes_{};
};

class MutableImageView {
public:
  MutableImageView() = default;

  [[nodiscard]] constexpr auto extent() const noexcept -> Extent {
    return extent_;
  }
  [[nodiscard]] constexpr auto stride_bytes() const noexcept -> std::size_t {
    return stride_bytes_;
  }
  [[nodiscard]] auto row(std::uint32_t y) const noexcept
      -> std::expected<std::span<Rgba8>, Error>;

  [[nodiscard]] constexpr operator ImageView() const noexcept {
    return ImageView{pixels_, extent_, stride_bytes_};
  }

private:
  friend class Image;

  constexpr MutableImageView(Rgba8 *pixels, Extent extent,
                             std::size_t stride_bytes) noexcept
      : pixels_{pixels}, extent_{extent}, stride_bytes_{stride_bytes} {}

  Rgba8 *pixels_{};
  Extent extent_{};
  std::size_t stride_bytes_{};
};

class Image {
public:
  Image(const Image &) = delete;
  auto operator=(const Image &) -> Image & = delete;
  Image(Image &&other) noexcept;
  auto operator=(Image &&other) noexcept -> Image &;

  [[nodiscard]] static auto create(Extent extent, const Limits &limits = {})
      -> std::expected<Image, Error>;

  [[nodiscard]] auto extent() const noexcept -> Extent { return extent_; }
  [[nodiscard]] auto stride_bytes() const noexcept -> std::size_t {
    return stride_bytes_;
  }
  [[nodiscard]] auto size_bytes() const noexcept -> std::size_t {
    return pixels_.size() * sizeof(Rgba8);
  }
  [[nodiscard]] auto view() const noexcept -> ImageView {
    return ImageView{pixels_.data(), extent_, stride_bytes_};
  }
  [[nodiscard]] auto mutable_view() noexcept -> MutableImageView {
    return MutableImageView{pixels_.data(), extent_, stride_bytes_};
  }

private:
  Image(Extent extent, std::size_t stride_bytes,
        std::vector<Rgba8> pixels) noexcept
      : extent_{extent}, stride_bytes_{stride_bytes},
        pixels_{std::move(pixels)} {}

  Extent extent_{};
  std::size_t stride_bytes_{};
  std::vector<Rgba8> pixels_{};
};

namespace detail {

struct DecodedImageAccess;

} // namespace detail

class DecodedImage {
public:
  DecodedImage(const DecodedImage &) = delete;
  auto operator=(const DecodedImage &) -> DecodedImage & = delete;
  DecodedImage(DecodedImage &&) noexcept = default;
  auto operator=(DecodedImage &&) noexcept -> DecodedImage & = default;

  [[nodiscard]] auto image() const noexcept -> const Image & { return image_; }
  [[nodiscard]] auto view() const noexcept -> ImageView { return image_.view(); }
  [[nodiscard]] auto format() const noexcept -> ImageFormat { return format_; }
  [[nodiscard]] auto encoded_extent() const noexcept -> Extent {
    return encoded_extent_;
  }
  [[nodiscard]] auto output_extent() const noexcept -> Extent {
    return image_.extent();
  }
  [[nodiscard]] auto has_alpha() const noexcept -> bool { return has_alpha_; }
  [[nodiscard]] auto orientation_status() const noexcept -> OrientationStatus {
    return orientation_status_;
  }

private:
  friend struct detail::DecodedImageAccess;

  DecodedImage(Image image, ImageFormat format, Extent encoded_extent,
               bool has_alpha, OrientationStatus orientation_status) noexcept
      : image_{std::move(image)}, format_{format},
        encoded_extent_{encoded_extent}, has_alpha_{has_alpha},
        orientation_status_{orientation_status} {}

  Image image_;
  ImageFormat format_;
  Extent encoded_extent_{};
  bool has_alpha_{};
  OrientationStatus orientation_status_{OrientationStatus::not_present};
};

// Decoding is byte-only: filenames, MIME hints, and filesystem access are not
// part of this boundary. Recognized PNG data is normalized to straight-alpha
// RGBA8; codec state and diagnostics remain private to the implementation.
[[nodiscard]] auto decode(std::span<const std::byte> encoded,
                          const DecodeOptions &options = {})
    -> std::expected<DecodedImage, Error>;

} // namespace rasterforge
