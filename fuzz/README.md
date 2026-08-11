# Fuzzing

RasterForge has two Clang/libFuzzer targets. Both use hard resource limits and
run with AddressSanitizer and UndefinedBehaviorSanitizer. They are excluded from
the default build, are never registered with CTest, and are unavailable to
consuming projects because `rasterforge_FUZZERS` follows
`PROJECT_IS_TOP_LEVEL`.

## Decode boundary

The `rasterforge-decode-fuzzer` target feeds arbitrary caller-owned byte spans
through the public `decode()` boundary. Its deliberately small limits cap input
at 4 KiB, dimensions at 64 pixels per axis, output at 16 KiB, and cumulative
codec allocations at 256 KiB. The harness does not write files or diagnostics;
libFuzzer and the sanitizers own process reporting and crash artifacts.

## Fit boundary

The `rasterforge-fit-fuzzer` target constructs tiny caller-owned RGBA images and
exercises `fit()` across contain, cover, stretch, and no-scale policies; nearest
and triangle filters; finite, clamped, and non-finite focal points; arbitrary
mattes; and valid or invalid resource budgets. Source images are at most 16 by
16 pixels. Destinations are capped by limits at 16 pixels per axis, 256 pixels,
1 KiB of output, and 64 KiB of coefficient storage. Every successful result is
checked for exact extent, packed stride and byte count, and readable complete
rows.

The first thirteen input bytes select source width/height, destination
width/height, policy, filter, focal x/y classes, four matte channels, and the
temporary-budget class. Remaining bytes become row-major source pixels. Missing
bytes read as zero, so empty and truncated inputs remain valid fuzz cases.

## Bounded local smoke

Configure and run both targets with Clang:

```bash
cmake -B build-fuzz \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fuzz.cmake \
  -Drasterforge_BUILD_BIN=OFF \
  -Drasterforge_TESTS=OFF \
  -Drasterforge_INSTALL=OFF \
  -Drasterforge_FUZZERS=ON
cmake --build build-fuzz --parallel \
  --target rasterforge-decode-fuzzer rasterforge-fit-fuzzer
cmake -E copy_directory fuzz/corpus/decode build-fuzz/decode-corpus
cmake -E copy_directory fuzz/corpus/fit build-fuzz/fit-corpus
mkdir -p build-fuzz/decode-artifacts build-fuzz/fit-artifacts
./build-fuzz/fuzz/rasterforge-decode-fuzzer build-fuzz/decode-corpus \
  -runs=256 -max_len=4096 -timeout=2 -rss_limit_mb=512 \
  -artifact_prefix=build-fuzz/decode-artifacts/
./build-fuzz/fuzz/rasterforge-fit-fuzzer build-fuzz/fit-corpus \
  -runs=512 -max_len=1024 -timeout=2 -rss_limit_mb=512 \
  -artifact_prefix=build-fuzz/fit-artifacts/
```

Run against the build-tree corpus copies shown above: libFuzzer evolves the
directory it receives, while the checked-in seed corpora must remain
reproducible and unchanged.

## Corpus provenance

`corpus/decode/` is generated from the exact byte arrays used by
`test/26png-decode/fixtures.hpp`. It contains the minimal valid normalization
fixtures plus the signature mismatches, truncations, corrupt payload, unknown
critical chunk, and hostile headers exercised by the unit-test failure matrix.

`corpus/fit/` contains compact hand-specified control streams covering every
fit policy and filter plus zero dimensions, non-finite and clamped focal
coordinates, invalid selectors, output limits, temporary limits, and arbitrary
pixel data. Both corpora are produced and checked by the same generator:

```bash
python3 fuzz/generate_corpus.py
python3 fuzz/generate_corpus.py --check
```

Every fuzz-discovered crash or timeout must be minimized, retained in this
corpus set, and added to an ordinary bounded regression test before the fix
lands.
