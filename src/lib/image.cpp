#include <rasterforge/rasterforge.hpp>

#include "decode_limits.hpp"

#include <new>
#include <stdexcept>
#include <utility>

namespace rasterforge {
namespace {

constexpr Error size_overflow{
    ErrorCode::resource_limit,
    "image storage size is not representable on this platform"};
constexpr Error allocation_failure{ErrorCode::allocation_failure,
                                   "image storage allocation failed"};
constexpr Error row_out_of_range{ErrorCode::row_out_of_range,
                                 "row index is outside the image extent"};

} // namespace

auto ImageView::row(std::uint32_t y) const noexcept
    -> std::expected<std::span<const Rgba8>, Error> {
  if (y >= extent_.height) {
    return std::unexpected{row_out_of_range};
  }
  const auto stride_pixels = stride_bytes_ / sizeof(Rgba8);
  return std::span<const Rgba8>{
      pixels_ + (static_cast<std::size_t>(y) * stride_pixels), extent_.width};
}

auto MutableImageView::row(std::uint32_t y) const noexcept
    -> std::expected<std::span<Rgba8>, Error> {
  if (y >= extent_.height) {
    return std::unexpected{row_out_of_range};
  }
  const auto stride_pixels = stride_bytes_ / sizeof(Rgba8);
  return std::span<Rgba8>{
      pixels_ + (static_cast<std::size_t>(y) * stride_pixels), extent_.width};
}

Image::Image(Image &&other) noexcept
    : extent_{std::exchange(other.extent_, {})},
      stride_bytes_{std::exchange(other.stride_bytes_, 0)},
      pixels_{std::move(other.pixels_)} {
  other.pixels_.clear();
}

auto Image::operator=(Image &&other) noexcept -> Image & {
  if (this != &other) {
    extent_ = std::exchange(other.extent_, {});
    stride_bytes_ = std::exchange(other.stride_bytes_, 0);
    pixels_ = std::move(other.pixels_);
    other.pixels_.clear();
  }
  return *this;
}

auto Image::create(Extent extent, const Limits &limits)
    -> std::expected<Image, Error> {
  const auto layout = detail::checked_decode_layout(extent, false, limits);
  if (!layout) {
    return std::unexpected{layout.error()};
  }
  if (std::cmp_greater(layout->pixel_count, std::vector<Rgba8>{}.max_size())) {
    return std::unexpected{size_overflow};
  }

  try {
    return Image{
        extent, layout->row_bytes,
        std::vector<Rgba8>(static_cast<std::size_t>(layout->pixel_count))};
  } catch (const std::bad_alloc &) {
    return std::unexpected{allocation_failure};
  } catch (const std::length_error &) {
    return std::unexpected{size_overflow};
  }
}

} // namespace rasterforge
