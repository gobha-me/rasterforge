#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>

namespace rasterforge::detail {

struct DecodeLayout {
  Extent encoded_extent{};
  Extent output_extent{};
  std::uint64_t pixel_count{};
  std::size_t row_bytes{};
  std::size_t output_bytes{};
};

[[nodiscard]] auto checked_decode_layout(Extent encoded_extent,
                                         bool orientation_swaps_axes,
                                         const Limits &limits) noexcept
    -> std::expected<DecodeLayout, Error>;

enum class CodecAllocationFailure : std::uint8_t {
  none,
  resource_limit,
  allocation_failure,
};

class CodecAllocationBudget {
public:
  using Allocate = void *(*)(std::size_t) noexcept;
  using Deallocate = void (*)(void *) noexcept;

  explicit CodecAllocationBudget(std::uint64_t limit) noexcept;
  CodecAllocationBudget(std::uint64_t limit, Allocate allocate,
                        Deallocate deallocate) noexcept;

  [[nodiscard]] auto request(std::size_t byte_count) noexcept -> void *;
  void release(void *memory) noexcept;

  [[nodiscard]] auto consumed_bytes() const noexcept -> std::uint64_t {
    return consumed_bytes_;
  }
  [[nodiscard]] auto failure() const noexcept -> CodecAllocationFailure {
    return failure_;
  }

private:
  std::uint64_t limit_{};
  std::uint64_t consumed_bytes_{};
  Allocate allocate_{};
  Deallocate deallocate_{};
  CodecAllocationFailure failure_{CodecAllocationFailure::none};
};

} // namespace rasterforge::detail
