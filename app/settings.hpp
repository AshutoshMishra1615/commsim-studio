#pragma once

#include <cstdio>
#include <string>

namespace settings {

struct AppSettings {
    float ui_scale    = 1.0f;
    int   scheme_idx  = 0;
    int   window_w    = 1600;
    int   window_h    = 900;
    bool  fullscreen  = false;
    int   active_tab  = 0;
};

inline bool loadSettings(AppSettings& s, const char* filepath = "settings.json") {
    FILE* f = std::fopen(filepath, "r");
    if (!f) return false;

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        float fval = 0.0f;
        int   ival = 0;
        char  bval[32];
        if (std::sscanf(line, " \"ui_scale\": %f", &fval) == 1) {
            if (fval >= 0.75f && fval <= 2.0f) s.ui_scale = fval;
        } else if (std::sscanf(line, " \"scheme_idx\": %d", &ival) == 1) {
            if (ival >= 0 && ival < 8) s.scheme_idx = ival;
        } else if (std::sscanf(line, " \"window_w\": %d", &ival) == 1) {
            if (ival >= 640 && ival <= 3840) s.window_w = ival;
        } else if (std::sscanf(line, " \"window_h\": %d", &ival) == 1) {
            if (ival >= 480 && ival <= 2160) s.window_h = ival;
        } else if (std::sscanf(line, " \"fullscreen\": %31s", bval) == 1) {
            s.fullscreen = (std::string(bval).find("true") != std::string::npos);
        } else if (std::sscanf(line, " \"active_tab\": %d", &ival) == 1) {
            if (ival >= 0 && ival < 7) s.active_tab = ival;
        }
    }
    std::fclose(f);
    return true;
}

inline bool saveSettings(const AppSettings& s, const char* filepath = "settings.json") {
    FILE* f = std::fopen(filepath, "w");
    if (!f) return false;

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"ui_scale\": %.2f,\n", s.ui_scale);
    std::fprintf(f, "  \"scheme_idx\": %d,\n", s.scheme_idx);
    std::fprintf(f, "  \"window_w\": %d,\n", s.window_w);
    std::fprintf(f, "  \"window_h\": %d,\n", s.window_h);
    std::fprintf(f, "  \"fullscreen\": %s,\n", s.fullscreen ? "true" : "false");
    std::fprintf(f, "  \"active_tab\": %d\n", s.active_tab);
    std::fprintf(f, "}\n");
    std::fclose(f);
    return true;
}

} // namespace settings
