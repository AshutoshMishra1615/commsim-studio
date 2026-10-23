#pragma once

#include <imgui.h>
#include <SDL.h>
#include <cstdio>
#include <string>
#include <vector>

namespace fonts {

struct FontSet {
    ImFont* body   = nullptr; // Inter Regular 18px base
    ImFont* header = nullptr; // Inter Bold 20px base
    ImFont* mono   = nullptr; // JetBrains Mono 16px base
};

inline float getDisplayDpiScale() {
    float ddpi = 96.0f, hdpi = 96.0f, vdpi = 96.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 50.0f) {
        return ddpi / 96.0f;
    }
    return 1.0f;
}

// Find font file with fallback paths
inline std::string resolveFontPath(const std::string& filename) {
    const std::vector<std::string> candidates = {
        "resources/fonts/" + filename,
        "../resources/fonts/" + filename,
        "../../resources/fonts/" + filename,
        "/usr/share/fonts/truetype/jetbrains-mono/" + filename,
        "/usr/share/fonts/truetype/dejavu/" + filename
    };

    char* base_path = SDL_GetBasePath();
    if (base_path) {
        std::string base_dir(base_path);
        SDL_free(base_path);
        std::string p1 = base_dir + "resources/fonts/" + filename;
        FILE* f = std::fopen(p1.c_str(), "rb");
        if (f) { std::fclose(f); return p1; }
        
        std::string p2 = base_dir + "../resources/fonts/" + filename;
        f = std::fopen(p2.c_str(), "rb");
        if (f) { std::fclose(f); return p2; }
    }

    for (const auto& path : candidates) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::fclose(f);
            return path;
        }
    }
    return "";
}

inline FontSet loadFonts(float ui_scale = 1.0f) {
    FontSet set;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float dpi_scale = getDisplayDpiScale();
    const float total_scale = dpi_scale * ui_scale;

    const float body_px   = 18.0f * total_scale;
    const float header_px = 20.0f * total_scale;
    const float mono_px   = 16.0f * total_scale;

    std::string inter_reg = resolveFontPath("Inter-Regular.ttf");
    std::string inter_bold = resolveFontPath("Inter-Bold.ttf");
    std::string jb_mono = resolveFontPath("JetBrainsMono-Regular.ttf");

    if (jb_mono.empty()) {
        jb_mono = resolveFontPath("DejaVuSansMono.ttf");
    }

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;

    // 1. Body font (Default)
    if (!inter_reg.empty()) {
        set.body = io.Fonts->AddFontFromFileTTF(inter_reg.c_str(), body_px, &cfg);
    }
    if (!set.body) {
        set.body = io.Fonts->AddFontDefault();
        std::fprintf(stderr, "[Fonts] Warning: Falling back to default font for body\n");
    }
    io.FontDefault = set.body;

    // 2. Header font (Bold, 20px base)
    if (!inter_bold.empty()) {
        set.header = io.Fonts->AddFontFromFileTTF(inter_bold.c_str(), header_px, &cfg);
    } else if (!inter_reg.empty()) {
        set.header = io.Fonts->AddFontFromFileTTF(inter_reg.c_str(), header_px, &cfg);
    } else {
        set.header = set.body;
    }

    // 3. Monospace font (JetBrains Mono, 16px base)
    if (!jb_mono.empty()) {
        set.mono = io.Fonts->AddFontFromFileTTF(jb_mono.c_str(), mono_px, &cfg);
    } else {
        set.mono = set.body;
    }

    io.Fonts->Build();
    return set;
}

} // namespace fonts
