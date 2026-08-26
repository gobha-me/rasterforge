#include <rasterforge/rasterforge.hpp>

#include "premultiplied_rgba.hpp"

#include <cstdint>
#include <optional>

namespace rasterforge {
namespace {

constexpr Error extent_mismatch{
    ErrorCode::invalid_argument,
    "source and backdrop extents must match for compositing"};

auto composite_over_impl(ImageView source,
                         std::optional<ImageView> image_backdrop,
                         Rgba8 solid_backdrop, const Limits &limits)
    -> std::expected<Image, Error> {
  auto output = Image::create(source.extent(), limits);
  if (!output) {
    return std::unexpected{output.error()};
  }

  auto output_view = output->mutable_view();
  for (std::uint32_t y = 0; y < source.extent().height; ++y) {
    const auto source_row = source.row(y);
    if (!source_row) {
      return std::unexpected{source_row.error()};
    }
    auto output_row = output_view.row(y);
    if (!output_row) {
      return std::unexpected{output_row.error()};
    }

    if (image_backdrop) {
      const auto backdrop_row = image_backdrop->row(y);
      if (!backdrop_row) {
        return std::unexpected{backdrop_row.error()};
      }
      for (std::uint32_t x = 0; x < source.extent().width; ++x) {
        (*output_row)[x] =
            detail::source_over_srgba((*source_row)[x], (*backdrop_row)[x]);
      }
      continue;
    }

    for (std::uint32_t x = 0; x < source.extent().width; ++x) {
      (*output_row)[x] =
          detail::source_over_srgba((*source_row)[x], solid_backdrop);
    }
  }

  return output;
}

} // namespace

auto composite_over(ImageView source, ImageView backdrop, const Limits &limits)
    -> std::expected<Image, Error> {
  if (source.extent() != backdrop.extent()) {
    return std::unexpected{extent_mismatch};
  }
  return composite_over_impl(source, backdrop, {}, limits);
}

auto composite_over(ImageView source, Rgba8 backdrop, const Limits &limits)
    -> std::expected<Image, Error> {
  return composite_over_impl(source, std::nullopt, backdrop, limits);
}

} // namespace rasterforge
