#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include <cstdio>
#include <string_view>

auto main(int argc, char **argv) -> int {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::printf("%.*s %u.%u.%u\n",
                static_cast<int>(rasterforge::version::program_name.size()),
                rasterforge::version::program_name.data(),
                rasterforge::version::major, rasterforge::version::minor,
                rasterforge::version::patch);
    return 0;
  }

  std::puts("RasterForge is a library. Pass --version for build information.");
  return argc == 1 ? 0 : 2;
}
