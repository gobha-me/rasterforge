#include "allocation_tracker.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define RASTERFORGE_BENCHMARK_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define RASTERFORGE_BENCHMARK_TSAN 1
#endif
#ifndef RASTERFORGE_BENCHMARK_TSAN
#define RASTERFORGE_BENCHMARK_TSAN 0
#endif

namespace rasterforge_benchmark::allocation_tracker {
namespace {

#if !RASTERFORGE_BENCHMARK_TSAN
std::atomic<std::uint64_t> active_generation{};
std::atomic<std::uint64_t> next_generation{1};
std::atomic<std::uint64_t> allocation_calls{};
std::atomic<std::uint64_t> allocated_bytes{};
std::atomic<std::uint64_t> outstanding_bytes{};
std::atomic<std::uint64_t> peak_outstanding_bytes{};

auto update_peak(std::uint64_t value) noexcept -> void {
  auto peak = peak_outstanding_bytes.load(std::memory_order_relaxed);
  while (peak < value && !peak_outstanding_bytes.compare_exchange_weak(
                             peak, value, std::memory_order_relaxed,
                             std::memory_order_relaxed)) {
  }
}
#endif

} // namespace

auto begin() noexcept -> void {
#if RASTERFORGE_BENCHMARK_TSAN
  return;
#else
  allocation_calls.store(0, std::memory_order_relaxed);
  allocated_bytes.store(0, std::memory_order_relaxed);
  outstanding_bytes.store(0, std::memory_order_relaxed);
  peak_outstanding_bytes.store(0, std::memory_order_relaxed);

  auto generation = next_generation.fetch_add(1, std::memory_order_relaxed);
  if (generation == 0) {
    generation = next_generation.fetch_add(1, std::memory_order_relaxed);
  }
  active_generation.store(generation, std::memory_order_release);
#endif
}

auto snapshot() noexcept -> Snapshot {
#if RASTERFORGE_BENCHMARK_TSAN
  return {};
#else
  return Snapshot{
      .available = true,
      .allocation_calls = allocation_calls.load(std::memory_order_relaxed),
      .allocated_bytes = allocated_bytes.load(std::memory_order_relaxed),
      .peak_outstanding_bytes =
          peak_outstanding_bytes.load(std::memory_order_relaxed),
  };
#endif
}

auto end() noexcept -> void {
#if !RASTERFORGE_BENCHMARK_TSAN
  active_generation.store(0, std::memory_order_release);
#endif
}

} // namespace rasterforge_benchmark::allocation_tracker

#if !RASTERFORGE_BENCHMARK_TSAN
namespace {

struct alignas(std::max_align_t) AllocationHeader {
  std::size_t size{};
  std::uint64_t generation{};
};

[[nodiscard]] auto allocate(std::size_t size) -> void * {
  if (size >
      std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) {
    throw std::bad_alloc{};
  }

  const auto requested = size == 0 ? std::size_t{1} : size;
  auto *header = static_cast<AllocationHeader *>(
      std::malloc(sizeof(AllocationHeader) + requested));
  if (header == nullptr) {
    throw std::bad_alloc{};
  }

  const auto generation =
      rasterforge_benchmark::allocation_tracker::active_generation.load(
          std::memory_order_acquire);
  header->size = requested;
  header->generation = generation;
  if (generation != 0) {
    using namespace rasterforge_benchmark::allocation_tracker;
    allocation_calls.fetch_add(1, std::memory_order_relaxed);
    allocated_bytes.fetch_add(requested, std::memory_order_relaxed);
    const auto outstanding =
        outstanding_bytes.fetch_add(requested, std::memory_order_relaxed) +
        requested;
    update_peak(outstanding);
  }
  return header + 1;
}

auto deallocate(void *memory) noexcept -> void {
  if (memory == nullptr) {
    return;
  }
  auto *header = static_cast<AllocationHeader *>(memory) - 1;
  const auto generation =
      rasterforge_benchmark::allocation_tracker::active_generation.load(
          std::memory_order_acquire);
  if (generation != 0 && header->generation == generation) {
    rasterforge_benchmark::allocation_tracker::outstanding_bytes.fetch_sub(
        header->size, std::memory_order_relaxed);
  }
  std::free(header);
}

} // namespace

auto operator new(std::size_t size) -> void * { return allocate(size); }
auto operator new[](std::size_t size) -> void * { return allocate(size); }

auto operator delete(void *memory) noexcept -> void { deallocate(memory); }
auto operator delete[](void *memory) noexcept -> void { deallocate(memory); }
auto operator delete(void *memory, std::size_t) noexcept -> void {
  deallocate(memory);
}
auto operator delete[](void *memory, std::size_t) noexcept -> void {
  deallocate(memory);
}
#endif
