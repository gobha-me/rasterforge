#include "orientation.hpp"

#include "decode_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace rasterforge::detail {
namespace {

enum class ByteOrder : std::uint8_t { little, big };

constexpr Error temporary_limit{
    ErrorCode::resource_limit,
    "orientation source storage exceeds the configured temporary-byte limit"};
constexpr Error invalid_orientation{ErrorCode::invalid_argument,
                                    "orientation value is not recognized"};
constexpr Error transform_failure{
    ErrorCode::codec_failure,
    "orientation transform could not access a validated image row"};

[[nodiscard]] auto byte_at(std::span<const std::byte> bytes,
                           std::size_t offset) noexcept -> std::uint8_t {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

[[nodiscard]] auto read_u16(std::span<const std::byte> bytes,
                            std::size_t offset, ByteOrder order) noexcept
    -> std::optional<std::uint16_t> {
  if (offset > bytes.size() || bytes.size() - offset < 2U) {
    return std::nullopt;
  }
  const auto first = static_cast<std::uint16_t>(byte_at(bytes, offset));
  const auto second = static_cast<std::uint16_t>(byte_at(bytes, offset + 1U));
  if (order == ByteOrder::little) {
    return static_cast<std::uint16_t>(first | (second << 8U));
  }
  return static_cast<std::uint16_t>((first << 8U) | second);
}

[[nodiscard]] auto read_u32(std::span<const std::byte> bytes,
                            std::size_t offset, ByteOrder order) noexcept
    -> std::optional<std::uint32_t> {
  if (offset > bytes.size() || bytes.size() - offset < 4U) {
    return std::nullopt;
  }
  const auto b0 = static_cast<std::uint32_t>(byte_at(bytes, offset));
  const auto b1 = static_cast<std::uint32_t>(byte_at(bytes, offset + 1U));
  const auto b2 = static_cast<std::uint32_t>(byte_at(bytes, offset + 2U));
  const auto b3 = static_cast<std::uint32_t>(byte_at(bytes, offset + 3U));
  if (order == ByteOrder::little) {
    return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
  }
  return (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
}

[[nodiscard]] auto recognized(Orientation orientation) noexcept -> bool {
  switch (orientation) {
  case Orientation::identity:
  case Orientation::mirror_horizontal:
  case Orientation::rotate_180:
  case Orientation::mirror_vertical:
  case Orientation::transpose:
  case Orientation::rotate_90_clockwise:
  case Orientation::transverse:
  case Orientation::rotate_270_clockwise:
    return true;
  }
  return false;
}

[[nodiscard]] auto source_coordinates(Orientation orientation, Extent source,
                                      std::uint32_t destination_x,
                                      std::uint32_t destination_y) noexcept
    -> std::pair<std::uint32_t, std::uint32_t> {
  switch (orientation) {
  case Orientation::identity:
    return {destination_x, destination_y};
  case Orientation::mirror_horizontal:
    return {source.width - 1U - destination_x, destination_y};
  case Orientation::rotate_180:
    return {source.width - 1U - destination_x,
            source.height - 1U - destination_y};
  case Orientation::mirror_vertical:
    return {destination_x, source.height - 1U - destination_y};
  case Orientation::transpose:
    return {destination_y, destination_x};
  case Orientation::rotate_90_clockwise:
    return {destination_y, source.height - 1U - destination_x};
  case Orientation::transverse:
    return {source.width - 1U - destination_y,
            source.height - 1U - destination_x};
  case Orientation::rotate_270_clockwise:
    return {source.width - 1U - destination_y, destination_x};
  }
  return {};
}

} // namespace

auto parse_exif_orientation(std::span<const std::byte> tiff) noexcept
    -> OrientationMetadata {
  if (tiff.size() < 8U) {
    return {.invalid = true};
  }

  ByteOrder order{};
  if (byte_at(tiff, 0) == static_cast<std::uint8_t>('I') &&
      byte_at(tiff, 1) == static_cast<std::uint8_t>('I')) {
    order = ByteOrder::little;
  } else if (byte_at(tiff, 0) == static_cast<std::uint8_t>('M') &&
             byte_at(tiff, 1) == static_cast<std::uint8_t>('M')) {
    order = ByteOrder::big;
  } else {
    return {.invalid = true};
  }

  const auto magic = read_u16(tiff, 2U, order);
  const auto directory_offset = read_u32(tiff, 4U, order);
  if (!magic || *magic != 42U || !directory_offset || *directory_offset < 8U ||
      *directory_offset > tiff.size()) {
    return {.invalid = true};
  }

  const auto entry_count = read_u16(tiff, *directory_offset, order);
  if (!entry_count) {
    return {.invalid = true};
  }

  constexpr std::uint64_t entry_bytes{12U};
  const auto entries_begin = static_cast<std::uint64_t>(*directory_offset) + 2U;
  const auto entries_size =
      static_cast<std::uint64_t>(*entry_count) * entry_bytes;
  const auto directory_end = entries_begin + entries_size + 4U;
  if (directory_end > tiff.size() || directory_end < entries_begin) {
    return {.invalid = true};
  }

  std::optional<Orientation> orientation;
  for (std::uint16_t index = 0; index < *entry_count; ++index) {
    const auto entry_offset = static_cast<std::size_t>(
        entries_begin + (static_cast<std::uint64_t>(index) * entry_bytes));
    const auto tag = read_u16(tiff, entry_offset, order);
    if (!tag || *tag != 0x0112U) {
      continue;
    }
    if (orientation) {
      return {.invalid = true};
    }

    const auto type = read_u16(tiff, entry_offset + 2U, order);
    const auto count = read_u32(tiff, entry_offset + 4U, order);
    const auto value = read_u16(tiff, entry_offset + 8U, order);
    if (!type || *type != 3U || !count || *count != 1U || !value ||
        *value < 1U || *value > 8U) {
      return {.invalid = true};
    }
    orientation = static_cast<Orientation>(*value);
  }

  return {.value = orientation};
}

void merge_orientation_metadata(OrientationMetadata &destination,
                                OrientationMetadata source) noexcept {
  if (destination.invalid) {
    return;
  }
  if (source.invalid || (destination.value && source.value)) {
    destination.value.reset();
    destination.invalid = true;
    return;
  }
  if (source.value) {
    destination.value = source.value;
  }
}

auto orientation_swaps_axes(Orientation orientation) noexcept -> bool {
  switch (orientation) {
  case Orientation::transpose:
  case Orientation::rotate_90_clockwise:
  case Orientation::transverse:
  case Orientation::rotate_270_clockwise:
    return true;
  case Orientation::identity:
  case Orientation::mirror_horizontal:
  case Orientation::rotate_180:
  case Orientation::mirror_vertical:
    return false;
  }
  return false;
}

auto validate_orientation_layout(Extent source_extent,
                                 const OrientationMetadata &metadata,
                                 const DecodeOptions &options) noexcept
    -> std::expected<void, Error> {
  const auto source_layout =
      checked_decode_layout(source_extent, false, options.limits);
  if (!source_layout) {
    return std::unexpected{source_layout.error()};
  }
  if (options.orientation != OrientationPolicy::apply || metadata.invalid ||
      !metadata.value) {
    return {};
  }

  const auto output_layout = checked_decode_layout(
      source_extent, orientation_swaps_axes(*metadata.value), options.limits);
  if (!output_layout) {
    return std::unexpected{output_layout.error()};
  }
  if (*metadata.value != Orientation::identity &&
      source_layout->output_bytes > options.limits.max_temporary_bytes) {
    return std::unexpected{temporary_limit};
  }
  return {};
}

auto apply_orientation(Image source, Orientation orientation,
                       const Limits &limits) -> std::expected<Image, Error> {
  if (!recognized(orientation)) {
    return std::unexpected{invalid_orientation};
  }
  const auto source_extent = source.extent();
  const auto source_layout =
      checked_decode_layout(source_extent, false, limits);
  if (!source_layout) {
    return std::unexpected{source_layout.error()};
  }
  if (orientation == Orientation::identity) {
    return source;
  }
  if (source_layout->output_bytes > limits.max_temporary_bytes) {
    return std::unexpected{temporary_limit};
  }

  const auto output_layout = checked_decode_layout(
      source_extent, orientation_swaps_axes(orientation), limits);
  if (!output_layout) {
    return std::unexpected{output_layout.error()};
  }
  auto output_result = Image::create(output_layout->output_extent, limits);
  if (!output_result) {
    return std::unexpected{output_result.error()};
  }
  auto output = std::move(*output_result);
  const auto source_view = source.view();
  auto output_view = output.mutable_view();

  for (std::uint32_t y = 0; y < output_layout->output_extent.height; ++y) {
    auto output_row = output_view.row(y);
    if (!output_row) {
      return std::unexpected{transform_failure};
    }
    for (std::uint32_t x = 0; x < output_layout->output_extent.width; ++x) {
      const auto [source_x, source_y] =
          source_coordinates(orientation, source_extent, x, y);
      const auto source_row = source_view.row(source_y);
      if (!source_row || source_x >= source_row->size()) {
        return std::unexpected{transform_failure};
      }
      (*output_row)[x] = (*source_row)[source_x];
    }
  }
  return output;
}

} // namespace rasterforge::detail
