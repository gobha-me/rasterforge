#include <catch2/catch_test_macros.hpp>

namespace rasterforge::detail {

auto png_dependency_ready() noexcept -> bool;

} // namespace rasterforge::detail

TEST_CASE("libpng headers and runtime agree", "[codec][dependency]") {
  REQUIRE(rasterforge::detail::png_dependency_ready());
}
