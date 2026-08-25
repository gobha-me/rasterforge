#pragma once

#include "orientation.hpp"

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace rasterforge::detail {

inline constexpr std::uint64_t webp_control_bytes{1ULL * 1024ULL * 1024ULL};
inline constexpr std::uint64_t webp_bytes_per_pixel{8ULL};
inline constexpr std::uint64_t webp_bytes_per_input_byte{32ULL};

struct WebpDecodeResult {
  Image image;
  Extent encoded_extent{};
  bool has_alpha{};
  OrientationMetadata orientation{};
};

[[nodiscard]] auto checked_webp_work_bytes(Extent extent,
                                           std::size_t encoded_size) noexcept
    -> std::expected<std::uint64_t, Error>;

[[nodiscard]] auto decode_webp(std::span<const std::byte> encoded,
                               const DecodeOptions &options)
    -> std::expected<WebpDecodeResult, Error>;

} // namespace rasterforge::detail
