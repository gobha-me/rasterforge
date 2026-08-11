#include "jpeg_decoder.hpp"

#include "decode_limits.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

extern "C" {
// jerror.h depends on declarations from jpeglib.h; keep this order.
// clang-format off
#include <jpeglib.h>
#include <jerror.h>
// clang-format on
}

#include <csetjmp>

namespace rasterforge::detail {
namespace {

enum class JpegFailure : std::uint8_t {
  none,
  malformed,
  truncated,
  allocation,
  resource_limit,
  unsupported,
  invalid_dimensions,
  codec,
};

struct JpegErrors {
  jpeg_error_mgr base{};
  std::jmp_buf jump{};
  JpegFailure failure{JpegFailure::none};
};

constexpr Error malformed_data{ErrorCode::malformed_data,
                               "JPEG data is malformed"};
constexpr Error truncated_data{ErrorCode::truncated_data,
                               "JPEG data ended before decoding completed"};
constexpr Error allocation_failure{ErrorCode::allocation_failure,
                                   "JPEG codec allocation failed"};
constexpr Error temporary_limit{
    ErrorCode::resource_limit,
    "JPEG working storage exceeds the configured temporary-byte limit"};
constexpr Error unsupported_feature{
    ErrorCode::unsupported_feature,
    "JPEG uses a process or color layout that this decoder does not support"};
constexpr Error invalid_dimensions{ErrorCode::invalid_dimensions,
                                   "JPEG dimensions must both be non-zero"};
constexpr Error codec_failure{ErrorCode::codec_failure,
                              "JPEG codec produced an invalid RGBA layout"};

[[nodiscard]] auto failure_for_message(int message_code) noexcept
    -> JpegFailure {
  switch (message_code) {
  case JERR_INPUT_EMPTY:
  case JERR_INPUT_EOF:
  case JWRN_JPEG_EOF:
    return JpegFailure::truncated;

  case JERR_OUT_OF_MEMORY:
    return JpegFailure::allocation;
  case JERR_NO_BACKING_STORE:
  case JERR_IMAGE_TOO_BIG:
  case JERR_WIDTH_OVERFLOW:
    return JpegFailure::resource_limit;
  case JERR_EMPTY_IMAGE:
    return JpegFailure::invalid_dimensions;

  case JERR_BAD_PRECISION:
  case JERR_CCIR601_NOTIMPL:
  case JERR_CONVERSION_NOTIMPL:
  case JERR_FRACT_SAMPLE_NOTIMPL:
  case JERR_NOT_COMPILED:
  case JERR_NOTIMPL:
  case JERR_SOF_UNSUPPORTED:
#if JPEG_LIB_VERSION < 70
  case JERR_ARITH_NOTIMPL:
#endif
    return JpegFailure::unsupported;

  case JERR_BAD_LIB_VERSION:
  case JERR_BAD_STRUCT_SIZE:
  case JERR_BAD_STATE:
  case JERR_BAD_VIRTUAL_ACCESS:
  case JERR_VIRTUAL_BUG:
    return JpegFailure::codec;
  default:
    return JpegFailure::malformed;
  }
}

void jpeg_error_exit(j_common_ptr common) noexcept {
  auto *errors = reinterpret_cast<JpegErrors *>(common->err);
  if (errors->failure == JpegFailure::none) {
    errors->failure = failure_for_message(errors->base.msg_code);
  }
  std::longjmp(errors->jump, 1);
}

void jpeg_emit_message(j_common_ptr common, int level) noexcept {
  if (level >= 0) {
    return;
  }
  auto *errors = reinterpret_cast<JpegErrors *>(common->err);
  if (errors->failure == JpegFailure::none) {
    errors->failure = failure_for_message(errors->base.msg_code);
  }
  std::longjmp(errors->jump, 1);
}

void jpeg_output_message(j_common_ptr) noexcept {}

[[nodiscard]] auto error_for(JpegFailure failure) noexcept -> Error {
  switch (failure) {
  case JpegFailure::truncated:
    return truncated_data;
  case JpegFailure::allocation:
    return allocation_failure;
  case JpegFailure::resource_limit:
    return temporary_limit;
  case JpegFailure::unsupported:
    return unsupported_feature;
  case JpegFailure::invalid_dimensions:
    return invalid_dimensions;
  case JpegFailure::codec:
    return codec_failure;
  case JpegFailure::none:
  case JpegFailure::malformed:
    return malformed_data;
  }
  return codec_failure;
}

// These call frames contain no non-trivial automatic objects after setjmp.
// A libjpeg fatal error therefore cannot bypass a C++ lifetime.
[[nodiscard]] auto create_decompressor(jpeg_decompress_struct *decoder,
                                       JpegErrors *errors) noexcept -> bool {
  if (setjmp(errors->jump) != 0) {
    return false;
  }
  jpeg_create_decompress(decoder);
  return true;
}

[[nodiscard]] auto read_header(jpeg_decompress_struct *decoder,
                               JpegErrors *errors, const unsigned char *encoded,
                               unsigned long encoded_size) noexcept -> bool {
  if (setjmp(errors->jump) != 0) {
    return false;
  }
  jpeg_mem_src(decoder, encoded, encoded_size);
  if (jpeg_read_header(decoder, TRUE) != JPEG_HEADER_OK) {
    errors->failure = JpegFailure::malformed;
    return false;
  }
  return true;
}

[[nodiscard]] auto calculate_output(jpeg_decompress_struct *decoder,
                                    JpegErrors *errors) noexcept -> bool {
  if (setjmp(errors->jump) != 0) {
    return false;
  }
  decoder->out_color_space = JCS_EXT_RGBA;
  decoder->quantize_colors = FALSE;
  decoder->do_block_smoothing = FALSE;
  decoder->dct_method = JDCT_ISLOW;
  jpeg_calc_output_dimensions(decoder);
  return true;
}

[[nodiscard]] auto read_pixels(jpeg_decompress_struct *decoder,
                               JpegErrors *errors, unsigned char *pixels,
                               std::size_t stride,
                               std::uint32_t height) noexcept -> bool {
  if (setjmp(errors->jump) != 0) {
    return false;
  }
  if (jpeg_start_decompress(decoder) == FALSE) {
    errors->failure = JpegFailure::codec;
    return false;
  }
  if (decoder->output_components != 4 ||
      static_cast<std::uint64_t>(decoder->output_width) * 4U != stride ||
      decoder->output_height != height) {
    errors->failure = JpegFailure::codec;
    return false;
  }

  while (decoder->output_scanline < decoder->output_height) {
    auto *row =
        pixels + (static_cast<std::size_t>(decoder->output_scanline) * stride);
    JSAMPROW rows[]{row};
    if (jpeg_read_scanlines(decoder, rows, 1) != 1) {
      errors->failure = JpegFailure::codec;
      return false;
    }
  }
  if (jpeg_finish_decompress(decoder) == FALSE) {
    errors->failure = JpegFailure::codec;
    return false;
  }
  return true;
}

class JpegReader {
public:
  JpegReader() noexcept {
    decoder_.err = jpeg_std_error(&errors_.base);
    errors_.base.error_exit = jpeg_error_exit;
    errors_.base.emit_message = jpeg_emit_message;
    errors_.base.output_message = jpeg_output_message;
    ready_ = create_decompressor(&decoder_, &errors_);
  }

  JpegReader(const JpegReader &) = delete;
  auto operator=(const JpegReader &) -> JpegReader & = delete;

  ~JpegReader() {
    if (decoder_.mem != nullptr) {
      jpeg_destroy_decompress(&decoder_);
    }
  }

  [[nodiscard]] auto ready() const noexcept -> bool { return ready_; }
  [[nodiscard]] auto decoder() noexcept -> jpeg_decompress_struct * {
    return &decoder_;
  }
  [[nodiscard]] auto errors() noexcept -> JpegErrors * { return &errors_; }

private:
  jpeg_decompress_struct decoder_{};
  JpegErrors errors_{};
  bool ready_{};
};

[[nodiscard]] auto supported_color_space(J_COLOR_SPACE color_space) noexcept
    -> bool {
  switch (color_space) {
  case JCS_GRAYSCALE:
  case JCS_RGB:
  case JCS_YCbCr:
    return true;
  default:
    return false;
  }
}

} // namespace

auto checked_jpeg_work_bytes(Extent extent) noexcept
    -> std::expected<std::uint64_t, Error> {
  constexpr auto bytes_per_rgba_row = sizeof(Rgba8);
  const auto width = static_cast<std::uint64_t>(extent.width);
  if (width >
      (std::numeric_limits<std::uint64_t>::max() / bytes_per_rgba_row)) {
    return std::unexpected{temporary_limit};
  }
  const auto row_bytes = width * bytes_per_rgba_row;
  if (row_bytes >
      ((std::numeric_limits<std::uint64_t>::max() - jpeg_control_bytes) /
       jpeg_scanline_row_multiplier)) {
    return std::unexpected{temporary_limit};
  }
  return jpeg_control_bytes + (row_bytes * jpeg_scanline_row_multiplier);
}

auto decode_jpeg(std::span<const std::byte> encoded,
                 const DecodeOptions &options)
    -> std::expected<JpegDecodeResult, Error> {
  if (encoded.size() > std::numeric_limits<unsigned long>::max()) {
    return std::unexpected{temporary_limit};
  }
  if (options.limits.max_temporary_bytes < jpeg_control_bytes) {
    return std::unexpected{temporary_limit};
  }

  JpegReader reader{};
  if (!reader.ready()) {
    return std::unexpected{error_for(reader.errors()->failure)};
  }

  reader.decoder()->mem->max_memory_to_use = static_cast<long>(
      std::min(options.limits.max_temporary_bytes,
               static_cast<std::uint64_t>(std::numeric_limits<long>::max())));

  if (!read_header(reader.decoder(), reader.errors(),
                   reinterpret_cast<const unsigned char *>(encoded.data()),
                   static_cast<unsigned long>(encoded.size()))) {
    return std::unexpected{error_for(reader.errors()->failure)};
  }

  if (reader.decoder()->data_precision != 8 ||
      (reader.decoder()->num_components != 1 &&
       reader.decoder()->num_components != 3) ||
      !supported_color_space(reader.decoder()->jpeg_color_space) ||
      reader.decoder()->arith_code != FALSE) {
    return std::unexpected{unsupported_feature};
  }

  const Extent extent{reader.decoder()->image_width,
                      reader.decoder()->image_height};
  const auto layout = checked_decode_layout(extent, false, options.limits);
  if (!layout) {
    return std::unexpected{layout.error()};
  }
  const auto work_bytes = checked_jpeg_work_bytes(extent);
  if (!work_bytes) {
    return std::unexpected{work_bytes.error()};
  }
  if (*work_bytes > options.limits.max_temporary_bytes) {
    return std::unexpected{temporary_limit};
  }

  if (!calculate_output(reader.decoder(), reader.errors())) {
    return std::unexpected{error_for(reader.errors()->failure)};
  }
  if (reader.decoder()->output_width != extent.width ||
      reader.decoder()->output_height != extent.height) {
    return std::unexpected{codec_failure};
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

  if (!read_pixels(reader.decoder(), reader.errors(),
                   reinterpret_cast<unsigned char *>(first_row->data()),
                   image.stride_bytes(), extent.height)) {
    return std::unexpected{error_for(reader.errors()->failure)};
  }

  return JpegDecodeResult{
      .image = std::move(image),
      .encoded_extent = extent,
  };
}

} // namespace rasterforge::detail
