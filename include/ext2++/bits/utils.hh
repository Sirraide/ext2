#ifndef EXT2_UTILS_HH
#define EXT2_UTILS_HH

#include <algorithm>
#include <cassert>
#include <clopts.hh>
#include <deque>
#include <fmt/color.h>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// ===========================================================================
///  Forward declarations and primitive types.
/// ===========================================================================
namespace Ext2 {
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;
using usz = size_t;
using isz = ptrdiff_t;

/// Log a message to stderr.
template <typename... Arguments>
void Log(fmt::format_string<Arguments...> const& Format, Arguments&&... Args) {
    fmt::print(stderr, Format, std::forward<Arguments>(Args)...);
    fmt::print(stderr, "\n");
}
} // namespace ext2

#endif // EXT2_UTILS_HH
