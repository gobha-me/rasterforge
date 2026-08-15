#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>

namespace rasterforge::detail {

struct OrientationMetadata {
  std::optional<Orientation> value{};
  bool invalid{};
};

// Parse a TIFF payload whose leading bytes are the byte-order marker. JPEG's
// Exif identifier and PNG's eXIf chunk framing are handled by their codecs.
[[nodiscard]] auto
parse_exif_orientation(std::span<const std::byte> tiff) noexcept
    -> OrientationMetadata;

// Merge orientation-bearing metadata without inventing a precedence rule.
// Duplicate, conflicting, or malformed EXIF records become invalid metadata.
void merge_orientation_metadata(OrientationMetadata &destination,
                                OrientationMetadata source) noexcept;

[[nodiscard]] auto orientation_swaps_axes(Orientation orientation) noexcept
    -> bool;

// Validate both the codec-native image and the normalized destination before
// either pixel image is allocated. A non-identity transform temporarily owns
// the codec-native image while allocating the normalized output.
[[nodiscard]] auto validate_orientation_layout(
    Extent source_extent, const OrientationMetadata &metadata,
    const DecodeOptions &options) noexcept -> std::expected<void, Error>;

[[nodiscard]] auto apply_orientation(Image source, Orientation orientation,
                                     const Limits &limits)
    -> std::expected<Image, Error>;

} // namespace rasterforge::detail
