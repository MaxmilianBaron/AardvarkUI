#pragma once

#include <aardvark/ui.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace aardvark::ui::detail {

inline bool viewport_size(Point size, DWORD &width, DWORD &height) noexcept {
    const auto maximum = static_cast<double>(std::numeric_limits<LONG>::max());
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.y < 1 ||
        static_cast<double>(size.x) > maximum || static_cast<double>(size.y) > maximum)
        return false;
    width = static_cast<DWORD>(size.x);
    height = static_cast<DWORD>(size.y);
    return true;
}

inline bool scissor(Rect bounds, DWORD width, DWORD height, RECT &result) noexcept {
    if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) || !std::isfinite(bounds.max.x) ||
        !std::isfinite(bounds.max.y) || width > static_cast<DWORD>(std::numeric_limits<LONG>::max()) ||
        height > static_cast<DWORD>(std::numeric_limits<LONG>::max()))
        return false;
    const auto clamp = [](float value, DWORD extent) {
        return static_cast<LONG>(std::clamp(static_cast<double>(value), 0.0, static_cast<double>(extent)));
    };
    result = {clamp(bounds.min.x, width), clamp(bounds.min.y, height), clamp(bounds.max.x, width),
              clamp(bounds.max.y, height)};
    return result.left < result.right && result.top < result.bottom;
}

inline UINT vertex_batch(std::size_t remaining, DWORD max_primitives) noexcept {
    const auto primitives = std::min<std::size_t>({remaining / 3, max_primitives, 60000});
    return static_cast<UINT>(primitives * 3);
}

}
