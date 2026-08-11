#include <rasterforge/rasterforge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace {

class Input {
public:
  explicit Input(std::span<const std::uint8_t> bytes) noexcept
      : bytes_{bytes} {}

  [[nodiscard]] auto next() noexcept -> std::uint8_t {
    if (position_ == bytes_.size()) {
      return 0;
    }
    return bytes_[position_++];
  }

private:
  std::span<const std::uint8_t> bytes_{};
  std::size_t position_{};
};

[[nodiscard]] auto select_policy(std::uint8_t value) noexcept
    -> rasterforge::Fit {
  switch (value % 6U) {
  case 0:
    return rasterforge::Fit::contain;
  case 1:
    return rasterforge::Fit::cover;
  case 2:
    return rasterforge::Fit::stretch;
  case 3:
    return rasterforge::Fit::none;
  case 4:
    return static_cast<rasterforge::Fit>(std::uint8_t{255});
  default:
    return static_cast<rasterforge::Fit>(value);
  }
}

[[nodiscard]] auto select_filter(std::uint8_t value) noexcept
    -> rasterforge::ResizeFilter {
  switch (value % 4U) {
  case 0:
    return rasterforge::ResizeFilter::nearest;
  case 1:
    return rasterforge::ResizeFilter::triangle;
  case 2:
    return static_cast<rasterforge::ResizeFilter>(std::uint8_t{255});
  default:
    return static_cast<rasterforge::ResizeFilter>(value);
  }
}

[[nodiscard]] auto select_focus(std::uint8_t value) noexcept -> float {
  switch (value % 10U) {
  case 0:
    return -1.0F;
  case 1:
    return 0.0F;
  case 2:
    return 0.5F;
  case 3:
    return 1.0F;
  case 4:
    return 2.0F;
  case 5:
    return std::numeric_limits<float>::quiet_NaN();
  case 6:
    return std::numeric_limits<float>::infinity();
  case 7:
    return -std::numeric_limits<float>::infinity();
  default:
    return static_cast<float>(value) /
           static_cast<float>(std::numeric_limits<std::uint8_t>::max());
  }
}

[[nodiscard]] auto select_temporary_limit(std::uint8_t value) noexcept
    -> std::uint64_t {
  constexpr std::array<std::uint64_t, 4> limits{0, 1, 1024, 64U * 1024U};
  return limits[value % limits.size()];
}

[[noreturn]] auto invariant_failed() noexcept -> void { std::abort(); }

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                       std::size_t size) -> int {
  Input input{std::span{data, size}};
  const rasterforge::Extent source_extent{
      .width = static_cast<std::uint32_t>(input.next() % 17U),
      .height = static_cast<std::uint32_t>(input.next() % 17U),
  };
  const rasterforge::Extent destination_extent{
      .width = static_cast<std::uint32_t>(input.next() % 25U),
      .height = static_cast<std::uint32_t>(input.next() % 25U),
  };
  const auto policy = select_policy(input.next());
  const auto filter = select_filter(input.next());
  const rasterforge::FocalPoint focus{select_focus(input.next()),
                                      select_focus(input.next())};
  const rasterforge::Rgba8 matte{input.next(), input.next(), input.next(),
                                 input.next()};

  rasterforge::Limits limits{};
  limits.max_input_bytes = 0;
  limits.max_dimension = 16;
  limits.max_pixels = 16U * 16U;
  limits.max_output_bytes = limits.max_pixels * sizeof(rasterforge::Rgba8);
  limits.max_temporary_bytes = select_temporary_limit(input.next());

  rasterforge::ImageView source_view{};
  std::optional<rasterforge::Image> source;
  if (source_extent.width != 0 && source_extent.height != 0) {
    auto created = rasterforge::Image::create(source_extent, limits);
    if (!created) {
      return 0;
    }
    source.emplace(std::move(*created));
    for (std::uint32_t y = 0; y < source_extent.height; ++y) {
      auto row = source->mutable_view().row(y);
      if (!row) {
        invariant_failed();
      }
      for (auto &pixel : *row) {
        pixel = {input.next(), input.next(), input.next(), input.next()};
      }
    }
    source_view = source->view();
  }

  const auto result = rasterforge::fit(source_view, destination_extent, policy,
                                       focus, matte, limits, filter);
  if (!result) {
    return 0;
  }

  if (result->extent() != destination_extent ||
      result->stride_bytes() !=
          static_cast<std::size_t>(destination_extent.width) *
              sizeof(rasterforge::Rgba8) ||
      result->size_bytes() !=
          static_cast<std::size_t>(destination_extent.width) *
              destination_extent.height * sizeof(rasterforge::Rgba8)) {
    invariant_failed();
  }
  for (std::uint32_t y = 0; y < destination_extent.height; ++y) {
    const auto row = result->view().row(y);
    if (!row || row->size() != destination_extent.width) {
      invariant_failed();
    }
  }
  return 0;
}
