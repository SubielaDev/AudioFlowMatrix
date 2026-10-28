#pragma once

// AudioFlow Matrix brand: palette, ImGui theme and the wordmark texture.

#include "imgui.h"

namespace afm::app::brand {

// Logo gradient: cyan -> blue -> violet.
inline constexpr ImU32 kCyan = IM_COL32(18, 184, 242, 255);
inline constexpr ImU32 kBlue = IM_COL32(42, 98, 242, 255);
inline constexpr ImU32 kViolet = IM_COL32(123, 76, 246, 255);
inline constexpr ImVec4 kBackground{0.024f, 0.031f, 0.047f, 1.0f};  // #06080C

void applyTheme(float scale);

// Uploads the wordmark to OpenGL. Call once the GL context is current.
void loadTextures();
void releaseTextures();

// Draws the wordmark `height` pixels tall at the cursor. No-op before loadTextures().
void wordmark(float height);

// Fills [p0, p1] with the logo gradient (cyan top-left, violet bottom-right).
void gradientRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1);

} // namespace afm::app::brand
