#include "Brand.h"

#include <windows.h>

#include <GL/gl.h>

#include <cstdint>

namespace afm::app::brand {

namespace {

#include "BrandLogo.inc"

GLuint g_wordmark = 0;

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

} // namespace

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.GrabMinSize = 14.0f;
    style.TabBarOverlineSize = 2.0f;
    style.TableAngledHeadersAngle = 50.0f * 3.14159265f / 180.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgb(242, 245, 250);
    c[ImGuiCol_TextDisabled] = rgb(135, 146, 166);
    c[ImGuiCol_WindowBg] = kBackground;
    c[ImGuiCol_ChildBg] = rgb(10, 13, 18);
    c[ImGuiCol_PopupBg] = rgb(14, 18, 25, 0.98f);
    c[ImGuiCol_Border] = rgb(26, 32, 41);
    c[ImGuiCol_FrameBg] = rgb(20, 25, 34);
    c[ImGuiCol_FrameBgHovered] = rgb(28, 35, 48);
    c[ImGuiCol_FrameBgActive] = rgb(35, 44, 60);
    c[ImGuiCol_Button] = rgb(22, 28, 38);
    c[ImGuiCol_ButtonHovered] = rgb(31, 40, 54);
    c[ImGuiCol_ButtonActive] = rgb(40, 52, 74);
    c[ImGuiCol_Header] = rgb(22, 32, 46);
    c[ImGuiCol_HeaderHovered] = rgb(26, 140, 240, 0.35f);
    c[ImGuiCol_HeaderActive] = rgb(26, 140, 240, 0.5f);
    c[ImGuiCol_Separator] = rgb(26, 32, 41);
    c[ImGuiCol_Tab] = rgb(14, 18, 25);
    c[ImGuiCol_TabHovered] = rgb(26, 140, 240, 0.45f);
    c[ImGuiCol_TabSelected] = rgb(20, 46, 82);
    c[ImGuiCol_TabSelectedOverline] = rgb(18, 184, 242);
    c[ImGuiCol_SliderGrab] = rgb(18, 184, 242);
    c[ImGuiCol_SliderGrabActive] = rgb(120, 214, 255);
    c[ImGuiCol_CheckMark] = rgb(18, 184, 242);
    c[ImGuiCol_PlotHistogram] = rgb(26, 140, 240);
    c[ImGuiCol_TableHeaderBg] = rgb(14, 18, 25);
    c[ImGuiCol_TableBorderStrong] = rgb(26, 32, 41);
    c[ImGuiCol_TableBorderLight] = rgb(20, 25, 33);
    c[ImGuiCol_ResizeGrip] = rgb(26, 140, 240, 0.2f);
    c[ImGuiCol_ResizeGripHovered] = rgb(26, 140, 240, 0.6f);
    c[ImGuiCol_NavCursor] = rgb(18, 184, 242);
    style.ScaleAllSizes(scale);
}

void loadTextures() {
    glGenTextures(1, &g_wordmark);
    glBindTexture(GL_TEXTURE_2D, g_wordmark);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWordmarkWidth, kWordmarkHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWordmarkRgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void releaseTextures() {
    if (g_wordmark != 0) glDeleteTextures(1, &g_wordmark);
    g_wordmark = 0;
}

void wordmark(float height) {
    if (g_wordmark == 0) return;
    const float width = height * static_cast<float>(kWordmarkWidth) / static_cast<float>(kWordmarkHeight);
    ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(g_wordmark)), ImVec2(width, height));
}

void gradientRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    dl->AddRectFilledMultiColor(p0, p1, kCyan, kBlue, kViolet, kBlue);
}

} // namespace afm::app::brand
