#pragma once

#include <imgui.h>
#include <implot.h>

namespace theme {

// ============================================================================
// Visual Studio Code "Dark+ (default dark)" Palette Constants
// ============================================================================
constexpr ImVec4 kEditorBg       = ImVec4(0.1176f, 0.1176f, 0.1176f, 1.0f); // #1E1E1E - editor & plot bg
constexpr ImVec4 kSideBarBg      = ImVec4(0.1451f, 0.1451f, 0.1490f, 1.0f); // #252526 - sidebar & panels
constexpr ImVec4 kTabBarBg       = ImVec4(0.1765f, 0.1765f, 0.1765f, 1.0f); // #2D2D2D - tab bar & title bar
constexpr ImVec4 kBorder         = ImVec4(0.2353f, 0.2353f, 0.2353f, 1.0f); // #3C3C3C - borders & separators
constexpr ImVec4 kActiveTab      = ImVec4(0.1176f, 0.1176f, 0.1176f, 1.0f); // #1E1E1E - active tab bg
constexpr ImVec4 kActiveTabTop   = ImVec4(0.0000f, 0.4784f, 0.8000f, 1.0f); // #007ACC - active tab accent
constexpr ImVec4 kInactiveTab    = ImVec4(0.1765f, 0.1765f, 0.1765f, 1.0f); // #2D2D2D - inactive tab bg
constexpr ImVec4 kInactiveText   = ImVec4(0.5882f, 0.5882f, 0.5882f, 1.0f); // #969696 - inactive tab text
constexpr ImVec4 kText           = ImVec4(0.8000f, 0.8000f, 0.8000f, 1.0f); // #CCCCCC - primary text
constexpr ImVec4 kTextMuted      = ImVec4(0.5216f, 0.5216f, 0.5216f, 1.0f); // #858585 - muted text
constexpr ImVec4 kTextWhite      = ImVec4(1.0000f, 1.0000f, 1.0000f, 1.0f); // #FFFFFF - headers & status text
constexpr ImVec4 kStatusBarBg    = ImVec4(0.0000f, 0.4784f, 0.8000f, 1.0f); // #007ACC - status bar blue
constexpr ImVec4 kStatusText     = ImVec4(1.0000f, 1.0000f, 1.0000f, 1.0f); // #FFFFFF - status text
constexpr ImVec4 kButton         = ImVec4(0.0549f, 0.3882f, 0.6118f, 1.0f); // #0E639C - button / accent
constexpr ImVec4 kButtonHover    = ImVec4(0.0667f, 0.4667f, 0.7333f, 1.0f); // #1177BB - hover
constexpr ImVec4 kButtonActive   = ImVec4(0.0353f, 0.2784f, 0.4431f, 1.0f); // #094771 - active / selection
constexpr ImVec4 kSelection      = ImVec4(0.0353f, 0.2784f, 0.4431f, 1.0f); // #094771 - selection
constexpr ImVec4 kFocusOutline   = ImVec4(0.0000f, 0.4980f, 0.8314f, 1.0f); // #007FD4 - focus outline
constexpr ImVec4 kDisabled       = ImVec4(0.3529f, 0.3529f, 0.3529f, 1.0f); // #5A5A5A - disabled controls
constexpr ImVec4 kScrollbar      = ImVec4(0.2588f, 0.2588f, 0.2588f, 1.0f); // #424242 - scrollbar thumb
constexpr ImVec4 kScrollbarHover = ImVec4(0.3098f, 0.3098f, 0.3098f, 1.0f); // #4F4F4F - scrollbar hover

// Data series colours in exact order:
// 1: #4EC9B0 teal
// 2: #569CD6 blue
// 3: #CE9178 orange
// 4: #DCDCAA yellow
// 5: #C586C0 purple
// 6: #F44747 red
constexpr ImVec4 kSeriesTeal     = ImVec4(0.3059f, 0.7882f, 0.6902f, 1.0f);
constexpr ImVec4 kSeriesBlue     = ImVec4(0.3373f, 0.6118f, 0.8392f, 1.0f);
constexpr ImVec4 kSeriesOrange   = ImVec4(0.8078f, 0.5686f, 0.4706f, 1.0f);
constexpr ImVec4 kSeriesYellow   = ImVec4(0.8627f, 0.8627f, 0.6667f, 1.0f);
constexpr ImVec4 kSeriesPurple   = ImVec4(0.7725f, 0.5255f, 0.7529f, 1.0f);
constexpr ImVec4 kSeriesRed      = ImVec4(0.9569f, 0.2784f, 0.2784f, 1.0f);

inline const ImVec4* getSeriesColors() {
    static const ImVec4 colors[6] = {
        kSeriesTeal, kSeriesBlue, kSeriesOrange, kSeriesYellow, kSeriesPurple, kSeriesRed
    };
    return colors;
}

// Apply ImGui style matching VS Code Dark+
inline void applyVSCodeDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Geometry & Control Style
    // Flat: no gradients, no shadows. Rounding 2 px for buttons and inputs, 0 for panels.
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 1.0f;

    // WindowPadding 12, ItemSpacing 8x8, FramePadding 8x6. Thin scrollbars (10px).
    style.WindowPadding     = ImVec2(12.0f, 12.0f);
    style.FramePadding      = ImVec2(8.0f, 6.0f);
    style.ItemSpacing       = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 10.0f;

    // Colors mapping
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = kText;
    colors[ImGuiCol_TextDisabled]          = kTextMuted;
    colors[ImGuiCol_WindowBg]              = kSideBarBg;
    colors[ImGuiCol_ChildBg]               = kSideBarBg;
    colors[ImGuiCol_PopupBg]               = kSideBarBg;
    colors[ImGuiCol_Border]                = kBorder;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = kEditorBg;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
    colors[ImGuiCol_TitleBg]               = kTabBarBg;
    colors[ImGuiCol_TitleBgActive]         = kTabBarBg;
    colors[ImGuiCol_TitleBgCollapsed]      = kTabBarBg;
    colors[ImGuiCol_MenuBarBg]             = kTabBarBg;
    colors[ImGuiCol_ScrollbarBg]           = kTabBarBg;
    colors[ImGuiCol_ScrollbarGrab]         = kScrollbar;
    colors[ImGuiCol_ScrollbarGrabHovered]  = kScrollbarHover;
    colors[ImGuiCol_ScrollbarGrabActive]   = kButton;
    colors[ImGuiCol_CheckMark]             = kFocusOutline;
    colors[ImGuiCol_SliderGrab]            = kButton;
    colors[ImGuiCol_SliderGrabActive]      = kButtonHover;
    colors[ImGuiCol_Button]                = kButton;
    colors[ImGuiCol_ButtonHovered]         = kButtonHover;
    colors[ImGuiCol_ButtonActive]          = kButtonActive;
    colors[ImGuiCol_Header]                = kSelection;
    colors[ImGuiCol_HeaderHovered]         = kButton;
    colors[ImGuiCol_HeaderActive]          = kButtonActive;
    colors[ImGuiCol_Separator]             = kBorder;
    colors[ImGuiCol_SeparatorHovered]      = kFocusOutline;
    colors[ImGuiCol_SeparatorActive]       = kFocusOutline;
    colors[ImGuiCol_ResizeGrip]            = kBorder;
    colors[ImGuiCol_ResizeGripHovered]     = kFocusOutline;
    colors[ImGuiCol_ResizeGripActive]      = kButtonHover;
    colors[ImGuiCol_Tab]                   = kInactiveTab;
    colors[ImGuiCol_TabHovered]            = kButton;
    colors[ImGuiCol_TabActive]             = kActiveTab;
    colors[ImGuiCol_TabUnfocused]          = kInactiveTab;
    colors[ImGuiCol_TabUnfocusedActive]    = kActiveTab;
    colors[ImGuiCol_DockingPreview]        = ImVec4(kButton.x, kButton.y, kButton.z, 0.7f);
    colors[ImGuiCol_DockingEmptyBg]        = kEditorBg;
    colors[ImGuiCol_PlotLines]             = kSeriesBlue;
    colors[ImGuiCol_PlotLinesHovered]      = kSeriesTeal;
    colors[ImGuiCol_PlotHistogram]         = kSeriesOrange;
    colors[ImGuiCol_PlotHistogramHovered]  = kSeriesYellow;
    colors[ImGuiCol_TableHeaderBg]         = kTabBarBg;
    colors[ImGuiCol_TableBorderStrong]     = kBorder;
    colors[ImGuiCol_TableBorderLight]      = kBorder;
    colors[ImGuiCol_TableRowBg]            = kEditorBg;
    colors[ImGuiCol_TableRowBgAlt]         = kSideBarBg;
    colors[ImGuiCol_TextSelectedBg]        = kSelection;
    colors[ImGuiCol_DragDropTarget]        = kFocusOutline;
    colors[ImGuiCol_NavHighlight]          = kFocusOutline;
    colors[ImGuiCol_NavWindowingHighlight] = kFocusOutline;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
}

// Apply ImPlot style matching VS Code Dark+
inline void applyImPlotVSCodeTheme() {
    ImPlotStyle& style = ImPlot::GetStyle();
    
    style.PlotDefaultSize = ImVec2(-1.0f, -1.0f);
    style.PlotMinSize     = ImVec2(240.0f, 180.0f);
    style.PlotPadding     = ImVec2(10.0f, 10.0f);
    style.LabelPadding    = ImVec2(6.0f, 6.0f);
    style.LegendPadding   = ImVec2(8.0f, 6.0f);
    style.LegendInnerPadding = ImVec2(4.0f, 4.0f);
    style.LegendSpacing   = ImVec2(6.0f, 4.0f);

    style.LineWeight      = 1.8f;
    style.MarkerSize      = 5.0f;
    style.MarkerWeight    = 1.5f;
    style.FillAlpha       = 0.25f;

    // ImPlot Colors
    ImVec4* colors = style.Colors;
    colors[ImPlotCol_PlotBg]         = kEditorBg;
    colors[ImPlotCol_PlotBorder]     = kBorder;
    colors[ImPlotCol_LegendBg]       = ImVec4(kSideBarBg.x, kSideBarBg.y, kSideBarBg.z, 0.92f);
    colors[ImPlotCol_LegendBorder]   = kBorder;
    colors[ImPlotCol_LegendText]     = kText;
    colors[ImPlotCol_TitleText]      = kTextWhite;
    colors[ImPlotCol_InlayText]      = kText;
    colors[ImPlotCol_AxisText]       = kText;
    colors[ImPlotCol_AxisGrid]       = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.7f);
    colors[ImPlotCol_AxisTick]       = kBorder;
    colors[ImPlotCol_AxisBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImPlotCol_AxisBgHovered]  = ImVec4(kSelection.x, kSelection.y, kSelection.z, 0.3f);
    colors[ImPlotCol_AxisBgActive]   = ImVec4(kSelection.x, kSelection.y, kSelection.z, 0.5f);
    colors[ImPlotCol_Selection]      = ImVec4(kSelection.x, kSelection.y, kSelection.z, 0.6f);
    colors[ImPlotCol_Crosshairs]     = kFocusOutline;

    // Register customized 6-color palette
    static const ImU32 vscode_palette[6] = {
        ImGui::ColorConvertFloat4ToU32(kSeriesTeal),
        ImGui::ColorConvertFloat4ToU32(kSeriesBlue),
        ImGui::ColorConvertFloat4ToU32(kSeriesOrange),
        ImGui::ColorConvertFloat4ToU32(kSeriesYellow),
        ImGui::ColorConvertFloat4ToU32(kSeriesPurple),
        ImGui::ColorConvertFloat4ToU32(kSeriesRed)
    };
    ImPlot::AddColormap("VSCodeDarkPlus", vscode_palette, 6);
    style.Colormap = ImPlot::GetColormapIndex("VSCodeDarkPlus");
}

} // namespace theme
