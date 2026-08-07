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

  friend constexpr auto operator==(const Limits &, const Limits &)
      -> bool = default;
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

} // namespace rasterforge
