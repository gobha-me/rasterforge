#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace external_rgba {

// This deliberately differs from rasterforge::Rgba8. The adapter copies named
// channels instead of assuming that an unrelated consumer has the same type or
// object representation.
struct Rgba8 {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{};

  friend constexpr auto operator==(const Rgba8 &, const Rgba8 &)
      -> bool = default;
};

static_assert(sizeof(Rgba8) == 4);

enum class ChannelOrder : std::uint8_t {
  rgba8,
};

enum class CopyError : std::uint8_t {
  invalid_source,
  size_overflow,
  allocation_failure,
};

class Image;

class ImageView {
public:
  [[nodiscard]] constexpr auto width() const noexcept -> std::uint32_t {
    return width_;
  }
  [[nodiscard]] constexpr auto height() const noexcept -> std::uint32_t {
    return height_;
  }
  [[nodiscard]] constexpr auto row_stride_bytes() const noexcept
      -> std::size_t {
    return row_stride_bytes_;
  }
  [[nodiscard]] static constexpr auto channel_order() noexcept -> ChannelOrder {
    return ChannelOrder::rgba8;
  }
  [[nodiscard]] constexpr auto row(std::uint32_t y) const noexcept
      -> std::span<const Rgba8> {
    if (y >= height_) {
      return {};
    }
    const auto stride_pixels = row_stride_bytes_ / sizeof(Rgba8);
    return pixels_.subspan(static_cast<std::size_t>(y) * stride_pixels, width_);
  }

private:
  friend class Image;

  constexpr ImageView(std::span<const Rgba8> pixels, std::uint32_t width,
                      std::uint32_t height,
                      std::size_t row_stride_bytes) noexcept
      : pixels_{pixels}, width_{width}, height_{height},
        row_stride_bytes_{row_stride_bytes} {}

  std::span<const Rgba8> pixels_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::size_t row_stride_bytes_{};
};

class Image {
public:
  [[nodiscard]] auto view() const noexcept -> ImageView {
    return ImageView{pixels_, width_, height_, row_stride_bytes_};
  }

  Image(const Image &) = delete;
  auto operator=(const Image &) -> Image & = delete;
  Image(Image &&) noexcept = default;
  auto operator=(Image &&) noexcept -> Image & = default;

private:
  friend auto copy_to_external_rgba8(rasterforge::ImageView source)
      -> std::expected<Image, CopyError>;

  Image(std::uint32_t width, std::uint32_t height, std::size_t row_stride_bytes,
        std::vector<Rgba8> pixels) noexcept
      : pixels_{std::move(pixels)}, width_{width}, height_{height},
        row_stride_bytes_{row_stride_bytes} {}

  std::vector<Rgba8> pixels_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::size_t row_stride_bytes_{};
};

[[nodiscard]] inline auto copy_to_external_rgba8(rasterforge::ImageView source)
    -> std::expected<Image, CopyError> {
  const auto extent = source.extent();
  if (extent.width == 0 || extent.height == 0) {
    return std::unexpected{CopyError::invalid_source};
  }

  constexpr auto size_max = std::numeric_limits<std::size_t>::max();
  const auto width = static_cast<std::size_t>(extent.width);
  const auto height = static_cast<std::size_t>(extent.height);
  if (width > size_max / sizeof(Rgba8) || height > size_max / width) {
    return std::unexpected{CopyError::size_overflow};
  }

  const auto pixel_count = width * height;
  std::vector<Rgba8> pixels;
  if (pixel_count > pixels.max_size()) {
    return std::unexpected{CopyError::size_overflow};
  }
  try {
    pixels.resize(pixel_count);
  } catch (const std::length_error &) {
    return std::unexpected{CopyError::size_overflow};
  } catch (const std::bad_alloc &) {
    return std::unexpected{CopyError::allocation_failure};
  }

  for (std::uint32_t y = 0; y < extent.height; ++y) {
    const auto source_row = source.row(y);
    if (!source_row || source_row->size() != width) {
      return std::unexpected{CopyError::invalid_source};
    }
    const auto destination_offset = static_cast<std::size_t>(y) * width;
    for (std::size_t x = 0; x < width; ++x) {
      const auto source_pixel = (*source_row)[x];
      pixels[destination_offset + x] = {
          source_pixel.r,
          source_pixel.g,
          source_pixel.b,
          source_pixel.a,
      };
    }
  }

  return Image{extent.width, extent.height, width * sizeof(Rgba8),
               std::move(pixels)};
}

} // namespace external_rgba
