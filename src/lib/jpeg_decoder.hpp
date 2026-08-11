#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace rasterforge::detail {

// A conservative, deterministic charge for libjpeg-turbo's permanent control
// state plus scanline/iMCU working buffers. Progressive coefficient arrays are
// additionally constrained by the codec's in-memory ceiling.
inline constexpr std::uint64_t jpeg_control_bytes{64ULL * 1024ULL};
inline constexpr std::uint64_t jpeg_scanline_row_multiplier{32ULL};

struct JpegDecodeResult {
  Image image;
  Extent encoded_extent;
};

[[nodiscard]] auto checked_jpeg_work_bytes(Extent extent) noexcept
    -> std::expected<std::uint64_t, Error>;

[[nodiscard]] auto decode_jpeg(std::span<const std::byte> encoded,
                               const DecodeOptions &options)
    -> std::expected<JpegDecodeResult, Error>;

} // namespace rasterforge::detail
