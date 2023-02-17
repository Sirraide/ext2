#ifndef EXT2_UTILS_HH
#define EXT2_UTILS_HH

#include <algorithm>
#include <cassert>
#include <deque>
#include <memory>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef EXT2XX_LOG_IMPL
#    include <fmt/color.h>
#    include <fmt/chrono.h>
#    include <fmt/format.h>
#endif

#ifndef EXT2XX_UNSIGNED_SIZE_TYPE
#    define EXT2XX_UNSIGNED_SIZE_TYPE std::size_t
#endif

#ifndef EXT2XX_SIGNED_SIZE_TYPE
#    define EXT2XX_SIGNED_SIZE_TYPE std::ptrdiff_t
#endif

#ifndef EXT2XX_FILE_DESCRIPTOR_TYPE
#    define EXT2XX_FILE_DESCRIPTOR_TYPE int
#endif

#ifndef EXT2XX_FORMAT
#    define EXT2XX_FORMAT fmt::format
#endif

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
using usz = EXT2XX_UNSIGNED_SIZE_TYPE;
using isz = EXT2XX_SIGNED_SIZE_TYPE;

#ifdef EXT2XX_LOG_IMPL
/// Log a message to stderr.
void Log(auto&& fmt, auto&&... args) {
    EXT2XX_LOG_IMPL(std::forward<decltype(fmt)>(fmt), std::forward<decltype(args)>(args)...);
    EXT2XX_LOG_IMPL("\n");
}
#else
/// Log a message to stderr.
template <typename... Arguments>
void Log(fmt::format_string<Arguments...> Format, Arguments&&... Args) {
    fmt::print(stderr, Format, std::forward<Arguments>(Args)...);
    fmt::print(stderr, "\n");
}
#endif
} // namespace Ext2

#endif // EXT2_UTILS_HH
