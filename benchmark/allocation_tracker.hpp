#pragma once

#include <cstdint>

namespace rasterforge_benchmark::allocation_tracker {

struct Snapshot {
  bool available{};
  std::uint64_t allocation_calls{};
  std::uint64_t allocated_bytes{};
  std::uint64_t peak_outstanding_bytes{};
};

// Counts ordinary C++ new/new[] allocations made while armed. Over-aligned
// allocations and codec-owned C malloc calls are intentionally outside this
// observation; the benchmark labels the values accordingly.
auto begin() noexcept -> void;
[[nodiscard]] auto snapshot() noexcept -> Snapshot;
auto end() noexcept -> void;

} // namespace rasterforge_benchmark::allocation_tracker
