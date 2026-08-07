#include "decode_limits.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace rasterforge::detail {
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
    "image row or storage size is not representable on this platform"};

[[nodiscard]] auto default_allocate(std::size_t byte_count) noexcept -> void * {
  return std::malloc(byte_count);
}

void default_deallocate(void *memory) noexcept { std::free(memory); }

[[nodiscard]] constexpr auto checked_multiply(std::uint64_t left,
                                              std::uint64_t right) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (right != 0 &&
      left > (std::numeric_limits<std::uint64_t>::max() / right)) {
    return std::unexpected{size_overflow};
  }
  return left * right;
}

} // namespace

auto checked_decode_layout(Extent encoded_extent, bool orientation_swaps_axes,
                           const Limits &limits) noexcept
    -> std::expected<DecodeLayout, Error> {
  if (encoded_extent.width == 0 || encoded_extent.height == 0) {
    return std::unexpected{invalid_dimensions};
  }
  if (encoded_extent.width > limits.max_dimension ||
      encoded_extent.height > limits.max_dimension) {
    return std::unexpected{dimension_limit};
  }

  const auto output_extent =
      orientation_swaps_axes
          ? Extent{encoded_extent.height, encoded_extent.width}
          : encoded_extent;
  if (output_extent.width > limits.max_dimension ||
      output_extent.height > limits.max_dimension) {
    return std::unexpected{dimension_limit};
  }

  const auto pixel_count =
      checked_multiply(output_extent.width, output_extent.height);
  if (!pixel_count) {
    return std::unexpected{pixel_count.error()};
  }
  if (*pixel_count > limits.max_pixels) {
    return std::unexpected{pixel_limit};
  }

  const auto row_bytes = checked_multiply(output_extent.width, sizeof(Rgba8));
  if (!row_bytes) {
    return std::unexpected{row_bytes.error()};
  }
  const auto output_bytes = checked_multiply(*row_bytes, output_extent.height);
  if (!output_bytes) {
    return std::unexpected{output_bytes.error()};
  }
  if (*output_bytes > limits.max_output_bytes) {
    return std::unexpected{output_limit};
  }
  if (*row_bytes > std::numeric_limits<std::size_t>::max() ||
      *output_bytes > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected{size_overflow};
  }

  return DecodeLayout{
      .encoded_extent = encoded_extent,
      .output_extent = output_extent,
      .pixel_count = *pixel_count,
      .row_bytes = static_cast<std::size_t>(*row_bytes),
      .output_bytes = static_cast<std::size_t>(*output_bytes),
  };
}

CodecAllocationBudget::CodecAllocationBudget(std::uint64_t limit) noexcept
    : CodecAllocationBudget{limit, default_allocate, default_deallocate} {}

CodecAllocationBudget::CodecAllocationBudget(std::uint64_t limit,
                                             Allocate allocate,
                                             Deallocate deallocate) noexcept
    : limit_{limit}, allocate_{allocate}, deallocate_{deallocate} {}

auto CodecAllocationBudget::request(std::size_t byte_count) noexcept -> void * {
  if (failure_ != CodecAllocationFailure::none) {
    return nullptr;
  }

  const auto accounted_bytes = std::max<std::size_t>(byte_count, 1);
  if (accounted_bytes > std::numeric_limits<std::uint64_t>::max() ||
      static_cast<std::uint64_t>(accounted_bytes) >
          (limit_ - consumed_bytes_)) {
    failure_ = CodecAllocationFailure::resource_limit;
    return nullptr;
  }

  auto *memory = allocate_(accounted_bytes);
  if (memory == nullptr) {
    failure_ = CodecAllocationFailure::allocation_failure;
    return nullptr;
  }

  consumed_bytes_ += static_cast<std::uint64_t>(accounted_bytes);
  return memory;
}

void CodecAllocationBudget::release(void *memory) noexcept {
  deallocate_(memory);
}

} // namespace rasterforge::detail
