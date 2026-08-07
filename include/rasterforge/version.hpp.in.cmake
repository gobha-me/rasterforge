#pragma once

#include <cstdint>
#include <string_view>

namespace rasterforge::version {

inline constexpr std::string_view program_name{"@PROJECT_NAME@"};
inline constexpr std::uint32_t major{@VERSION_MAJOR@};
inline constexpr std::uint32_t minor{@VERSION_MINOR@};
inline constexpr std::uint32_t patch{@VERSION_PATCH@};
inline constexpr std::uint32_t tweak{@VERSION_TWEAK@};
inline constexpr bool dirty{@VERSION_DIRTY@};

}  // namespace rasterforge::version
