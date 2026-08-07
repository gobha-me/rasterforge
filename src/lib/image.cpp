#include <rasterforge/rasterforge.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace rasterforge {
namespace {

constexpr Error invalid_dimensions{ErrorCode::invalid_dimensions,
                                   "image dimensions must both be non-zero"};
constexpr Error dimension_limit{ErrorCode::resource_limit,
                                "image dimension exceeds the configured limit"};
constexpr Error pixel_limit{ErrorCode::resource_limit,
                            "image pixel count exceeds the configured limit"};
constexpr Error output_limit{
    ErrorCode::resource_limit,
    "image storage exceeds the configured output-byte limit"};
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
  if (extent.width == 0 || extent.height == 0) {
    return std::unexpected{invalid_dimensions};
  }
  if (extent.width > limits.max_dimension ||
      extent.height > limits.max_dimension) {
    return std::unexpected{dimension_limit};
  }

  const auto pixel_count =
      static_cast<std::uint64_t>(extent.width) * extent.height;
  if (pixel_count > limits.max_pixels) {
    return std::unexpected{pixel_limit};
  }
  if (pixel_count >
      (std::numeric_limits<std::uint64_t>::max() / sizeof(Rgba8))) {
    return std::unexpected{size_overflow};
  }

  const auto byte_count = pixel_count * sizeof(Rgba8);
  if (byte_count > limits.max_output_bytes ||
      byte_count >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return std::unexpected{
        byte_count > limits.max_output_bytes ? output_limit : size_overflow};
  }

  const auto stride = static_cast<std::size_t>(extent.width) * sizeof(Rgba8);
  try {
    return Image{extent, stride,
                 std::vector<Rgba8>(static_cast<std::size_t>(pixel_count))};
  } catch (const std::bad_alloc &) {
    return std::unexpected{allocation_failure};
  } catch (const std::length_error &) {
    return std::unexpected{size_overflow};
  }
}

} // namespace rasterforge
