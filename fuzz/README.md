# Decode fuzzing

The `rasterforge-decode-fuzzer` target feeds arbitrary caller-owned byte spans
through the public `decode()` boundary. Its deliberately small limits cap input
at 4 KiB, dimensions at 64 pixels per axis, output at 16 KiB, and cumulative
codec allocations at 256 KiB. The harness does not write files or diagnostics;
libFuzzer and the sanitizers own process reporting and crash artifacts.

Configure and run a bounded smoke session with Clang:

```bash
cmake -B build-fuzz \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fuzz.cmake \
  -Drasterforge_BUILD_BIN=OFF \
  -Drasterforge_TESTS=OFF \
  -Drasterforge_INSTALL=OFF \
  -Drasterforge_FUZZERS=ON
cmake --build build-fuzz --target rasterforge-decode-fuzzer --parallel
cmake -E copy_directory fuzz/corpus/decode build-fuzz/fuzz-corpus
mkdir -p build-fuzz/fuzz-artifacts
./build-fuzz/fuzz/rasterforge-decode-fuzzer build-fuzz/fuzz-corpus \
  -runs=256 -max_len=4096 -timeout=2 -rss_limit_mb=512 \
  -artifact_prefix=build-fuzz/fuzz-artifacts/
```

The fuzzer is excluded from the default build and is never registered with
CTest. Consumers receive no fuzz targets because `rasterforge_FUZZERS` follows
`PROJECT_IS_TOP_LEVEL`. Run against the build-tree corpus copy shown above:
libFuzzer evolves the directory it receives, while the checked-in seed corpus
must remain reproducible and unchanged.

## Corpus provenance

`corpus/decode/` is generated from the exact byte arrays used by
`test/26png-decode/fixtures.hpp`. It contains the minimal valid normalization
fixtures plus the signature mismatches, truncations, corrupt payload, unknown
critical chunk, and hostile headers exercised by the unit-test failure matrix.
Regenerate or verify it with:

```bash
python3 fuzz/generate_corpus.py
python3 fuzz/generate_corpus.py --check
```

Every fuzz-discovered crash or timeout must be minimized, retained in this
corpus, and added to an ordinary bounded regression test before the fix lands.
