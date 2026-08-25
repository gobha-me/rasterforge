#include "png_decoder.hpp"

#include "decode_limits.hpp"

#include <png.h>

#ifndef PNG_eXIf_SUPPORTED
#error "RasterForge requires libpng eXIf metadata support"
#endif

#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace rasterforge::detail {
namespace {

enum class PngFailure : std::uint8_t {
  none,
  malformed,
  truncated,
  allocation,
  resource_limit,
  unsupported,
  codec,
};

struct PngState {
  const std::byte *next{};
  std::size_t remaining{};
  CodecAllocationBudget *allocations{};
  PngFailure failure{PngFailure::none};
};

struct PngHeader {
  png_uint_32 width{};
  png_uint_32 height{};
  png_size_t row_bytes{};
  int passes{};
  bool has_alpha{};
  OrientationMetadata orientation{};
};

constexpr Error malformed_data{ErrorCode::malformed_data,
                               "PNG data is malformed"};
constexpr Error truncated_data{ErrorCode::truncated_data,
                               "PNG data ended before decoding completed"};
constexpr Error allocation_failure{ErrorCode::allocation_failure,
                                   "PNG codec allocation failed"};
constexpr Error temporary_limit{
    ErrorCode::resource_limit,
    "PNG codec allocations exceed the configured temporary-byte limit"};
constexpr Error unsupported_feature{
    ErrorCode::unsupported_feature,
    "PNG uses a critical feature that this decoder does not support"};
constexpr Error codec_failure{ErrorCode::codec_failure,
                              "PNG codec produced an invalid RGBA layout"};

// RasterForge deliberately does not color-manage or retain source metadata.
// Mark every colorimetry chunk recognized by supported libpng releases as
// unknown so the existing bounded callback discards its payload without
// profile decompression or codec-dependent validation. eXIf remains known and
// available to the orientation path below.
constexpr std::array<png_byte, 35> ignored_color_chunks{
    'c', 'H', 'R', 'M', 0,   'g', 'A', 'M', 'A', 0,   'i', 'C',
    'C', 'P', 0,   's', 'R', 'G', 'B', 0,   'c', 'I', 'C', 'P',
    0,   'c', 'L', 'L', 'I', 0,   'm', 'D', 'C', 'V', 0,
};

void set_failure(PngState *state, PngFailure failure) noexcept {
  if (state != nullptr && state->failure == PngFailure::none) {
    state->failure = failure;
  }
}

[[nodiscard]] auto state_from_error(png_structp png) noexcept -> PngState * {
  return static_cast<PngState *>(png_get_error_ptr(png));
}

[[nodiscard]] auto state_from_memory(png_structp png) noexcept -> PngState * {
  return png == nullptr ? nullptr
                        : static_cast<PngState *>(png_get_mem_ptr(png));
}

void png_error_callback(png_structp png, png_const_charp) noexcept {
  set_failure(state_from_error(png), PngFailure::malformed);
  png_longjmp(png, 1);
}

void png_warning_callback(png_structp, png_const_charp) noexcept {}

[[nodiscard]] auto png_allocate(png_structp png,
                                png_alloc_size_t byte_count) noexcept
    -> png_voidp {
  auto *state = state_from_memory(png);
  if (state == nullptr || state->allocations == nullptr) {
    return nullptr;
  }
  if (std::cmp_greater(byte_count, std::numeric_limits<std::size_t>::max())) {
    set_failure(state, PngFailure::resource_limit);
    return nullptr;
  }

  auto *memory =
      state->allocations->request(static_cast<std::size_t>(byte_count));
  if (memory == nullptr) {
    set_failure(state, state->allocations->failure() ==
                               CodecAllocationFailure::resource_limit
                           ? PngFailure::resource_limit
                           : PngFailure::allocation);
  }
  return memory;
}

void png_deallocate(png_structp png, png_voidp memory) noexcept {
  auto *state = state_from_memory(png);
  if (state != nullptr && state->allocations != nullptr) {
    state->allocations->release(memory);
    return;
  }
  std::free(memory);
}

void png_read_callback(png_structp png, png_bytep output,
                       png_size_t byte_count) noexcept {
  auto *state = static_cast<PngState *>(png_get_io_ptr(png));
  if (state == nullptr || std::cmp_greater(byte_count, state->remaining)) {
    set_failure(state, PngFailure::truncated);
    png_longjmp(png, 1);
  }

  std::memcpy(output, state->next, static_cast<std::size_t>(byte_count));
  state->next += static_cast<std::size_t>(byte_count);
  state->remaining -= static_cast<std::size_t>(byte_count);
}

auto png_unknown_chunk_callback(png_structp png,
                                png_unknown_chunkp chunk) noexcept -> int {
  auto *state = static_cast<PngState *>(png_get_user_chunk_ptr(png));
  constexpr png_byte ancillary_bit{0x20};
  if ((chunk->name[0] & ancillary_bit) == 0) {
    set_failure(state, PngFailure::unsupported);
    return -1;
  }

  // RasterForge does not retain codec metadata. Reporting an ancillary chunk
  // as handled prevents libpng from allocating storage for it.
  return 1;
}

[[nodiscard]] auto error_for(PngFailure failure) noexcept -> Error {
  switch (failure) {
  case PngFailure::truncated:
    return truncated_data;
  case PngFailure::allocation:
    return allocation_failure;
  case PngFailure::resource_limit:
    return temporary_limit;
  case PngFailure::unsupported:
    return unsupported_feature;
  case PngFailure::codec:
    return codec_failure;
  case PngFailure::none:
  case PngFailure::malformed:
    return malformed_data;
  }
  return codec_failure;
}

[[nodiscard]] auto read_big_endian_u32(const std::byte *bytes) noexcept
    -> std::uint32_t {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0]))
          << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1]))
          << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2]))
          << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

[[nodiscard]] auto validate_ihdr(std::span<const std::byte> encoded,
                                 const Limits &limits)
    -> std::expected<DecodeLayout, Error> {
  constexpr std::size_t ihdr_dimensions_end{24};
  if (encoded.size() < ihdr_dimensions_end) {
    return std::unexpected{truncated_data};
  }

  constexpr std::array ihdr_type{std::byte{'I'}, std::byte{'H'}, std::byte{'D'},
                                 std::byte{'R'}};
  if (read_big_endian_u32(encoded.data() + 8) != 13 ||
      !std::equal(ihdr_type.begin(), ihdr_type.end(), encoded.begin() + 12)) {
    return std::unexpected{malformed_data};
  }

  const Extent extent{
      read_big_endian_u32(encoded.data() + 16),
      read_big_endian_u32(encoded.data() + 20),
  };
  return checked_decode_layout(extent, false, limits);
}

class PngReader {
public:
  explicit PngReader(PngState *state) noexcept
      : png_{png_create_read_struct_2(PNG_LIBPNG_VER_STRING, state,
                                      png_error_callback, png_warning_callback,
                                      state, png_allocate, png_deallocate)} {
    if (png_ != nullptr) {
      info_ = png_create_info_struct(png_);
    }
  }

  PngReader(const PngReader &) = delete;
  auto operator=(const PngReader &) -> PngReader & = delete;

  ~PngReader() {
    if (png_ != nullptr) {
      png_destroy_read_struct(&png_, info_ == nullptr ? nullptr : &info_,
                              nullptr);
    }
  }

  [[nodiscard]] auto ready() const noexcept -> bool {
    return png_ != nullptr && info_ != nullptr;
  }
  [[nodiscard]] auto png() const noexcept -> png_structp { return png_; }
  [[nodiscard]] auto info() const noexcept -> png_infop { return info_; }

private:
  png_structp png_{};
  png_infop info_{};
};

// These frames contain only trivial automatic state after setjmp. A libpng
// longjmp therefore cannot skip a C++ object with a non-trivial lifetime.
[[nodiscard]] auto read_header(png_structp png, png_infop info, PngState *state,
                               const DecodeOptions *options,
                               PngHeader *header) noexcept -> bool {
  if (setjmp(png_jmpbuf(png)) != 0) {
    return false;
  }

  png_set_read_fn(png, state, png_read_callback);
  png_set_sig_bytes(png, 8);
  png_set_user_limits(png, options->limits.max_dimension,
                      options->limits.max_dimension);

  const auto chunk_limit = static_cast<png_alloc_size_t>(std::min(
      {options->limits.max_input_bytes, options->limits.max_temporary_bytes,
       static_cast<std::uint64_t>(
           std::numeric_limits<png_alloc_size_t>::max())}));
  png_set_chunk_malloc_max(png, chunk_limit);
  png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_IF_SAFE, nullptr, 0);
  png_set_keep_unknown_chunks(
      png, PNG_HANDLE_CHUNK_NEVER, ignored_color_chunks.data(),
      static_cast<int>(ignored_color_chunks.size() / 5U));
  png_set_read_user_chunk_fn(png, state, png_unknown_chunk_callback);

  png_read_info(png, info);

  png_uint_32 exif_size{};
  png_bytep exif_data{};
  if (png_get_eXIf_1(png, info, &exif_size, &exif_data) != 0U) {
    header->orientation = parse_exif_orientation(std::span<const std::byte>{
        reinterpret_cast<const std::byte *>(exif_data),
        static_cast<std::size_t>(exif_size)});
  }

  int bit_depth{};
  int color_type{};
  int interlace_type{};
  int compression_type{};
  int filter_method{};
  if (png_get_IHDR(png, info, &header->width, &header->height, &bit_depth,
                   &color_type, &interlace_type, &compression_type,
                   &filter_method) == 0) {
    set_failure(state, PngFailure::codec);
    return false;
  }

  const auto has_trns = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
  header->has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0 || has_trns;

  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (has_trns) {
    png_set_tRNS_to_alpha(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  if ((color_type & PNG_COLOR_MASK_ALPHA) == 0 && !has_trns) {
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
  }

  header->passes = png_set_interlace_handling(png);
  png_read_update_info(png, info);
  header->row_bytes = png_get_rowbytes(png, info);

  if (png_get_bit_depth(png, info) != 8 || png_get_channels(png, info) != 4) {
    set_failure(state, PngFailure::codec);
    return false;
  }
  return true;
}

[[nodiscard]] auto read_pixels(png_structp png, png_infop info,
                               png_bytep pixels, std::size_t stride,
                               png_uint_32 height, int passes) noexcept
    -> bool {
  if (setjmp(png_jmpbuf(png)) != 0) {
    return false;
  }

  for (int pass = 0; pass < passes; ++pass) {
    for (png_uint_32 y = 0; y < height; ++y) {
      png_read_row(png, pixels + (static_cast<std::size_t>(y) * stride),
                   nullptr);
    }
  }
  png_read_end(png, info);
  return true;
}

} // namespace

auto decode_png(std::span<const std::byte> encoded,
                const DecodeOptions &options)
    -> std::expected<PngDecodeResult, Error> {
  const auto expected_extent = validate_ihdr(encoded, options.limits);
  if (!expected_extent) {
    return std::unexpected{expected_extent.error()};
  }

  CodecAllocationBudget allocations{options.limits.max_temporary_bytes};
  PngState state{
      .next = encoded.data() + 8,
      .remaining = encoded.size() - 8,
      .allocations = &allocations,
  };
  PngReader reader{&state};
  if (!reader.ready()) {
    if (state.failure == PngFailure::none) {
      set_failure(&state, allocations.failure() ==
                                  CodecAllocationFailure::resource_limit
                              ? PngFailure::resource_limit
                              : PngFailure::allocation);
    }
    return std::unexpected{error_for(state.failure)};
  }

  PngHeader header{};
  if (!read_header(reader.png(), reader.info(), &state, &options, &header)) {
    return std::unexpected{error_for(state.failure)};
  }

  const Extent extent{header.width, header.height};
  if (extent != expected_extent->encoded_extent) {
    return std::unexpected{codec_failure};
  }
  const auto orientation_layout =
      validate_orientation_layout(extent, header.orientation, options);
  if (!orientation_layout) {
    return std::unexpected{orientation_layout.error()};
  }
  auto image_result =
      Image::create(expected_extent->output_extent, options.limits);
  if (!image_result) {
    return std::unexpected{image_result.error()};
  }
  auto image = std::move(*image_result);

  if (std::cmp_not_equal(header.row_bytes, expected_extent->row_bytes) ||
      image.stride_bytes() != expected_extent->row_bytes ||
      image.size_bytes() != expected_extent->output_bytes) {
    return std::unexpected{codec_failure};
  }
  auto first_row = image.mutable_view().row(0);
  if (!first_row) {
    return std::unexpected{codec_failure};
  }

  auto *pixels = reinterpret_cast<png_bytep>(first_row->data());
  if (!read_pixels(reader.png(), reader.info(), pixels, image.stride_bytes(),
                   header.height, header.passes)) {
    return std::unexpected{error_for(state.failure)};
  }

  return PngDecodeResult{
      .image = std::move(image),
      .encoded_extent = extent,
      .has_alpha = header.has_alpha,
      .orientation = header.orientation,
  };
}

} // namespace rasterforge::detail
