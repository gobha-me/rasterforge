#include "webp_decoder.hpp"

#include "decode_limits.hpp"

#include <webp/decode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace rasterforge::detail {
namespace {

constexpr Error malformed_data{ErrorCode::malformed_data,
                               "WebP data is malformed"};
constexpr Error truncated_data{ErrorCode::truncated_data,
                               "WebP data ended before decoding completed"};
constexpr Error allocation_failure{ErrorCode::allocation_failure,
                                   "WebP codec allocation failed"};
constexpr Error temporary_limit{
    ErrorCode::resource_limit,
    "WebP working storage exceeds the configured temporary-byte limit"};
constexpr Error unsupported_feature{
    ErrorCode::unsupported_feature,
    "WebP animation or another unsupported feature is present"};
constexpr Error codec_failure{ErrorCode::codec_failure,
                              "WebP codec failed without a stable category"};

constexpr std::array exif_chunk{
    std::byte{'E'},
    std::byte{'X'},
    std::byte{'I'},
    std::byte{'F'},
};
constexpr std::array exif_identifier{
    std::byte{'E'}, std::byte{'x'}, std::byte{'i'},
    std::byte{'f'}, std::byte{0},   std::byte{0},
};

[[nodiscard]] auto read_little_endian_u32(const std::byte *bytes) noexcept
    -> std::uint32_t {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1]))
          << 8U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2]))
          << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]))
          << 24U);
}

[[nodiscard]] auto
scan_container_orientation(std::span<const std::byte> encoded)
    -> std::expected<OrientationMetadata, Error> {
  constexpr std::size_t riff_header_bytes{12U};
  constexpr std::size_t chunk_header_bytes{8U};
  if (encoded.size() < riff_header_bytes) {
    return std::unexpected{truncated_data};
  }

  const auto riff_payload =
      static_cast<std::uint64_t>(read_little_endian_u32(encoded.data() + 4U));
  if (riff_payload < 4U || (riff_payload & 1U) != 0U) {
    return std::unexpected{malformed_data};
  }
  const auto container_bytes = riff_payload + 8U;
  if (container_bytes > encoded.size()) {
    return std::unexpected{truncated_data};
  }

  OrientationMetadata orientation{};
  auto offset = static_cast<std::uint64_t>(riff_header_bytes);
  while (offset < container_bytes) {
    if (container_bytes - offset < chunk_header_bytes) {
      return std::unexpected{malformed_data};
    }

    const auto *chunk = encoded.data() + static_cast<std::size_t>(offset);
    const auto payload_bytes =
        static_cast<std::uint64_t>(read_little_endian_u32(chunk + 4U));
    const auto padded_bytes = payload_bytes + (payload_bytes & 1U);
    if (padded_bytes > container_bytes - offset - chunk_header_bytes) {
      return std::unexpected{malformed_data};
    }

    if (std::equal(exif_chunk.begin(), exif_chunk.end(), chunk)) {
      auto payload =
          encoded.subspan(static_cast<std::size_t>(offset + chunk_header_bytes),
                          static_cast<std::size_t>(payload_bytes));
      if (payload.size() >= exif_identifier.size() &&
          std::equal(exif_identifier.begin(), exif_identifier.end(),
                     payload.begin())) {
        payload = payload.subspan(exif_identifier.size());
      }
      merge_orientation_metadata(orientation, parse_exif_orientation(payload));
    }

    offset += chunk_header_bytes + padded_bytes;
  }
  return orientation;
}

[[nodiscard]] auto error_for(VP8StatusCode status) noexcept -> Error {
  switch (status) {
  case VP8_STATUS_NOT_ENOUGH_DATA:
  case VP8_STATUS_SUSPENDED:
    return truncated_data;
  case VP8_STATUS_BITSTREAM_ERROR:
    return malformed_data;
  case VP8_STATUS_OUT_OF_MEMORY:
    return allocation_failure;
  case VP8_STATUS_UNSUPPORTED_FEATURE:
    return unsupported_feature;
  case VP8_STATUS_INVALID_PARAM:
  case VP8_STATUS_USER_ABORT:
  case VP8_STATUS_OK:
    return codec_failure;
  }
  return codec_failure;
}

class WebpConfig {
public:
  WebpConfig() noexcept : ready_{WebPInitDecoderConfig(&config_) != 0} {}
  WebpConfig(const WebpConfig &) = delete;
  auto operator=(const WebpConfig &) -> WebpConfig & = delete;
  ~WebpConfig() { WebPFreeDecBuffer(&config_.output); }

  [[nodiscard]] auto ready() const noexcept -> bool { return ready_; }
  [[nodiscard]] auto get() noexcept -> WebPDecoderConfig * { return &config_; }

private:
  WebPDecoderConfig config_{};
  bool ready_{};
};

[[nodiscard]] constexpr auto checked_add(std::uint64_t left,
                                         std::uint64_t right) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::unexpected{temporary_limit};
  }
  return left + right;
}

[[nodiscard]] constexpr auto checked_multiply(std::uint64_t left,
                                              std::uint64_t right) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right) {
    return std::unexpected{temporary_limit};
  }
  return left * right;
}

} // namespace

auto checked_webp_work_bytes(Extent extent, std::size_t encoded_size) noexcept
    -> std::expected<std::uint64_t, Error> {
  const auto pixels = checked_multiply(extent.width, extent.height);
  if (!pixels) {
    return std::unexpected{pixels.error()};
  }
  const auto pixel_reserve = checked_multiply(*pixels, webp_bytes_per_pixel);
  if (!pixel_reserve) {
    return std::unexpected{pixel_reserve.error()};
  }
  if (encoded_size > std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected{temporary_limit};
  }
  const auto input_reserve = checked_multiply(
      static_cast<std::uint64_t>(encoded_size), webp_bytes_per_input_byte);
  if (!input_reserve) {
    return std::unexpected{input_reserve.error()};
  }
  const auto variable_reserve = checked_add(*pixel_reserve, *input_reserve);
  if (!variable_reserve) {
    return std::unexpected{variable_reserve.error()};
  }
  return checked_add(webp_control_bytes, *variable_reserve);
}

auto decode_webp(std::span<const std::byte> encoded,
                 const DecodeOptions &options)
    -> std::expected<WebpDecodeResult, Error> {
  const auto orientation = scan_container_orientation(encoded);
  if (!orientation) {
    return std::unexpected{orientation.error()};
  }

  WebPBitstreamFeatures features{};
  const auto feature_status =
      WebPGetFeatures(reinterpret_cast<const std::uint8_t *>(encoded.data()),
                      encoded.size(), &features);
  if (feature_status != VP8_STATUS_OK) {
    return std::unexpected{error_for(feature_status)};
  }
  if (features.has_animation != 0) {
    return std::unexpected{unsupported_feature};
  }
  if (features.width <= 0 || features.height <= 0 ||
      std::cmp_greater(features.width,
                       std::numeric_limits<std::uint32_t>::max()) ||
      std::cmp_greater(features.height,
                       std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected{malformed_data};
  }

  const Extent extent{static_cast<std::uint32_t>(features.width),
                      static_cast<std::uint32_t>(features.height)};
  const auto layout = checked_decode_layout(extent, false, options.limits);
  if (!layout) {
    return std::unexpected{layout.error()};
  }
  const auto orientation_layout =
      validate_orientation_layout(extent, *orientation, options);
  if (!orientation_layout) {
    return std::unexpected{orientation_layout.error()};
  }
  const auto work_bytes = checked_webp_work_bytes(extent, encoded.size());
  if (!work_bytes) {
    return std::unexpected{work_bytes.error()};
  }
  if (*work_bytes > options.limits.max_temporary_bytes) {
    return std::unexpected{temporary_limit};
  }
  if (layout->row_bytes >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::unexpected{temporary_limit};
  }

  auto image_result = Image::create(layout->output_extent, options.limits);
  if (!image_result) {
    return std::unexpected{image_result.error()};
  }
  auto image = std::move(*image_result);
  if (image.stride_bytes() != layout->row_bytes ||
      image.size_bytes() != layout->output_bytes) {
    return std::unexpected{codec_failure};
  }
  auto first_row = image.mutable_view().row(0);
  if (!first_row) {
    return std::unexpected{codec_failure};
  }

  WebpConfig config{};
  if (!config.ready()) {
    return std::unexpected{codec_failure};
  }
  config.get()->input = features;
  config.get()->output.colorspace = MODE_RGBA;
  config.get()->output.is_external_memory = 1;
  config.get()->output.u.RGBA.rgba =
      reinterpret_cast<std::uint8_t *>(first_row->data());
  config.get()->output.u.RGBA.stride = static_cast<int>(image.stride_bytes());
  config.get()->output.u.RGBA.size = image.size_bytes();
  config.get()->options.use_threads = 0;
  config.get()->options.dithering_strength = 0;
  config.get()->options.alpha_dithering_strength = 0;

  const auto decode_status =
      WebPDecode(reinterpret_cast<const std::uint8_t *>(encoded.data()),
                 encoded.size(), config.get());
  if (decode_status != VP8_STATUS_OK) {
    return std::unexpected{error_for(decode_status)};
  }
  if (config.get()->output.width != features.width ||
      config.get()->output.height != features.height ||
      config.get()->output.u.RGBA.rgba !=
          reinterpret_cast<std::uint8_t *>(first_row->data())) {
    return std::unexpected{codec_failure};
  }

  return WebpDecodeResult{
      .image = std::move(image),
      .encoded_extent = extent,
      .has_alpha = features.has_alpha != 0,
      .orientation = *orientation,
  };
}

} // namespace rasterforge::detail
