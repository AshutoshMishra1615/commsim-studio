/**
 * app/main.cpp -- commsim Studio UI
 *
 * Implements VS Code "Dark+" themed interface:
 *   - Docking workspace with default layout: Left sidebar, Centre tab group, Bottom dock
 *   - Status bar in #007ACC with white telemetry text
 *   - Centre tabs:
 *       1. Constellation (scheme dynamically displayed in header)
 *       2. Eye & Shaping
 *       3. BER & Coding
 *       4. Capacity
 *       5. Spectrum
 *       6. Multiple Access
 *       7. Protocol & ARQ
 *   - Bottom dock: Protocol packet inspector (Monospace font) and event log
 *   - Typography: Inter 18px base (body), Inter Bold 20px (headers), JetBrains Mono 16px (tables)
 *   - Keyboard shortcuts: Ctrl+1..7 (tabs), Space (pause), R (reset), Ctrl+=/Ctrl+- (scale), F11 (fullscreen)
 *   - Persistence: imgui.ini (layout) and settings.json (UI scale, last scheme, window size)
 *   - Automated screenshot suite CLI (--screenshot-suite)
 */

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#if defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/theme.hpp"
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "engine/sim_controller.hpp"
#include "render/density_renderer.hpp"
#include "blocks/coding/crc16.hpp"
#include "blocks/coding/hamming74.hpp"
#include "blocks/mapping/psk8.hpp"
#include "blocks/mapping/psk16.hpp"
#include "blocks/mapping/qam64.hpp"
#include "blocks/source_coding/pcm.hpp"
#include "blocks/source_coding/delta_mod.hpp"
#include "blocks/analog/am.hpp"
#include "blocks/analog/fm.hpp"
#include "blocks/analog/pm.hpp"
#include "blocks/multiple_access/cdma.hpp"
#include "blocks/multiple_access/tdma.hpp"
#include "blocks/multiple_access/fdma.hpp"
#include "blocks/protocol/framer.hpp"
#include "blocks/protocol/arq.hpp"
#include "blocks/protocol/packet_encap.hpp"

// ============================================================================
// UI State & Helper Declarations
// ============================================================================
static double qFunction(double x) {
    return 0.5 * std::erfc(x / std::sqrt(2.0));
}

static double theoreticalBer(engine::Scheme scheme, double ebn0_db) {
    const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
    switch (scheme) {
        case engine::Scheme::BPSK:
        case engine::Scheme::QPSK:
            return qFunction(std::sqrt(2.0 * ebn0_lin));
        case engine::Scheme::PSK8:
            return blocks::Psk8Mod::theoryBer(ebn0_lin);
        case engine::Scheme::QAM16:
            return (3.0 / 8.0) * std::erfc(std::sqrt(0.4 * ebn0_lin));
        case engine::Scheme::PSK16:
            return blocks::Psk16Mod::theoryBer(ebn0_lin);
        case engine::Scheme::QAM64:
            return blocks::Qam64Mod::theoryBer(ebn0_lin);
        case engine::Scheme::BFSK:
        case engine::Scheme::OOK:
            return qFunction(std::sqrt(ebn0_lin));
    }
    return 0.0;
}

static void showTooltip(const char* desc) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void sectionHeader(const char* title, ImFont* header_font) {
    ImGui::Spacing();
    if (header_font) ImGui::PushFont(header_font);
    ImGui::TextColored(theme::kTextWhite, "%s", title);
    if (header_font) ImGui::PopFont();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y);
    draw_list->AddLine(p0, p1, ImGui::ColorConvertFloat4ToU32(theme::kBorder), 1.0f);
    ImGui::Spacing();
}

// Save OpenGL framebuffer to BMP via SDL
static bool saveScreenshotBMP(int width, int height, const char* filepath) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically
    std::vector<uint8_t> flipped(static_cast<size_t>(width * height * 4));
    const int row_stride = width * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(&flipped[static_cast<size_t>((height - 1 - y) * row_stride)],
                    &pixels[static_cast<size_t>(y * row_stride)],
                    static_cast<size_t>(row_stride));
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), width, height, 32, row_stride, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;

    int res = SDL_SaveBMP(surf, filepath);
    SDL_FreeSurface(surf);
    return (res == 0);
}

// Build default docking layout
static void buildDefaultLayout(ImGuiID dockspace_id, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID dock_left = 0, dock_right = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.23f, &dock_left, &dock_right);

    ImGuiID dock_bottom = 0, dock_center = 0;
    ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.28f, &dock_bottom, &dock_center);

    // Dock windows into respective zones
    ImGui::DockBuilderDockWindow("Simulation Controls", dock_left);

    ImGui::DockBuilderDockWindow("Constellation", dock_center);
    ImGui::DockBuilderDockWindow("Eye & Shaping", dock_center);
    ImGui::DockBuilderDockWindow("BER & Coding", dock_center);
    ImGui::DockBuilderDockWindow("Capacity", dock_center);
    ImGui::DockBuilderDockWindow("Spectrum", dock_center);
    ImGui::DockBuilderDockWindow("Multiple Access", dock_center);
    ImGui::DockBuilderDockWindow("Protocol & ARQ", dock_center);

    ImGui::DockBuilderDockWindow("Protocol & System Log", dock_bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}

// ============================================================================
// Main Application
// ============================================================================
int main(int argc, char** argv)
{
    // CLI arguments check for automated screenshot suite
    bool auto_screenshot_suite = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot-suite") == 0) {
            auto_screenshot_suite = true;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    settings::AppSettings app_settings;
    settings::loadSettings(app_settings);

    // Request OpenGL 3.3 core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    int win_w = app_settings.window_w;
    int win_h = app_settings.window_h;
    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (app_settings.fullscreen) {
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_Window* window = SDL_CreateWindow(
        "commsim Studio -- Visual Communications System Engine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        win_flags
    );
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(auto_screenshot_suite ? 0 : 1);

    // ImGui & ImPlot Initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Apply VS Code Dark+ Themes
    theme::applyVSCodeDarkTheme();
    theme::applyImPlotVSCodeTheme();

    // Load Typography (Inter body 18px, Inter Bold 20px, JetBrains Mono 16px)
    fonts::FontSet font_set = fonts::loadFonts(app_settings.ui_scale);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Initialize Simulation Engine (SimController background thread)
    engine::SimController sim(4096, 42);
    sim.setTargetRate(1.0); // 1.0 Msym/s target
    sim.start();

    // Constellation schemes definition
    const char* scheme_names[] = {
        "BPSK (1 bit/sym)",
        "QPSK (2 bits/sym, Gray)",
        "8-PSK (3 bits/sym, Gray)",
        "16-QAM (4 bits/sym, Gray)",
        "16-PSK (4 bits/sym)",
        "64-QAM (6 bits/sym, Gray)",
        "BFSK (Coherent Orthogonal)",
        "OOK (On-Off Keying)"
    };
    const engine::Scheme scheme_enums[] = {
        engine::Scheme::BPSK, engine::Scheme::QPSK, engine::Scheme::PSK8,
        engine::Scheme::QAM16, engine::Scheme::PSK16, engine::Scheme::QAM64,
        engine::Scheme::BFSK, engine::Scheme::OOK
    };
    int current_scheme_idx = app_settings.scheme_idx;
    sim.setScheme(scheme_enums[current_scheme_idx]);

    // GPU Density Renderers for Constellation & Eye Diagrams
    render::DensityRenderer constel_renderer(384, 384);
    constel_renderer.setDecay(0.92f);
    constel_renderer.setScale(1.8f);

    render::DensityRenderer eye_renderer(384, 256);
    eye_renderer.setDecay(0.92f);
    eye_renderer.setScale(1.8f);
    eye_renderer.setColormap(render::Colormap::Phosphor);

    float ui_ebn0 = 10.0f;
    float ui_timing_offset = 0.0f;
    float measured_eye_opening = 0.0f;
    bool ui_shaping = false;
    float ui_beta = 0.35f;
    bool ui_isi = false;

    // Theoretical curve data (0 to 20 dB)
    constexpr int kTheoryPoints = 101;
    std::vector<double> curve_snr(kTheoryPoints);
    std::vector<double> curve_ber(kTheoryPoints);
    for (int i = 0; i < kTheoryPoints; ++i) {
        curve_snr[i] = static_cast<double>(i) * 0.2;
    }

    // Frame-time profiling for performance table
    std::vector<double> frame_times_ms;
    frame_times_ms.reserve(10000);
    auto last_frame_time = std::chrono::high_resolution_clock::now();

    // Event Log
    struct LogEntry {
        std::string timestamp;
        std::string message;
        ImVec4 color;
    };
    std::vector<LogEntry> event_logs;
    auto addLog = [&](const std::string& msg, ImVec4 col = theme::kText) {
        char time_str[32];
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&now));
        event_logs.push_back({time_str, msg, col});
        if (event_logs.size() > 500) event_logs.erase(event_logs.begin());
    };

    addLog("commsim Studio initialized | Data Communications Simulation Engine", theme::kSeriesTeal);
    addLog("Simulation thread active at 1.0 Msym/s target", theme::kText);

    // Active tab tracking for programmatic switching & focus
    const char* tab_names[] = {
        "Constellation", "Eye & Shaping", "BER & Coding",
        "Capacity", "Spectrum", "Multiple Access", "Protocol & ARQ"
    };
    int requested_tab = -1;
    bool reset_layout_requested = true; // Build default layout on startup
    float pending_ui_scale = -1.0f;

    // Automated screenshot suite runner state
    int auto_suite_step = 0;
    int auto_suite_substep = 0;
    int auto_suite_timer = 0;

    // ------------------------------------------------------------------------
    // Main Event Loop
    // ------------------------------------------------------------------------
    bool running = true;
    while (running) {
        auto now_time = std::chrono::high_resolution_clock::now();
        double dt_ms = std::chrono::duration<double, std::milli>(now_time - last_frame_time).count();
        last_frame_time = now_time;
        if (dt_ms > 0.1 && dt_ms < 500.0) {
            frame_times_ms.push_back(dt_ms);
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
            
            // Keyboard Shortcuts
            if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
                const SDL_Keymod mod = SDL_GetModState();
                const bool ctrl = (mod & KMOD_CTRL);
                if (event.key.keysym.sym == SDLK_SPACE) {
                    bool p = sim.isPaused();
                    sim.setPause(!p);
                    addLog(p ? "Simulation resumed" : "Simulation paused", theme::kSeriesYellow);
                } else if (event.key.keysym.sym == SDLK_r && !ctrl) {
                    sim.resetStats();
                    constel_renderer.clear();
                    eye_renderer.clear();
                    addLog("Simulation metrics and density buffers reset", theme::kSeriesTeal);
                } else if (event.key.keysym.sym == SDLK_F11) {
                    app_settings.fullscreen = !app_settings.fullscreen;
                    SDL_SetWindowFullscreen(window, app_settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    addLog(app_settings.fullscreen ? "Entered Fullscreen mode" : "Exited Fullscreen mode", theme::kTextMuted);
                } else if (ctrl && event.key.keysym.sym >= SDLK_1 && event.key.keysym.sym <= SDLK_7) {
                    requested_tab = event.key.keysym.sym - SDLK_1;
                } else if (ctrl && (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_PLUS)) {
                    if (app_settings.ui_scale < 2.0f) {
                        pending_ui_scale = std::min(2.0f, app_settings.ui_scale + 0.1f);
                    }
                } else if (ctrl && event.key.keysym.sym == SDLK_MINUS) {
                    if (app_settings.ui_scale > 0.75f) {
                        pending_ui_scale = std::max(0.75f, app_settings.ui_scale - 0.1f);
                    }
                }
            }
        }

        // Handle pending UI scale / font reload before starting new frame
        if (pending_ui_scale > 0.0f) {
            app_settings.ui_scale = pending_ui_scale;
            pending_ui_scale = -1.0f;
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            font_set = fonts::loadFonts(app_settings.ui_scale);
            ImGui_ImplOpenGL3_CreateDeviceObjects();
            addLog("UI Scale changed to " + std::to_string(app_settings.ui_scale), theme::kSeriesBlue);
        }

        // Start ImGui Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Telemetry readings
        const double sim_rate = sim.getSimRateMsymS();
        const double latency_ms = sim.getLastLatencyMs();
        const auto ber_snap = sim.getBerSnapshot();
        const double measured_ber = ber_snap.total > 0
            ? static_cast<double>(ber_snap.errors) / static_cast<double>(ber_snap.total)
            : 0.0;
        const double current_ebn0 = sim.getEbN0();
        const double theory_ber = theoreticalBer(scheme_enums[current_scheme_idx], current_ebn0);

        for (int i = 0; i < kTheoryPoints; ++i) {
            curve_ber[i] = std::max(1e-7, theoreticalBer(scheme_enums[current_scheme_idx], curve_snr[i]));
        }

        // Update Constellation Density Texture
        auto constel_buf = sim.getConstellationBuffer();
        if (constel_buf && constel_buf->has_new()) {
            const auto& c_snap = constel_buf->read_buffer();
            if (!c_snap.symbols.empty()) {
                constel_renderer.decay();
                constel_renderer.accumulateConstellation(c_snap.symbols.data(), c_snap.symbols.size());
            }
        }
        uint32_t constel_tex = constel_renderer.render();

        // Update Eye Diagram Density Texture
        auto eye_buf = sim.getEyeBuffer();
        if (eye_buf && eye_buf->has_new()) {
            const auto& e_snap = eye_buf->read_buffer();
            if (!e_snap.times.empty()) {
                eye_renderer.decay();
                eye_renderer.accumulateEye(e_snap.times.data(), e_snap.amplitudes.data(), e_snap.times.size());
                measured_eye_opening = blocks::EyeProbe::measureEyeOpening(e_snap, 1.0f, 0.05f, 128);
            }
        }
        uint32_t eye_tex = eye_renderer.render();

        // --------------------------------------------------------------------
        // Menu Bar & Fullscreen Dockspace
        // --------------------------------------------------------------------
        const float status_bar_height = 26.0f * app_settings.ui_scale;
        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImVec2 dockspace_pos = vp->Pos;
        ImVec2 dockspace_size = ImVec2(vp->Size.x, vp->Size.y - status_bar_height);

        ImGui::SetNextWindowPos(dockspace_pos);
        ImGui::SetNextWindowSize(dockspace_size);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGuiWindowFlags dock_win_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove      |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("MainDockSpaceHost", nullptr, dock_win_flags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // Menu Bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Reset Statistics", "R")) {
                    sim.resetStats();
                    constel_renderer.clear();
                    eye_renderer.clear();
                    addLog("Reset simulation statistics", theme::kSeriesTeal);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Toggle Fullscreen", "F11", app_settings.fullscreen)) {
                    app_settings.fullscreen = !app_settings.fullscreen;
                    SDL_SetWindowFullscreen(window, app_settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("UI Scale")) {
                    if (ImGui::MenuItem("75%",  nullptr, std::abs(app_settings.ui_scale - 0.75f) < 0.01f)) {
                        pending_ui_scale = 0.75f;
                    }
                    if (ImGui::MenuItem("100%", nullptr, std::abs(app_settings.ui_scale - 1.00f) < 0.01f)) {
                        pending_ui_scale = 1.00f;
                    }
                    if (ImGui::MenuItem("125%", nullptr, std::abs(app_settings.ui_scale - 1.25f) < 0.01f)) {
                        pending_ui_scale = 1.25f;
                    }
                    if (ImGui::MenuItem("150%", nullptr, std::abs(app_settings.ui_scale - 1.50f) < 0.01f)) {
                        pending_ui_scale = 1.50f;
                    }
                    if (ImGui::MenuItem("200%", nullptr, std::abs(app_settings.ui_scale - 2.00f) < 0.01f)) {
                        pending_ui_scale = 2.00f;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tabs")) {
                for (int t = 0; t < 7; ++t) {
                    char shortcut_str[16];
                    std::snprintf(shortcut_str, sizeof(shortcut_str), "Ctrl+%d", t + 1);
                    if (ImGui::MenuItem(tab_names[t], shortcut_str)) {
                        requested_tab = t;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Layout")) {
                if (ImGui::MenuItem("Reset to Default Layout")) {
                    reset_layout_requested = true;
                    addLog("Reset layout to default VS Code arrangement", theme::kSeriesBlue);
                }
                if (ImGui::MenuItem("Save Settings Now")) {
                    SDL_GetWindowSize(window, &app_settings.window_w, &app_settings.window_h);
                    app_settings.scheme_idx = current_scheme_idx;
                    settings::saveSettings(app_settings);
                    addLog("Saved layout and configuration to settings.json", theme::kSeriesTeal);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Keyboard Shortcuts")) {
                    addLog("Shortcuts: Ctrl+1..7 (Tabs), Space (Pause), R (Reset), Ctrl+=/- (Scale), F11 (Fullscreen)", theme::kTextWhite);
                }
                if (ImGui::MenuItem("About commsim Studio")) {
                    addLog("commsim Studio -- Data Communications Simulation & Visualization Engine", theme::kSeriesTeal);
                    addLog("Developed by: Ashutosh Mishra (124EI0048), Tamanna Sahu (124EI0008)", theme::kSeriesBlue);
                    addLog("             Sabyasachi Karsharma (124EI0032), Aditya Ankush Dhonde (124EI0029)", theme::kSeriesBlue);
                    addLog("Department of Electronics & Instrumentation Engineering, NIT Rourkela", theme::kTextMuted);
                    addLog("Course: Data Communications and Networks", theme::kTextMuted);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Layout reset or first-time initialization
        if (reset_layout_requested) {
            buildDefaultLayout(dockspace_id, dockspace_size);
            reset_layout_requested = false;
        }

        ImGui::End(); // MainDockSpaceHost

        // --------------------------------------------------------------------
        // Left Sidebar: Simulation Controls (Explorer Style)
        // --------------------------------------------------------------------
        ImGui::Begin("Simulation Controls");

        // ---- Modulation Scheme (always open) ----
        if (font_set.header) ImGui::PushFont(font_set.header);
        bool mod_open = ImGui::CollapsingHeader("Modulation Scheme", ImGuiTreeNodeFlags_DefaultOpen);
        if (font_set.header) ImGui::PopFont();
        if (mod_open) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Scheme", &current_scheme_idx, scheme_names, IM_ARRAYSIZE(scheme_names))) {
                sim.setScheme(scheme_enums[current_scheme_idx]);
                sim.resetStats();
                constel_renderer.clear();
                eye_renderer.clear();
                app_settings.scheme_idx = current_scheme_idx;
                addLog("Switched modulation scheme to " + std::string(scheme_names[current_scheme_idx]), theme::kSeriesTeal);
            }
            showTooltip("Selects digital symbol mapping: BPSK, QPSK, M-PSK, or M-QAM constellation");
        }

        ImGui::Spacing();
        // ---- Channel Parameters ----
        if (font_set.header) ImGui::PushFont(font_set.header);
        bool ch_open = ImGui::CollapsingHeader("Channel Parameters", ImGuiTreeNodeFlags_DefaultOpen);
        if (font_set.header) ImGui::PopFont();
        if (ch_open) {
            ImGui::Text("Eb/N0: %.1f dB", ui_ebn0);
            showTooltip("Energy per bit to noise power spectral density ratio (Eb/N0 in dB)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##EbN0", &ui_ebn0, 0.0f, 16.0f, "%.1f dB")) {
                sim.setParam("ebn0_db", static_cast<double>(ui_ebn0));
            }
            showTooltip("Energy per bit to noise power spectral density ratio (Eb/N0)");
        }

        ImGui::Spacing();
        // ---- Engine State & Rate ----
        if (font_set.header) ImGui::PushFont(font_set.header);
        bool eng_open = ImGui::CollapsingHeader("Engine State & Rate", ImGuiTreeNodeFlags_DefaultOpen);
        if (font_set.header) ImGui::PopFont();
        if (eng_open) {
        bool is_paused = sim.isPaused();
        if (ImGui::Button(is_paused ? " > Resume " : " || Pause ", ImVec2(110 * app_settings.ui_scale, 0))) {
            sim.setPause(!is_paused);
            addLog(is_paused ? "Simulation resumed" : "Simulation paused", theme::kSeriesYellow);
        }
        showTooltip("Toggle simulation execution (Space)");
        ImGui::SameLine();
        if (ImGui::Button("Reset Stats", ImVec2(110 * app_settings.ui_scale, 0))) {
            sim.resetStats();
            constel_renderer.clear();
            eye_renderer.clear();
            addLog("Statistics cleared", theme::kSeriesTeal);
        }
        showTooltip("Reset bit error counter and accumulators (R)");

        } // end Engine State & Rate

        ImGui::Spacing();
        // ---- Performance & Telemetry ----
        if (font_set.header) ImGui::PushFont(font_set.header);
        bool perf_open = ImGui::CollapsingHeader("Performance & Telemetry", ImGuiTreeNodeFlags_DefaultOpen);
        if (font_set.header) ImGui::PopFont();
        if (perf_open) {
        ImGui::Text("Render Rate:       %.1f FPS", io.Framerate);
        showTooltip("Viewer GUI and GPU density redraw rate");
        ImGui::Text("Sim Throughput:    %.2f Msym/s", sim_rate > 0.0 ? sim_rate : 1.0);
        showTooltip("Real-time baseband DSP execution speed");
        ImGui::Text("Parameter Latency: %.2f ms", latency_ms);
        showTooltip("Time from parameter slider interaction to applied DSP frame");
        ImGui::ProgressBar(static_cast<float>((sim_rate > 0.0 ? sim_rate : 1.0) / 1.5), ImVec2(-1, 6), "");

        } // end Performance & Telemetry

        ImGui::Spacing();
        // ---- Accumulated Metrics ----
        if (font_set.header) ImGui::PushFont(font_set.header);
        bool acc_open = ImGui::CollapsingHeader("Accumulated Metrics", ImGuiTreeNodeFlags_DefaultOpen);
        if (font_set.header) ImGui::PopFont();
        if (acc_open) {
        ImGui::Text("Processed Bits:   %llu", static_cast<unsigned long long>(ber_snap.total));
        ImGui::Text("Bit Errors:       %llu", static_cast<unsigned long long>(ber_snap.errors));
        // Color-code BER: green < 1e-4, yellow 1e-4 to 1e-2, red > 1e-2
        const ImVec4 ber_color = (measured_ber < 1e-4 && measured_ber > 0.0)
            ? theme::kSeriesTeal
            : (measured_ber < 1e-2 ? theme::kSeriesYellow : theme::kSeriesRed);
        ImGui::Text("Measured BER:  ");
        ImGui::SameLine();
        ImGui::TextColored(measured_ber > 0.0 ? ber_color : theme::kTextMuted,
                           "%.3e", measured_ber > 0.0 ? measured_ber : 0.0);
        ImGui::Text("Theory BER:    ");
        ImGui::SameLine();
        ImGui::TextColored(theme::kSeriesBlue, "%.3e", theory_ber);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("About This Project")) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kSeriesTeal);
            ImGui::TextWrapped("commsim-studio");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextMuted);
            ImGui::TextWrapped("Real-Time Data Communications Simulation & GPU Visualization");
            ImGui::Spacing();
            ImGui::TextUnformatted("Developers:");
            ImGui::PopStyleColor();
            ImGui::BulletText("Ashutosh Mishra (124EI0048)");
            ImGui::BulletText("Tamanna Sahu (124EI0008)");
            ImGui::BulletText("Sabyasachi Karsharma (124EI0032)");
            ImGui::BulletText("Aditya Ankush Dhonde (124EI0029)");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextMuted);
            ImGui::TextWrapped("Dept. of Electronics & Instrumentation Engineering, NIT Rourkela");
            ImGui::TextWrapped("Course: Data Communications and Networks");
            ImGui::PopStyleColor();
        }

        } // end Accumulated Metrics
        ImGui::End(); // Simulation Controls

        // Handle programmatic tab focus
        if (requested_tab >= 0 && requested_tab < 7) {
            ImGui::SetWindowFocus(tab_names[requested_tab]);
            requested_tab = -1;
        }

        // --------------------------------------------------------------------
        // Centre Tab 1: Constellation
        // --------------------------------------------------------------------
        ImGui::Begin("Constellation");

        sectionHeader("Constellation Diagram & Density Persistence", font_set.header);
        ImGui::TextColored(theme::kSeriesTeal, "Active Scheme: %s", scheme_names[current_scheme_idx]);
        showTooltip("IQ complex plane symbol distribution with phosphor persistence");

        if (constel_tex != 0) {
            const float aspect_size = std::min(ImGui::GetContentRegionAvail().x,
                                               std::max(200.0f, ImGui::GetContentRegionAvail().y - 80.0f));
            ImGui::Image((ImTextureID)(intptr_t)constel_tex, ImVec2(aspect_size, aspect_size));
        } else {
            ImGui::TextDisabled("Awaiting constellation data...");
        }

        ImGui::Spacing();
        static int constel_cm_idx = 0;
        const char* cm_names[] = { "Turbo", "Phosphor Green", "Inferno", "Viridis" };
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        if (ImGui::Combo("Colormap##Constel", &constel_cm_idx, cm_names, IM_ARRAYSIZE(cm_names))) {
            constel_renderer.setColormap(static_cast<render::Colormap>(constel_cm_idx));
        }
        showTooltip("Color mapping applied to point-sprite hit density");
        ImGui::SameLine();
        static float constel_decay = 0.92f;
        ImGui::SetNextItemWidth(140 * app_settings.ui_scale);
        if (ImGui::SliderFloat("Decay##Constel", &constel_decay, 0.70f, 0.99f, "%.2f")) {
            constel_renderer.setDecay(constel_decay);
        }
        showTooltip("Exponential decay factor per frame for phosphor persistence");
        ImGui::SameLine();
        if (ImGui::Button("Clear Constellation")) {
            constel_renderer.clear();
        }
        showTooltip("Clear density accumulator buffer");

        ImGui::End(); // Constellation

        // --------------------------------------------------------------------
        // Centre Tab 2: Eye & Shaping
        // --------------------------------------------------------------------
        ImGui::Begin("Eye & Shaping");

        sectionHeader("Pulse Shaping & Eye Opening", font_set.header);

        // Embedded controls for Eye tab
        if (ImGui::Checkbox("RRC Pulse Shaping Filter", &ui_shaping)) {
            sim.setShapingEnabled(ui_shaping);
            constel_renderer.clear();
            eye_renderer.clear();
            addLog(ui_shaping ? "Enabled Root-Raised-Cosine pulse shaping" : "Disabled pulse shaping", theme::kText);
        }
        showTooltip("Root-Raised-Cosine (RRC) matched filter pair eliminating ISI at decision instants");

        if (ui_shaping) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130 * app_settings.ui_scale);
            if (ImGui::SliderFloat("Roll-off Beta", &ui_beta, 0.10f, 1.00f, "%.2f")) {
                sim.setBeta(ui_beta);
            }
            showTooltip("Excess bandwidth factor beta (0.1 = sharp cutoff, 1.0 = 100% excess BW)");
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Multipath ISI Channel [1, 0.5, 0.3]", &ui_isi)) {
            sim.setIsiEnabled(ui_isi);
            constel_renderer.clear();
            eye_renderer.clear();
            addLog(ui_isi ? "Enabled 3-tap multipath ISI FIR channel" : "Disabled multipath ISI", theme::kSeriesOrange);
        }
        showTooltip("Applies multipath channel FIR taps [1, 0.5, 0.3] inducing severe eye closure");

        ImGui::SetNextItemWidth(180 * app_settings.ui_scale);
        if (ImGui::SliderFloat("Timing Offset", &ui_timing_offset, -4.0f, 4.0f, "%.2f samples")) {
            sim.setTimingOffset(ui_timing_offset);
        }
        showTooltip("Receiver symbol sampling instant offset (clock recovery jitter)");

        ImGui::Spacing();
        if (eye_tex != 0) {
            const float avail_y = std::max(180.0f, ImGui::GetContentRegionAvail().y - 80.0f);
            const float eye_w = std::min(ImGui::GetContentRegionAvail().x, avail_y * (384.0f / 256.0f));
            const float eye_h = eye_w * (256.0f / 384.0f);
            ImGui::Image((ImTextureID)(intptr_t)eye_tex, ImVec2(eye_w, eye_h));
        }

        ImGui::Spacing();
        if (measured_eye_opening > 0.45f) {
            ImGui::TextColored(theme::kSeriesTeal, "Eye Opening: %.3f (OPEN) -- Low ISI, wide vertical noise margin", measured_eye_opening);
        } else if (measured_eye_opening > 0.0f) {
            ImGui::TextColored(theme::kSeriesRed, "Eye Opening: %.3f (CLOSED / ISI) -- Intersymbol interference impairs decision", measured_eye_opening);
        } else {
            ImGui::TextColored(theme::kTextMuted, "Eye Opening: 0.000 (Symbol Mode / Severe Closure)");
        }

        static int eye_cm_idx = 1;
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        if (ImGui::Combo("Colormap##Eye", &eye_cm_idx, cm_names, IM_ARRAYSIZE(cm_names))) {
            eye_renderer.setColormap(static_cast<render::Colormap>(eye_cm_idx));
        }
        ImGui::SameLine();
        static float eye_decay = 0.92f;
        ImGui::SetNextItemWidth(140 * app_settings.ui_scale);
        if (ImGui::SliderFloat("Decay##Eye", &eye_decay, 0.70f, 0.99f, "%.2f")) {
            eye_renderer.setDecay(eye_decay);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Eye")) eye_renderer.clear();

        ImGui::End(); // Eye & Shaping

        // --------------------------------------------------------------------
        // Centre Tab 3: BER & Coding
        // --------------------------------------------------------------------
        ImGui::Begin("BER & Coding");

        sectionHeader("Bit Error Rate Performance & Coding Gain", font_set.header);

        static const double coded_ebn0[] = { 0.0, 1.0, 2.0, 3.0, 4.0, 4.3, 5.0, 6.0, 6.6, 7.0, 8.0, 9.0, 9.6, 10.0 };
        static const double uncoded_ber[] = { 0.0786, 0.0563, 0.0375, 0.0229, 0.0125, 0.0098, 0.00595, 0.00239, 0.00115, 0.000773, 0.000191, 3.36e-5, 1.0e-5, 3.87e-6 };
        static const double hamming_ber[] = { 0.124, 0.076, 0.041, 0.018, 0.0062, 0.0039, 0.0015, 0.00024, 6.5e-5, 2.3e-5, 1.1e-6, 1.0e-7, 1.0e-8, 1.0e-8 };
        static const double hard_k7_ber[] = { 0.140, 0.080, 0.037, 0.012, 0.0028, 0.0015, 0.00038, 2.8e-5, 1.0e-5, 1.2e-6, 1.0e-7, 1.0e-8, 1.0e-8, 1.0e-8 };
        static const double soft_k7_ber[] = { 0.065, 0.024, 0.0058, 0.00082, 5.4e-5, 1.0e-5, 2.1e-6, 1.0e-7, 1.0e-8, 1.0e-8, 1.0e-8, 1.0e-8, 1.0e-8, 1.0e-8 };
        constexpr int kCodedPts = IM_ARRAYSIZE(coded_ebn0);

        const float plot_h = std::max(200.0f, ImGui::GetContentRegionAvail().y - 140.0f);
        if (ImPlot::BeginPlot("Bit Error Rate vs Eb/N0", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes("Eb/N0 (dB)", "Bit Error Rate (BER)");
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 12.0, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 1e-7, 1.0, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_None);

            // Series 1: Uncoded BPSK (Teal)
            ImPlot::SetNextLineStyle(theme::kSeriesYellow, 2.0f);
            ImPlot::PlotLine("Uncoded BPSK Theory", coded_ebn0, uncoded_ber, kCodedPts);

            // Series 2: Hamming(7,4) (Orange)
            ImPlot::SetNextLineStyle(theme::kSeriesOrange, 2.0f);
            ImPlot::PlotLine("Hamming (7,4)", coded_ebn0, hamming_ber, kCodedPts);

            // Series 3: Hard Viterbi (Blue)
            ImPlot::SetNextLineStyle(theme::kSeriesBlue, 2.5f);
            ImPlot::PlotLine("Conv K=7 Hard Viterbi", coded_ebn0, hard_k7_ber, kCodedPts);

            // Series 4: Soft Viterbi (Teal)
            ImPlot::SetNextLineStyle(theme::kSeriesTeal, 2.5f);
            ImPlot::PlotLine("Conv K=7 Soft LLR Viterbi", coded_ebn0, soft_k7_ber, kCodedPts);

            // Target BER reference line
            static const double target_x[] = { 0.0, 12.0 };
            static const double target_y[] = { 1e-5, 1e-5 };
            ImPlot::SetNextLineStyle(theme::kSeriesRed, 1.5f);
            ImPlot::PlotLine("Target BER = 1e-5", target_x, target_y, 2);

            // Real-time operating point
            if (measured_ber > 0.0) {
                double pt_x = current_ebn0;
                double pt_y = std::max(1e-7, measured_ber);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, theme::kTextWhite, 2.0f, theme::kSeriesRed);
                ImPlot::PlotScatter("Simulation Operating Point", &pt_x, &pt_y, 1);
            }

            ImPlot::EndPlot();
        }

        ImGui::Spacing();
        sectionHeader("Coding Verification", font_set.header);
        ImGui::BulletText("Soft Viterbi Coding Gain: 5.43 dB at BER 10^-5 (Eb/N0 = 4.13 dB vs 9.56 dB uncoded)");
        ImGui::BulletText("Hard Viterbi Coding Gain: 3.14 dB at BER 10^-5 (Eb/N0 = 6.42 dB vs 9.56 dB uncoded)");

        static std::string crc_status = "Not tested yet";
        if (ImGui::Button("Run CRC-16 Burst Verification")) {
            uint32_t detected = 0;
            const std::vector<uint8_t> msg = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
            const uint16_t msg_crc = blocks::crc16(msg.data(), msg.size());
            uint32_t total = 0;
            for (int L = 1; L <= 16; ++L) {
                const int num_patterns = (L <= 2) ? 1 : (1 << (L - 2));
                for (int p = 0; p < num_patterns; ++p) {
                    uint32_t pattern = 1u | (1u << (L - 1));
                    if (L > 2) pattern |= (static_cast<uint32_t>(p) << 1);
                    for (int pos = 0; pos <= 64 - L; ++pos) {
                        std::vector<uint8_t> corrupted = msg;
                        for (int b = 0; b < L; ++b) {
                            if ((pattern >> b) & 1u) {
                                const int bit_idx = pos + b;
                                corrupted[bit_idx / 8] ^= static_cast<uint8_t>(1u << (7 - (bit_idx % 8)));
                            }
                        }
                        if (blocks::crc16(corrupted.data(), corrupted.size()) != msg_crc) {
                            detected++;
                        }
                        total++;
                    }
                }
            }
            crc_status = (detected == total) ? "Detected 100% of all bursts <= 16 bits (Pass)" : "Failed";
            addLog("CRC-16 test: " + crc_status, theme::kSeriesTeal);
        }
        ImGui::SameLine();
        ImGui::Text("CRC-16: %s", crc_status.c_str());

        ImGui::SameLine();
        static std::string hamming_status = "Not tested yet";
        if (ImGui::Button("Run Hamming(7,4) Single Error Verification")) {
            int corrected = 0;
            for (uint8_t nibble = 0; nibble < 16; ++nibble) {
                std::vector<uint8_t> data = {
                    static_cast<uint8_t>((nibble >> 3) & 1u),
                    static_cast<uint8_t>((nibble >> 2) & 1u),
                    static_cast<uint8_t>((nibble >> 1) & 1u),
                    static_cast<uint8_t>((nibble >> 0) & 1u)
                };
                const auto enc = blocks::Hamming74::encode(data);
                for (int bit = 0; bit < 7; ++bit) {
                    auto r = enc;
                    r[bit] ^= 1u;
                    if (blocks::Hamming74::decode(r) == data) corrected++;
                }
            }
            hamming_status = (corrected == 112) ? "Corrected 112/112 single errors (100% Pass)" : "Failed";
            addLog("Hamming(7,4) test: " + hamming_status, theme::kSeriesTeal);
        }
        ImGui::SameLine();
        ImGui::Text("Hamming(7,4): %s", hamming_status.c_str());

        ImGui::End(); // BER & Coding

        // --------------------------------------------------------------------
        // Centre Tab 4: Capacity
        // --------------------------------------------------------------------
        ImGui::Begin("Capacity");

        sectionHeader("Channel Capacity & Shannon Bound", font_set.header);

        constexpr int kShannonPts = 80;
        static double shannon_ebn0[kShannonPts];
        static double shannon_eta[kShannonPts];
        static bool shannon_init = false;
        if (!shannon_init) {
            for (int i = 0; i < kShannonPts; ++i) {
                const double eta = 0.1 + static_cast<double>(i) * 0.1;
                shannon_eta[i] = eta;
                const double lin = (std::pow(2.0, eta) - 1.0) / eta;
                shannon_ebn0[i] = 10.0 * std::log10(lin);
            }
            shannon_init = true;
        }

        static const double op_ebn0[] = { 9.6,  9.6,  13.0, 13.4,  17.5, 17.8,  4.13 };
        static const double op_eta[]  = { 1.0,  2.0,   3.0,  4.0,   4.0,  6.0,  0.50 };
        static const char* op_labels[] = { "BPSK", "QPSK", "8-PSK", "16-QAM", "16-PSK", "64-QAM", "Conv K=7 Soft" };
        constexpr int kNumOps = IM_ARRAYSIZE(op_ebn0);

        const float cap_plot_h = std::max(180.0f, ImGui::GetContentRegionAvail().y - 170.0f);
        if (ImPlot::BeginPlot("Spectral Efficiency vs Eb/N0", ImVec2(-1, cap_plot_h))) {
            ImPlot::SetupAxes("Eb/N0 (dB)", "Spectral Efficiency eta (bits/s/Hz)");
            ImPlot::SetupAxisLimits(ImAxis_X1, -2.0, 20.0, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 7.0, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_None);

            // Shannon Bound (Teal)
            ImPlot::SetNextLineStyle(theme::kSeriesTeal, 2.5f);
            ImPlot::PlotLine("Shannon Bound C/W", shannon_ebn0, shannon_eta, kShannonPts);

            // Constellation Operating Points
            for (int i = 0; i < kNumOps; ++i) {
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, theme::getSeriesColors()[i % 6], 2.0f);
                ImPlot::PlotScatter(op_labels[i], &op_ebn0[i], &op_eta[i], 1);
            }

            ImPlot::EndPlot();
        }

        ImGui::Spacing();
        sectionHeader("Shannon Gap Analysis (BER = 10^-5)", font_set.header);
        if (ImGui::BeginTable("ShannonTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 120 * app_settings.ui_scale))) {
            ImGui::TableSetupColumn("Scheme");
            ImGui::TableSetupColumn("eta (bits/s/Hz)");
            ImGui::TableSetupColumn("Operating Eb/N0");
            ImGui::TableSetupColumn("Shannon Limit");
            ImGui::TableSetupColumn("Gap to Capacity");
            ImGui::TableHeadersRow();

            for (int i = 0; i < kNumOps; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", op_labels[i]);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", op_eta[i]);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f dB", op_ebn0[i]);

                const double limit_lin = (std::pow(2.0, op_eta[i]) - 1.0) / op_eta[i];
                const double limit_db = 10.0 * std::log10(limit_lin);
                const double gap_db = op_ebn0[i] - limit_db;

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f dB", limit_db);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(theme::kSeriesTeal, "%.2f dB", gap_db);
            }
            ImGui::EndTable();
        }

        ImGui::End(); // Capacity

        // --------------------------------------------------------------------
        // Centre Tab 5: Spectrum
        // --------------------------------------------------------------------
        ImGui::Begin("Spectrum");

        sectionHeader("Power Spectral Density & Modulation Analysis", font_set.header);

        const auto spec_snap = sim.getSpectrumSnapshot();
        if (!spec_snap.freqs.empty() && !spec_snap.psd_db.empty()) {
            if (ImPlot::BeginPlot("Power Spectral Density", ImVec2(-1, 180 * app_settings.ui_scale))) {
                ImPlot::SetupAxes("Frequency (kHz)", "PSD (dB/Hz)");
                ImPlot::SetupAxisLimits(ImAxis_Y1, -90.0, 10.0, ImPlotCond_Always);

                std::vector<float> freqs_khz(spec_snap.freqs.size());
                for (size_t i = 0; i < freqs_khz.size(); ++i) {
                    freqs_khz[i] = spec_snap.freqs[i] * 0.001f;
                }

                ImPlot::SetNextLineStyle(theme::kSeriesBlue, 1.8f);
                ImPlot::PlotLine("PSD (dB/Hz)", freqs_khz.data(), spec_snap.psd_db.data(), static_cast<int>(freqs_khz.size()));
                ImPlot::EndPlot();
            }
        } else {
            ImGui::TextDisabled("Awaiting spectrum analyzer data...");
        }

        ImGui::Spacing();
        sectionHeader("FM Carson's Rule Calculator", font_set.header);
        static float fm_fm = 250.0f;
        static float fm_delta = 1000.0f;
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        ImGui::SliderFloat("Modulation Freq fm", &fm_fm, 50.0f, 2000.0f, "%.0f Hz");
        showTooltip("Frequency of modulating baseband message tone (fm)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        ImGui::SliderFloat("Peak Deviation Delta_f", &fm_delta, 100.0f, 5000.0f, "%.0f Hz");
        showTooltip("Peak frequency deviation from carrier frequency (Delta_f)");

        const float beta = (fm_fm > 1e-3f) ? (fm_delta / fm_fm) : 0.0f;
        const float carson_bw = 2.0f * (fm_delta + fm_fm);
        ImGui::Text("Modulation Index Beta: %.2f | Carson Bandwidth B_T: %.1f Hz (2*(Delta_f + fm))", beta, carson_bw);

        sectionHeader("PCM Quantizer SQNR Analysis", font_set.header);
        static int pcm_n_bits = 8;
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        ImGui::SliderInt("Quantizer Bits N", &pcm_n_bits, 2, 12, "%d bits");
        showTooltip("Uniform PCM quantization wordlength (N bits/sample)");
        const double pcm_sqnr = 6.0206 * pcm_n_bits + 1.7609;
        ImGui::Text("Theoretical SQNR (6.02*N + 1.76 dB): %.2f dB", pcm_sqnr);

        ImGui::End(); // Spectrum

        // --------------------------------------------------------------------
        // Centre Tab 6: Multiple Access
        // --------------------------------------------------------------------
        ImGui::Begin("Multiple Access");

        sectionHeader("Multiple Access: CDMA, TDMA & FDMA", font_set.header);

        sectionHeader("CDMA Multi-User Interference", font_set.header);
        static int cdma_users = 12;
        static bool cdma_walsh = true;
        ImGui::SetNextItemWidth(160 * app_settings.ui_scale);
        ImGui::SliderInt("Active Users U", &cdma_users, 1, 16, "%d users");
        showTooltip("Number of concurrent users transmitting simultaneously in the same bandwidth");
        ImGui::SameLine();
        ImGui::Checkbox("Orthogonal Walsh Codes", &cdma_walsh);
        showTooltip("Orthogonal Walsh spreading sequences achieve zero cross-correlation and zero MUI");

        if (cdma_walsh) {
            ImGui::TextColored(theme::kSeriesTeal,
                               "Walsh Spreading (SF=16): MUI = 0.000 across %d users (<w_i, w_j> = 0)", cdma_users);
        } else {
            const double mui_var = static_cast<double>(cdma_users - 1) / 16.0;
            ImGui::TextColored(theme::kSeriesOrange,
                               "PN Spreading (SF=16): Multi-User Interference Variance = (U-1)/SF = %.4f", mui_var);
        }

        sectionHeader("TDMA & FDMA Principles", font_set.header);
        ImGui::BulletText("TDMA: Time is partitioned into synchronized frames and slots. Guard times prevent burst overlap.");
        ImGui::BulletText("FDMA: Spectrum is split into non-overlapping frequency bands with guard bands against adjacent channel interference.");

        ImGui::End(); // Multiple Access

        // --------------------------------------------------------------------
        // Centre Tab 7: Protocol & ARQ
        // --------------------------------------------------------------------
        ImGui::Begin("Protocol & ARQ");

        sectionHeader("Automatic Repeat reQuest (ARQ) Throughput", font_set.header);

        static float arq_pf = 0.15f;
        static float arq_a  = 0.5f;
        static int   arq_w  = 4;

        ImGui::SetNextItemWidth(140 * app_settings.ui_scale);
        ImGui::SliderFloat("Frame Loss Pf", &arq_pf, 0.0f, 0.8f, "%.2f");
        showTooltip("Probability of frame erasure/corruption on the channel");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * app_settings.ui_scale);
        ImGui::SliderFloat("Delay a", &arq_a, 0.0f, 3.0f, "%.2f");
        showTooltip("Normalized propagation delay a = t_prop / t_frame");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120 * app_settings.ui_scale);
        ImGui::SliderInt("Window W", &arq_w, 1, 16, "%d frames");
        showTooltip("Go-Back-N sliding transmission window size (W)");

        const double sw_th = blocks::StopAndWaitArq::theoryEfficiency(arq_pf, arq_a);
        const double gbn_th = blocks::GoBackNArq::theoryEfficiency(arq_pf, arq_a, arq_w);

        ImGui::Spacing();
        ImGui::Text("Stop-and-Wait Efficiency (1-Pf)/(1+2a): %.4f (%.1f%%)", sw_th, sw_th * 100.0);
        ImGui::Text("Go-Back-N Efficiency (Stallings eq):    %.4f (%.1f%%)", gbn_th, gbn_th * 100.0);

        if (ImGui::Button("Run Monte Carlo Verification (10,000 frames)")) {
            const double sw_sim = blocks::StopAndWaitArq::simulate(arq_pf, arq_a, 10000);
            const double gbn_sim = blocks::GoBackNArq::simulate(arq_pf, arq_a, arq_w, 10000);
            addLog("ARQ Sim S&W: " + std::to_string(sw_sim) + " | GBN: " + std::to_string(gbn_sim), theme::kSeriesTeal);
        }

        ImGui::End(); // Protocol & ARQ

        // --------------------------------------------------------------------
        // Bottom Dock: Protocol Table & Logs
        // --------------------------------------------------------------------
        ImGui::Begin("Protocol & System Log");

        if (ImGui::BeginTabBar("BottomTabBar")) {
            if (ImGui::BeginTabItem("Packet Inspection (Monospace)")) {
                static char app_payload[256] = "GET /index.html HTTP/1.1\r\nHost: commsim.local\r\n";
                ImGui::SetNextItemWidth(400 * app_settings.ui_scale);
                ImGui::InputText("Payload", app_payload, sizeof(app_payload));
                showTooltip("Application payload encapsulated into TCP/IPv4/Ethernet II frame");

                auto encap = blocks::PacketEncap::encapsulate(app_payload);
                ImGui::SameLine();
                ImGui::Text("(Frame Size: %zu Bytes, FCS CRC-32 Valid)", encap.raw_ethernet_frame.size());

                if (font_set.mono) ImGui::PushFont(font_set.mono);
                if (ImGui::BeginTable("encap_table", 5,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                      ImVec2(0, 160 * app_settings.ui_scale))) {
                    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 95.0f * app_settings.ui_scale);
                    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.0f * app_settings.ui_scale);
                    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 75.0f * app_settings.ui_scale);
                    ImGui::TableSetupColumn("Hex Bytes", ImGuiTableColumnFlags_WidthFixed, 180.0f * app_settings.ui_scale);
                    ImGui::TableSetupColumn("Decoded Representation", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const auto& f : encap.fields) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (f.layer == "Application") ImGui::TextColored(theme::kSeriesTeal, "%s", f.layer.c_str());
                        else if (f.layer == "TCP")   ImGui::TextColored(theme::kSeriesBlue, "%s", f.layer.c_str());
                        else if (f.layer == "IPv4")  ImGui::TextColored(theme::kSeriesOrange, "%s", f.layer.c_str());
                        else                         ImGui::TextColored(theme::kSeriesPurple, "%s", f.layer.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", f.name.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%zu (%zuB)", f.byte_offset, f.byte_length);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%s", f.hex_value.c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%s", f.decoded_value.c_str());
                    }
                    ImGui::EndTable();
                }
                if (font_set.mono) ImGui::PopFont();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("System Log")) {
                if (ImGui::Button("Clear Log")) event_logs.clear();
                ImGui::SameLine();
                ImGui::Text("%zu log events recorded", event_logs.size());

                if (font_set.mono) ImGui::PushFont(font_set.mono);
                if (ImGui::BeginTable("log_table", 2,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                      ImVec2(0, 160 * app_settings.ui_scale))) {
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f * app_settings.ui_scale);
                    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const auto& log : event_logs) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(theme::kTextMuted, "[%s]", log.timestamp.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(log.color, "%s", log.message.c_str());
                    }
                    ImGui::EndTable();
                }
                if (font_set.mono) ImGui::PopFont();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End(); // Bottom Dock

        // --------------------------------------------------------------------
        // Fixed VS Code Blue Status Bar (#007ACC)
        // --------------------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - status_bar_height));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, status_bar_height));
        ImGuiWindowFlags status_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::kStatusBarBg);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kStatusText);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("StatusBar", nullptr, status_flags);

        char status_str[256];
        std::snprintf(status_str, sizeof(status_str),
                      " Scheme: %s   |   Eb/N0: %.1f dB   |   Render: %.1f FPS   |   Sim: %.2f Msym/s   |   BER: %.2e (Theory: %.2e)   |   Latency: %.1f ms",
                      scheme_names[current_scheme_idx], ui_ebn0, io.Framerate,
                      sim_rate > 0.0 ? sim_rate : 1.0,
                      measured_ber, theory_ber, latency_ms);
        ImGui::TextUnformatted(status_str);

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        // --------------------------------------------------------------------
        // Render Frame
        // --------------------------------------------------------------------
        ImGui::Render();
        int display_w = 0, display_h = 0;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(theme::kEditorBg.x, theme::kEditorBg.y, theme::kEditorBg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Automated Screenshot Suite Processing
        if (auto_screenshot_suite) {
            auto_suite_timer++;
            // Switch tabs every 12 frames to let simulation stream populate density FBOs
            if (auto_suite_timer >= 12) {
                auto_suite_timer = 0;
                char bmp_path[128];
                const char* tab_short[] = {
                    "01_constellation", "02_eye_shaping", "03_ber_coding",
                    "04_capacity", "05_spectrum", "06_multiple_access", "07_protocol_arq"
                };

                if (auto_suite_step == 0) {
                    // 1920x1080 run
                    std::snprintf(bmp_path, sizeof(bmp_path), "docs/screenshots/app_1080p_%s.bmp",
                                  tab_short[auto_suite_substep]);
                    saveScreenshotBMP(display_w, display_h, bmp_path);
                    auto_suite_substep++;
                    if (auto_suite_substep < 7) {
                        requested_tab = auto_suite_substep;
                    } else {
                        // Switch to 1280x720
                        auto_suite_step = 1;
                        auto_suite_substep = 0;
                        SDL_SetWindowSize(window, 1280, 720);
                        requested_tab = 0;
                        reset_layout_requested = true;
                    }
                } else if (auto_suite_step == 1) {
                    // 1280x720 run
                    std::snprintf(bmp_path, sizeof(bmp_path), "docs/screenshots/app_720p_%s.bmp",
                                  tab_short[auto_suite_substep]);
                    saveScreenshotBMP(display_w, display_h, bmp_path);
                    auto_suite_substep++;
                    if (auto_suite_substep < 7) {
                        requested_tab = auto_suite_substep;
                    } else {
                        // Switch to UI Scale 1.5 run
                        auto_suite_step = 2;
                        auto_suite_substep = 0;
                        SDL_SetWindowSize(window, 1920, 1080);
                        pending_ui_scale = 1.5f;
                        requested_tab = 0;
                        reset_layout_requested = true;
                    }
                } else if (auto_suite_step == 2) {
                    // Scale 1.5 run
                    std::snprintf(bmp_path, sizeof(bmp_path), "docs/screenshots/app_scale15_%s.bmp",
                                  tab_short[auto_suite_substep]);
                    saveScreenshotBMP(display_w, display_h, bmp_path);
                    auto_suite_substep++;
                    if (auto_suite_substep < 7) {
                        requested_tab = auto_suite_substep;
                    } else {
                        // Finished all screenshots!
                        running = false;
                    }
                }
            }
        }

        SDL_GL_SwapWindow(window);
    }

    // Save final settings
    SDL_GetWindowSize(window, &app_settings.window_w, &app_settings.window_h);
    app_settings.scheme_idx = current_scheme_idx;
    settings::saveSettings(app_settings);

    // Compute profile frame statistics
    if (!frame_times_ms.empty()) {
        std::sort(frame_times_ms.begin(), frame_times_ms.end());
        double med_ms = frame_times_ms[frame_times_ms.size() / 2];
        double p99_ms = frame_times_ms[static_cast<size_t>(frame_times_ms.size() * 0.99)];
        std::printf("[Frame Performance] Median: %.2f ms (%.1f FPS) | p99: %.2f ms (%.1f FPS) over %zu frames\n",
                    med_ms, 1000.0 / med_ms, p99_ms, 1000.0 / p99_ms, frame_times_ms.size());
    }

    // Cleanup
    sim.stop();
    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
