#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

void decode_with_policy(std::span<const std::byte> encoded,
                        rasterforge::OrientationPolicy orientation) {
  constexpr std::uint64_t max_input_bytes{4U * 1024U};
  constexpr std::uint32_t max_dimension{64U};
  constexpr std::uint64_t max_pixels{static_cast<std::uint64_t>(max_dimension) *
                                     max_dimension};

  rasterforge::DecodeOptions options{};
  options.limits = {
      .max_input_bytes = max_input_bytes,
      .max_pixels = max_pixels,
      .max_output_bytes = max_pixels * sizeof(rasterforge::Rgba8),
      .max_dimension = max_dimension,
      .max_temporary_bytes = 2U * 1024U * 1024U,
  };
  options.orientation = orientation;

  [[maybe_unused]] auto decoded = rasterforge::decode(encoded, options);
}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                       std::size_t size) -> int {
  const auto encoded = std::as_bytes(std::span{data, size});
  decode_with_policy(encoded, rasterforge::OrientationPolicy::apply);
  decode_with_policy(encoded, rasterforge::OrientationPolicy::ignore);

  return 0;
}
