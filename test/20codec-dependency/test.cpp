#include <catch2/catch_test_macros.hpp>

namespace rasterforge::detail {

auto png_dependency_ready() noexcept -> bool;
auto jpeg_dependency_ready() noexcept -> bool;
auto webp_dependency_ready() noexcept -> bool;

} // namespace rasterforge::detail

TEST_CASE("libpng headers and runtime agree", "[codec][dependency]") {
  REQUIRE(rasterforge::detail::png_dependency_ready());
}

TEST_CASE("libjpeg dependency is the required libjpeg-turbo floor",
          "[codec][dependency]") {
  REQUIRE(rasterforge::detail::jpeg_dependency_ready());
}

TEST_CASE("libwebp dependency meets the maintained decoder floor",
          "[codec][dependency]") {
  REQUIRE(rasterforge::detail::webp_dependency_ready());
}
