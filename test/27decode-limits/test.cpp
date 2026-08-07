#include "../../src/lib/decode_limits.hpp"
#include "../26png-decode/fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;
namespace fixtures = png_fixtures;

namespace {

alignas(std::max_align_t) std::array<std::byte, 64> allocation_storage{};

[[nodiscard]] auto successful_allocation(std::size_t) noexcept -> void * {
  return allocation_storage.data();
}

[[nodiscard]] auto failed_allocation(std::size_t) noexcept -> void * {
  return nullptr;
}

void no_op_deallocation(void *) noexcept {}

template <std::size_t Size>
[[nodiscard]] auto bytes(const std::array<std::uint8_t, Size> &fixture) {
  return std::as_bytes(std::span{fixture});
}

template <std::size_t Size>
void write_big_endian_u32(std::array<std::uint8_t, Size> &fixture,
                          std::size_t offset, std::uint32_t value) {
  REQUIRE(offset + 4 <= fixture.size());
  fixture[offset] = static_cast<std::uint8_t>(value >> 24U);
  fixture[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
  fixture[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
  fixture[offset + 3] = static_cast<std::uint8_t>(value);
}

[[nodiscard]] auto unlimited_layout_limits() -> rf::Limits {
  return rf::Limits{
      .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
      .max_pixels = std::numeric_limits<std::uint64_t>::max(),
      .max_output_bytes = std::numeric_limits<std::uint64_t>::max(),
      .max_dimension = std::numeric_limits<std::uint32_t>::max(),
      .max_temporary_bytes = std::numeric_limits<std::uint64_t>::max(),
  };
}

} // namespace

TEST_CASE("decode limits accept exact boundaries and reject one past",
          "[decode][limits][boundary]") {
  rf::DecodeOptions exact{};
  exact.limits.max_input_bytes = fixtures::rgb_png.size();
  exact.limits.max_dimension = 2;
  exact.limits.max_pixels = 2;
  exact.limits.max_output_bytes = 2 * sizeof(rf::Rgba8);

  REQUIRE(rf::decode(bytes(fixtures::rgb_png), exact));

  SECTION("input bytes") {
    auto options = exact;
    --options.limits.max_input_bytes;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::input_too_large);
  }

  SECTION("dimension") {
    auto options = exact;
    --options.limits.max_dimension;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel count") {
    auto options = exact;
    --options.limits.max_pixels;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output bytes") {
    auto options = exact;
    --options.limits.max_output_bytes;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("codec temporary bytes") {
    auto options = exact;
    options.limits.max_temporary_bytes = 0;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE(
    "decode layout arithmetic rejects hostile dimensions before codec work",
    "[decode][limits][layout]") {
  SECTION("zero dimensions") {
    const auto layout = detail::checked_decode_layout({0, 1}, false, {});
    REQUIRE_FALSE(layout);
    REQUIRE(layout.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("orientation swaps a maximum legal axis") {
    auto limits = unlimited_layout_limits();
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    const auto layout =
        detail::checked_decode_layout({maximum, 1}, true, limits);
    REQUIRE(layout);
    REQUIRE(layout->encoded_extent == rf::Extent{maximum, 1});
    REQUIRE(layout->output_extent == rf::Extent{1, maximum});
    REQUIRE(layout->row_bytes == sizeof(rf::Rgba8));

    --limits.max_dimension;
    const auto over_limit =
        detail::checked_decode_layout({maximum, 1}, true, limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("maximum dimensions overflow RGBA storage") {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    const auto layout = detail::checked_decode_layout(
        {maximum, maximum}, false, unlimited_layout_limits());
    REQUIRE_FALSE(layout);
    REQUIRE(layout.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("raw IHDR limits win over an impossible payload",
          "[decode][png][limits][precedence]") {
  SECTION("zero width is invalid before the truncated payload") {
    auto impossible = fixtures::rgb_png;
    write_big_endian_u32(impossible, 16, 0);
    const auto decoded = rf::decode(bytes(impossible).first(24));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("overflowing output is rejected before CRC or payload parsing") {
    auto impossible = fixtures::rgb_png;
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    write_big_endian_u32(impossible, 16, maximum);
    write_big_endian_u32(impossible, 20, maximum);

    rf::DecodeOptions options{};
    options.limits = unlimited_layout_limits();
    const auto decoded = rf::decode(bytes(impossible).first(24), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("codec allocation accounting is exact, cumulative, and injectable",
          "[decode][limits][allocation]") {
  SECTION("an exact request consumes the inclusive limit") {
    detail::CodecAllocationBudget budget{8, successful_allocation,
                                         no_op_deallocation};
    auto *memory = budget.request(8);
    REQUIRE(memory != nullptr);
    REQUIRE(budget.consumed_bytes() == 8);
    REQUIRE(budget.failure() == detail::CodecAllocationFailure::none);
    budget.release(memory);

    REQUIRE(budget.request(1) == nullptr);
    REQUIRE(budget.failure() == detail::CodecAllocationFailure::resource_limit);
  }

  SECTION("one request beyond the limit is rejected before allocation") {
    detail::CodecAllocationBudget budget{8, successful_allocation,
                                         no_op_deallocation};
    REQUIRE(budget.request(9) == nullptr);
    REQUIRE(budget.consumed_bytes() == 0);
    REQUIRE(budget.failure() == detail::CodecAllocationFailure::resource_limit);
  }

  SECTION("freeing memory does not replenish the work budget") {
    detail::CodecAllocationBudget budget{8, successful_allocation,
                                         no_op_deallocation};
    auto *memory = budget.request(4);
    REQUIRE(memory != nullptr);
    budget.release(memory);
    REQUIRE(budget.request(5) == nullptr);
    REQUIRE(budget.consumed_bytes() == 4);
  }

  SECTION("a zero-sized codec request still consumes one byte") {
    detail::CodecAllocationBudget budget{1, successful_allocation,
                                         no_op_deallocation};
    REQUIRE(budget.request(0) != nullptr);
    REQUIRE(budget.consumed_bytes() == 1);
  }

  SECTION("allocator failure is distinct and needs no diagnostic allocation") {
    detail::CodecAllocationBudget budget{8, failed_allocation,
                                         no_op_deallocation};
    REQUIRE(budget.request(1) == nullptr);
    REQUIRE(budget.consumed_bytes() == 0);
    REQUIRE(budget.failure() ==
            detail::CodecAllocationFailure::allocation_failure);
  }
}

TEST_CASE("default decode limits include a documented temporary budget",
          "[decode][limits][defaults]") {
  constexpr rf::Limits limits{};
  STATIC_REQUIRE(limits.max_input_bytes == 32ULL * 1024ULL * 1024ULL);
  STATIC_REQUIRE(limits.max_pixels == 64ULL * 1024ULL * 1024ULL);
  STATIC_REQUIRE(limits.max_output_bytes == 256ULL * 1024ULL * 1024ULL);
  STATIC_REQUIRE(limits.max_dimension == 16'384U);
  STATIC_REQUIRE(limits.max_temporary_bytes == 64ULL * 1024ULL * 1024ULL);
}
