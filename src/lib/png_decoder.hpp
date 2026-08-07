#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <expected>
#include <span>

namespace rasterforge::detail {

struct PngDecodeResult {
  Image image;
  Extent encoded_extent;
  bool has_alpha;
};

[[nodiscard]] auto decode_png(std::span<const std::byte> encoded,
                              const DecodeOptions &options)
    -> std::expected<PngDecodeResult, Error>;

} // namespace rasterforge::detail
