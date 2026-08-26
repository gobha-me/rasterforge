#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include "allocation_tracker.hpp"
#include "external_rgba.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef RASTERFORGE_BENCHMARK_BUILD_TYPE
#define RASTERFORGE_BENCHMARK_BUILD_TYPE "unknown"
#endif

namespace rasterforge_benchmark {
namespace {

using Clock = std::chrono::steady_clock;

enum class OutputFormat : std::uint8_t {
  table,
  csv,
};

struct Options {
  std::uint32_t samples{20};
  std::uint32_t sample_ms{25};
  OutputFormat format{OutputFormat::table};
};

struct Outcome {
  bool success{};
  bool temporary_limit{};
  std::uint64_t checksum{};
  std::string message{};
};

using Operation = std::function<Outcome(std::uint64_t)>;

struct BenchmarkCase {
  std::string name{};
  rasterforge::Extent output_extent{};
  std::uint64_t encoded_bytes{};
  bool measure_temporary_floor{};
  Operation operation{};
};

struct Fixture {
  std::string name{};
  rasterforge::Extent extent{};
  rasterforge::Image source;
  rasterforge::Image backdrop;
  std::vector<std::uint8_t> encoded_png{};
};

struct Result {
  std::string name{};
  rasterforge::Extent output_extent{};
  std::uint64_t encoded_bytes{};
  std::uint64_t output_bytes{};
  std::uint64_t temporary_floor_bytes{};
  std::uint64_t iterations_per_sample{};
  double median_us{};
  double p95_us{};
  double operations_per_second{};
  double megapixels_per_second{};
  double output_mib_per_second{};
  allocation_tracker::Snapshot allocations{};
};

std::uint64_t checksum_sink{};

[[nodiscard]] auto parse_positive(std::string_view text,
                                  std::string_view option)
    -> std::expected<std::uint32_t, std::string> {
  std::uint64_t value{};
  const auto *first = text.data();
  const auto *last = text.data() + text.size();
  const auto [next, error] = std::from_chars(first, last, value);
  if (error != std::errc{} || next != last || value == 0 ||
      value > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected{std::string{option} +
                           " requires a positive 32-bit integer"};
  }
  return static_cast<std::uint32_t>(value);
}

auto print_usage(std::FILE *stream) -> void {
  std::fprintf(stream, "usage: rasterforge-benchmark [--samples N] "
                       "[--sample-ms N] [--format table|csv]\n");
}

[[nodiscard]] auto parse_options(int argc, char **argv)
    -> std::expected<Options, std::string> {
  Options options{};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help") {
      print_usage(stdout);
      std::exit(0);
    }
    if (argument == "--samples" || argument == "--sample-ms" ||
        argument == "--format") {
      if (++index >= argc) {
        return std::unexpected{std::string{argument} + " needs a value"};
      }
      const std::string_view value{argv[index]};
      if (argument == "--samples") {
        const auto parsed = parse_positive(value, argument);
        if (!parsed) {
          return std::unexpected{parsed.error()};
        }
        options.samples = *parsed;
      } else if (argument == "--sample-ms") {
        const auto parsed = parse_positive(value, argument);
        if (!parsed) {
          return std::unexpected{parsed.error()};
        }
        options.sample_ms = *parsed;
      } else if (value == "table") {
        options.format = OutputFormat::table;
      } else if (value == "csv") {
        options.format = OutputFormat::csv;
      } else {
        return std::unexpected{"--format requires table or csv"};
      }
      continue;
    }
    return std::unexpected{"unknown argument: " + std::string{argument}};
  }
  return options;
}

[[nodiscard]] auto error_outcome(const rasterforge::Error &error) -> Outcome {
  return Outcome{
      .success = false,
      .temporary_limit = error.code == rasterforge::ErrorCode::resource_limit,
      .message = std::string{error.message},
  };
}

[[nodiscard]] auto checksum(rasterforge::ImageView view) -> Outcome {
  const auto extent = view.extent();
  if (extent.width == 0 || extent.height == 0) {
    return Outcome{.message = "operation returned an empty image"};
  }
  const std::array<std::uint32_t, 3> rows{
      0,
      extent.height / 2,
      extent.height - 1,
  };
  const std::array<std::uint32_t, 3> columns{
      0,
      extent.width / 2,
      extent.width - 1,
  };

  std::uint64_t value =
      (static_cast<std::uint64_t>(extent.width) << 32U) | extent.height;
  for (const auto y : rows) {
    const auto row = view.row(y);
    if (!row) {
      return error_outcome(row.error());
    }
    for (const auto x : columns) {
      const auto pixel = (*row)[x];
      value = (value * 1'099'511'628'211ULL) ^ pixel.r;
      value = (value * 1'099'511'628'211ULL) ^ pixel.g;
      value = (value * 1'099'511'628'211ULL) ^ pixel.b;
      value = (value * 1'099'511'628'211ULL) ^ pixel.a;
    }
  }
  return Outcome{.success = true, .checksum = value};
}

[[nodiscard]] auto checksum(external_rgba::ImageView view) -> Outcome {
  if (view.width() == 0 || view.height() == 0) {
    return Outcome{.message = "bridge returned an empty image"};
  }
  const auto row = view.row(view.height() - 1);
  if (row.size() != view.width()) {
    return Outcome{.message = "bridge returned invalid row storage"};
  }
  const auto pixel = row[view.width() - 1];
  auto value =
      (static_cast<std::uint64_t>(view.width()) << 32U) | view.height();
  value = (value << 8U) ^ pixel.red;
  value = (value << 8U) ^ pixel.green;
  value = (value << 8U) ^ pixel.blue;
  value = (value << 8U) ^ pixel.alpha;
  return Outcome{.success = true, .checksum = value};
}

[[nodiscard]] constexpr auto checked_pixel_count(rasterforge::Extent extent)
    -> std::expected<std::size_t, std::string_view> {
  const auto width = static_cast<std::size_t>(extent.width);
  const auto height = static_cast<std::size_t>(extent.height);
  if (width == 0 || height == 0 ||
      height > std::numeric_limits<std::size_t>::max() / width) {
    return std::unexpected{"fixture dimensions are not representable"};
  }
  return width * height;
}

[[nodiscard]] auto make_image(rasterforge::Extent extent, std::uint8_t seed)
    -> std::expected<rasterforge::Image, std::string> {
  auto image = rasterforge::Image::create(extent);
  if (!image) {
    return std::unexpected{std::string{image.error().message}};
  }
  auto view = image->mutable_view();
  for (std::uint32_t y = 0; y < extent.height; ++y) {
    auto row = view.row(y);
    if (!row) {
      return std::unexpected{std::string{row.error().message}};
    }
    for (std::uint32_t x = 0; x < extent.width; ++x) {
      (*row)[x] = rasterforge::Rgba8{
          static_cast<std::uint8_t>(seed + (x * 13U) + (y * 3U)),
          static_cast<std::uint8_t>((seed * 3U) + (x * 5U) + (y * 11U)),
          static_cast<std::uint8_t>((seed * 7U) + (x * 17U) + y),
          static_cast<std::uint8_t>(64U + ((x * 19U + y * 23U) % 192U)),
      };
    }
  }
  return std::move(*image);
}

auto append_u32(std::vector<std::uint8_t> &output, std::uint32_t value)
    -> void {
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value));
}

auto append_chunk(std::vector<std::uint8_t> &output,
                  const std::array<std::uint8_t, 4> &type,
                  std::span<const std::uint8_t> payload) -> void {
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error{"PNG chunk exceeds its 32-bit length field"};
  }
  append_u32(output, static_cast<std::uint32_t>(payload.size()));
  output.insert(output.end(), type.begin(), type.end());
  output.insert(output.end(), payload.begin(), payload.end());

  auto crc = ::crc32(0, Z_NULL, 0);
  crc = ::crc32(crc, type.data(), static_cast<uInt>(type.size()));
  if (!payload.empty()) {
    crc = ::crc32(crc, payload.data(), static_cast<uInt>(payload.size()));
  }
  append_u32(output, static_cast<std::uint32_t>(crc));
}

[[nodiscard]] auto make_png(rasterforge::ImageView source)
    -> std::expected<std::vector<std::uint8_t>, std::string> {
  const auto pixel_count = checked_pixel_count(source.extent());
  if (!pixel_count || *pixel_count > (std::numeric_limits<std::size_t>::max() -
                                      source.extent().height) /
                                         sizeof(rasterforge::Rgba8)) {
    return std::unexpected{"PNG fixture storage is not representable"};
  }

  std::vector<std::uint8_t> scanlines(
      (*pixel_count * sizeof(rasterforge::Rgba8)) + source.extent().height);
  std::size_t offset = 0;
  for (std::uint32_t y = 0; y < source.extent().height; ++y) {
    scanlines[offset++] = 0; // PNG filter: none
    const auto row = source.row(y);
    if (!row || row->size() != source.extent().width) {
      return std::unexpected{"source row is invalid while generating PNG"};
    }
    for (const auto pixel : *row) {
      scanlines[offset++] = pixel.r;
      scanlines[offset++] = pixel.g;
      scanlines[offset++] = pixel.b;
      scanlines[offset++] = pixel.a;
    }
  }

  if (scanlines.size() > std::numeric_limits<uLong>::max()) {
    return std::unexpected{"PNG fixture exceeds zlib's input size"};
  }
  uLongf compressed_size =
      ::compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> compressed(compressed_size);
  const auto zlib_result =
      ::compress2(compressed.data(), &compressed_size, scanlines.data(),
                  static_cast<uLong>(scanlines.size()), Z_BEST_SPEED);
  if (zlib_result != Z_OK) {
    return std::unexpected{"zlib failed to generate the PNG fixture"};
  }
  compressed.resize(compressed_size);

  std::vector<std::uint8_t> png{
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
  };
  std::array<std::uint8_t, 13> ihdr{};
  const auto extent = source.extent();
  ihdr[0] = static_cast<std::uint8_t>(extent.width >> 24U);
  ihdr[1] = static_cast<std::uint8_t>(extent.width >> 16U);
  ihdr[2] = static_cast<std::uint8_t>(extent.width >> 8U);
  ihdr[3] = static_cast<std::uint8_t>(extent.width);
  ihdr[4] = static_cast<std::uint8_t>(extent.height >> 24U);
  ihdr[5] = static_cast<std::uint8_t>(extent.height >> 16U);
  ihdr[6] = static_cast<std::uint8_t>(extent.height >> 8U);
  ihdr[7] = static_cast<std::uint8_t>(extent.height);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA

  append_chunk(png, {'I', 'H', 'D', 'R'}, ihdr);
  append_chunk(png, {'I', 'D', 'A', 'T'}, compressed);
  append_chunk(png, {'I', 'E', 'N', 'D'}, {});
  return png;
}

[[nodiscard]] auto make_fixture(std::string name, rasterforge::Extent extent,
                                std::uint8_t seed)
    -> std::expected<Fixture, std::string> {
  auto source = make_image(extent, seed);
  if (!source) {
    return std::unexpected{source.error()};
  }
  auto backdrop = make_image(extent, static_cast<std::uint8_t>(seed + 91U));
  if (!backdrop) {
    return std::unexpected{backdrop.error()};
  }
  auto encoded = make_png(source->view());
  if (!encoded) {
    return std::unexpected{encoded.error()};
  }
  return Fixture{std::move(name), extent, std::move(*source),
                 std::move(*backdrop), std::move(*encoded)};
}

[[nodiscard]] auto decode_operation(const Fixture *fixture,
                                    std::uint64_t temporary_bytes) -> Outcome {
  rasterforge::DecodeOptions options{};
  options.limits.max_temporary_bytes = temporary_bytes;
  const auto decoded = rasterforge::decode(
      std::as_bytes(std::span{fixture->encoded_png}), options);
  if (!decoded) {
    return error_outcome(decoded.error());
  }
  return checksum(decoded->view());
}

[[nodiscard]] auto fit_operation(const Fixture *fixture,
                                 rasterforge::Extent destination,
                                 rasterforge::Fit policy,
                                 rasterforge::ResizeFilter filter,
                                 std::uint64_t temporary_bytes) -> Outcome {
  rasterforge::Limits limits{};
  limits.max_temporary_bytes = temporary_bytes;
  const auto fitted =
      rasterforge::fit(fixture->source.view(), destination, policy, {},
                       rasterforge::Rgba8{0, 0, 0, 0}, limits, filter);
  if (!fitted) {
    return error_outcome(fitted.error());
  }
  return checksum(fitted->view());
}

[[nodiscard]] auto solid_composite_operation(const Fixture *fixture)
    -> Outcome {
  const auto composited = rasterforge::composite_over(
      fixture->source.view(), rasterforge::Rgba8{24, 32, 48, 255});
  if (!composited) {
    return error_outcome(composited.error());
  }
  return checksum(composited->view());
}

[[nodiscard]] auto image_composite_operation(const Fixture *fixture)
    -> Outcome {
  const auto composited = rasterforge::composite_over(fixture->source.view(),
                                                      fixture->backdrop.view());
  if (!composited) {
    return error_outcome(composited.error());
  }
  return checksum(composited->view());
}

[[nodiscard]] auto bridge_operation(const Fixture *fixture) -> Outcome {
  const auto copied =
      external_rgba::copy_to_external_rgba8(fixture->source.view());
  if (!copied) {
    return Outcome{.message = "external RGBA bridge copy failed"};
  }
  return checksum(copied->view());
}

[[nodiscard]] auto make_cases(const std::vector<Fixture> &fixtures)
    -> std::vector<BenchmarkCase> {
  std::vector<BenchmarkCase> cases;
  cases.reserve((fixtures.size() * 6U) + 2U);
  for (const auto &fixture : fixtures) {
    const auto prefix = fixture.name + "/";
    const auto *value = &fixture;
    cases.push_back(BenchmarkCase{
        prefix + "decode_png",
        fixture.extent,
        fixture.encoded_png.size(),
        true,
        [value](std::uint64_t temporary) {
          return decode_operation(value, temporary);
        },
    });
    cases.push_back(BenchmarkCase{
        prefix + "fit_nearest_stretch",
        fixture.extent,
        0,
        false,
        [value](std::uint64_t temporary) {
          return fit_operation(value, value->extent, rasterforge::Fit::stretch,
                               rasterforge::ResizeFilter::nearest, temporary);
        },
    });
    cases.push_back(BenchmarkCase{
        prefix + "fit_triangle_stretch",
        fixture.extent,
        0,
        true,
        [value](std::uint64_t temporary) {
          return fit_operation(value, value->extent, rasterforge::Fit::stretch,
                               rasterforge::ResizeFilter::triangle, temporary);
        },
    });
    cases.push_back(BenchmarkCase{
        prefix + "composite_solid",
        fixture.extent,
        0,
        false,
        [value](std::uint64_t) { return solid_composite_operation(value); },
    });
    cases.push_back(BenchmarkCase{
        prefix + "composite_image",
        fixture.extent,
        0,
        false,
        [value](std::uint64_t) { return image_composite_operation(value); },
    });
    cases.push_back(BenchmarkCase{
        prefix + "bridge_copy",
        fixture.extent,
        0,
        false,
        [value](std::uint64_t) { return bridge_operation(value); },
    });
  }

  const auto *upper = &fixtures.back();
  constexpr rasterforge::Extent terminal{320, 180};
  cases.push_back(BenchmarkCase{
      "pipeline_768x1024_to_320x180/fit_nearest_cover",
      terminal,
      0,
      false,
      [upper, terminal](std::uint64_t temporary) {
        return fit_operation(upper, terminal, rasterforge::Fit::cover,
                             rasterforge::ResizeFilter::nearest, temporary);
      },
  });
  cases.push_back(BenchmarkCase{
      "pipeline_768x1024_to_320x180/fit_triangle_cover",
      terminal,
      0,
      true,
      [upper, terminal](std::uint64_t temporary) {
        return fit_operation(upper, terminal, rasterforge::Fit::cover,
                             rasterforge::ResizeFilter::triangle, temporary);
      },
  });
  return cases;
}

[[nodiscard]] auto run_checked(const BenchmarkCase &benchmark,
                               std::uint64_t temporary_bytes) -> std::uint64_t {
  auto outcome = benchmark.operation(temporary_bytes);
  if (!outcome.success) {
    throw std::runtime_error{benchmark.name + ": " + outcome.message};
  }
  checksum_sink ^= outcome.checksum + 0x9E3779B97F4A7C15ULL +
                   (checksum_sink << 6U) + (checksum_sink >> 2U);
  return outcome.checksum;
}

[[nodiscard]] auto temporary_floor(const BenchmarkCase &benchmark,
                                   std::uint64_t maximum) -> std::uint64_t {
  if (!benchmark.measure_temporary_floor) {
    return 0;
  }
  const auto full = benchmark.operation(maximum);
  if (!full.success) {
    throw std::runtime_error{
        benchmark.name + ": default temporary budget failed: " + full.message};
  }

  std::uint64_t low = 0;
  std::uint64_t high = maximum;
  while (low < high) {
    const auto midpoint = low + ((high - low) / 2U);
    const auto outcome = benchmark.operation(midpoint);
    if (outcome.success) {
      high = midpoint;
    } else if (outcome.temporary_limit) {
      low = midpoint + 1U;
    } else {
      throw std::runtime_error{
          benchmark.name +
          ": temporary-budget probe failed: " + outcome.message};
    }
  }
  return low;
}

[[nodiscard]] auto elapsed_ns(Clock::time_point started) -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                           started)
          .count());
}

[[nodiscard]] auto measure(const BenchmarkCase &benchmark,
                           const Options &options) -> Result {
  constexpr auto default_temporary = rasterforge::Limits{}.max_temporary_bytes;
  const auto floor = temporary_floor(benchmark, default_temporary);

  (void)run_checked(benchmark, default_temporary); // warmup

  allocation_tracker::begin();
  const auto allocation_outcome = benchmark.operation(default_temporary);
  const auto allocations = allocation_tracker::snapshot();
  allocation_tracker::end();
  if (!allocation_outcome.success) {
    throw std::runtime_error{benchmark.name + ": allocation sample failed: " +
                             allocation_outcome.message};
  }
  checksum_sink ^= allocation_outcome.checksum;

  const auto target_ns =
      static_cast<std::uint64_t>(options.sample_ms) * 1'000'000ULL;
  std::uint64_t iterations = 1;
  for (;;) {
    const auto started = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
      (void)run_checked(benchmark, default_temporary);
    }
    const auto elapsed = elapsed_ns(started);
    if (elapsed >= target_ns ||
        iterations > std::numeric_limits<std::uint64_t>::max() / 2U) {
      break;
    }
    iterations *= 2U;
  }

  std::vector<double> samples;
  samples.reserve(options.samples);
  for (std::uint32_t sample = 0; sample < options.samples; ++sample) {
    const auto started = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
      (void)run_checked(benchmark, default_temporary);
    }
    const auto elapsed = elapsed_ns(started);
    samples.push_back(static_cast<double>(elapsed) /
                      static_cast<double>(iterations));
  }
  std::ranges::sort(samples);

  const auto median_ns = samples[samples.size() / 2U];
  const auto p95_index = static_cast<std::size_t>(
      std::ceil(static_cast<double>(samples.size()) * 0.95) - 1.0);
  const auto p95_ns = samples[std::min(p95_index, samples.size() - 1U)];
  const auto pixels =
      static_cast<std::uint64_t>(benchmark.output_extent.width) *
      benchmark.output_extent.height;
  const auto output_bytes = pixels * sizeof(rasterforge::Rgba8);
  const auto seconds = median_ns / 1'000'000'000.0;

  return Result{
      .name = benchmark.name,
      .output_extent = benchmark.output_extent,
      .encoded_bytes = benchmark.encoded_bytes,
      .output_bytes = output_bytes,
      .temporary_floor_bytes = floor,
      .iterations_per_sample = iterations,
      .median_us = median_ns / 1'000.0,
      .p95_us = p95_ns / 1'000.0,
      .operations_per_second = 1.0 / seconds,
      .megapixels_per_second =
          (static_cast<double>(pixels) / 1'000'000.0) / seconds,
      .output_mib_per_second =
          (static_cast<double>(output_bytes) / (1024.0 * 1024.0)) / seconds,
      .allocations = allocations,
  };
}

[[nodiscard]] constexpr auto compiler_name() -> std::string_view {
#if defined(__clang__)
  return "Clang " __clang_version__;
#elif defined(__GNUC__)
  return "GCC " __VERSION__;
#else
  return "unknown";
#endif
}

[[nodiscard]] constexpr auto platform_name() -> std::string_view {
#if defined(__linux__)
  return "linux";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "unknown";
#endif
}

auto print_metadata(const Options &options) -> void {
  std::printf("# rasterforge_version=%u.%u.%u tweak=%u dirty=%s\n",
              rasterforge::version::major, rasterforge::version::minor,
              rasterforge::version::patch, rasterforge::version::tweak,
              rasterforge::version::dirty ? "true" : "false");
  std::printf("# compiler=%.*s\n", static_cast<int>(compiler_name().size()),
              compiler_name().data());
  std::printf("# build_type=%s\n", RASTERFORGE_BENCHMARK_BUILD_TYPE);
  std::printf("# platform=%.*s hardware_threads=%u pointer_bits=%zu\n",
              static_cast<int>(platform_name().size()), platform_name().data(),
              std::thread::hardware_concurrency(), sizeof(void *) * 8U);
  std::printf("# zlib=%s samples=%u sample_ms=%u\n", ::zlibVersion(),
              options.samples, options.sample_ms);
  std::printf(
      "# allocation_scope=ordinary_cpp_new codec_c_allocations=excluded\n");
}

auto print_table(const std::vector<Result> &results) -> void {
  std::printf("%-55s %10s %10s %10s %10s %12s %12s\n", "case", "median_us",
              "p95_us", "MPix/s", "cpp_allocs", "cpp_peak_B", "temp_floor_B");
  for (const auto &result : results) {
    if (result.allocations.available) {
      std::printf(
          "%-55s %10.3f %10.3f %10.2f %10llu %12llu %12llu\n",
          result.name.c_str(), result.median_us, result.p95_us,
          result.megapixels_per_second,
          static_cast<unsigned long long>(result.allocations.allocation_calls),
          static_cast<unsigned long long>(
              result.allocations.peak_outstanding_bytes),
          static_cast<unsigned long long>(result.temporary_floor_bytes));
    } else {
      std::printf(
          "%-55s %10.3f %10.3f %10.2f %10s %12s %12llu\n", result.name.c_str(),
          result.median_us, result.p95_us, result.megapixels_per_second, "n/a",
          "n/a", static_cast<unsigned long long>(result.temporary_floor_bytes));
    }
  }
}

auto print_csv(const std::vector<Result> &results) -> void {
  std::puts(
      "case,width,height,encoded_bytes,output_bytes,iterations_per_sample,"
      "median_us,p95_us,operations_per_second,megapixels_per_second,"
      "output_mib_per_second,cpp_allocation_calls,cpp_allocated_bytes,"
      "cpp_peak_outstanding_bytes,temporary_floor_bytes");
  for (const auto &result : results) {
    std::printf("%s,%u,%u,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,",
                result.name.c_str(), result.output_extent.width,
                result.output_extent.height,
                static_cast<unsigned long long>(result.encoded_bytes),
                static_cast<unsigned long long>(result.output_bytes),
                static_cast<unsigned long long>(result.iterations_per_sample),
                result.median_us, result.p95_us, result.operations_per_second,
                result.megapixels_per_second, result.output_mib_per_second);
    if (result.allocations.available) {
      std::printf(
          "%llu,%llu,%llu,",
          static_cast<unsigned long long>(result.allocations.allocation_calls),
          static_cast<unsigned long long>(result.allocations.allocated_bytes),
          static_cast<unsigned long long>(
              result.allocations.peak_outstanding_bytes));
    } else {
      std::printf("n/a,n/a,n/a,");
    }
    std::printf("%llu\n",
                static_cast<unsigned long long>(result.temporary_floor_bytes));
  }
}

} // namespace

auto run(int argc, char **argv) -> int {
  const auto options = parse_options(argc, argv);
  if (!options) {
    std::fprintf(stderr, "rasterforge-benchmark: %s\n",
                 options.error().c_str());
    print_usage(stderr);
    return 2;
  }

  try {
    std::vector<Fixture> fixtures;
    fixtures.reserve(3);
    for (auto specification : std::array{
             std::pair{"tiny_1x1", rasterforge::Extent{1, 1}},
             std::pair{"termforge_320x180", rasterforge::Extent{320, 180}},
             std::pair{"venice_proxy_768x1024", rasterforge::Extent{768, 1024}},
         }) {
      const auto seed = static_cast<std::uint8_t>(fixtures.size() * 47U + 11U);
      auto fixture =
          make_fixture(specification.first, specification.second, seed);
      if (!fixture) {
        throw std::runtime_error{specification.first + std::string{": "} +
                                 fixture.error()};
      }
      fixtures.push_back(std::move(*fixture));
    }

    const auto cases = make_cases(fixtures);
    std::vector<Result> results;
    results.reserve(cases.size());
    for (const auto &benchmark : cases) {
      results.push_back(measure(benchmark, *options));
    }

    print_metadata(*options);
    if (options->format == OutputFormat::table) {
      print_table(results);
    } else {
      print_csv(results);
    }
    std::printf("# checksum=%llu\n",
                static_cast<unsigned long long>(checksum_sink));
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "rasterforge-benchmark: %s\n", error.what());
    return 3;
  }
}

} // namespace rasterforge_benchmark

auto main(int argc, char **argv) -> int {
  return rasterforge_benchmark::run(argc, argv);
}
