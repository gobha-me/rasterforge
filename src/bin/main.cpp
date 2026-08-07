#include <argparse/argparse.hpp>
#include <cstdlib>
#include <fmt/format.h>
#include <version.hpp>

auto main(int argc, char** argv) -> int {
  argparse::ArgumentParser program(PROGRAM_NAME.data(), fmt::format("{}.{}.{}", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH));

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << '\n';
    std::cerr << program;
    std::exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}
