/**
 * app/main.cpp -- M2 Viewer v1
 *
 * Implements M2 Viewer with SDL2, OpenGL 3.3 Core, Dear ImGui, and ImPlot.
 * Features:
 *   - Background simulation thread running the signal graph (SimController)
 *   - Wait-free TripleBuffer transferring real-time waveforms
 *   - Real-time controls: modulation scheme, Eb/N0 slider, run/pause, reset stats
 *   - ImPlot time-domain Waveform panel (I & Q)
 *   - ImPlot BER curve readout with theoretical curves and operating point marker
 *   - Performance telemetry (Msym/s, FPS, slider change latency)
 */
#include <SDL.h>
#include <imgui.h>
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/sim_controller.hpp"

// ============================================================
// Constants & Helpers
// ============================================================
static constexpr int   WIN_W   = 1400;
static constexpr int   WIN_H   = 850;
static constexpr float CLEAR_R = 0.08f;
static constexpr float CLEAR_G = 0.09f;
static constexpr float CLEAR_B = 0.12f;

static double qFunction(double x) {
    return 0.5 * std::erfc(x / std::sqrt(2.0));
}

static double theoreticalBer(engine::Scheme scheme, double ebn0_db) {
    const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
    switch (scheme) {
        case engine::Scheme::BPSK:
        case engine::Scheme::QPSK:
            return qFunction(std::sqrt(2.0 * ebn0_lin));
        case engine::Scheme::QAM16:
            return (3.0 / 8.0) * std::erfc(std::sqrt(0.4 * ebn0_lin));
        case engine::Scheme::BFSK:
        case engine::Scheme::OOK:
            return qFunction(std::sqrt(ebn0_lin));
    }
    return 0.0;
}

// ============================================================
int main(int /*argc*/, char** /*argv*/)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Request OpenGL 3.3 core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "commsim Studio -- Communications System Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
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
    SDL_GL_SetSwapInterval(1);  // vsync for smooth 60 FPS

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Style adjustments for sleek studio aesthetic
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Start simulation engine (SimController on background thread)
    engine::SimController sim(4096, 42);
    sim.setTargetRate(1.0); // 1.0 Msym/s target
    sim.start();

    // Local UI state
    float ui_ebn0 = 6.0f;
    int current_scheme_idx = 0;
    const char* scheme_names[] = { "BPSK", "QPSK", "16-QAM", "BFSK", "OOK" };
    const engine::Scheme scheme_enums[] = {
        engine::Scheme::BPSK, engine::Scheme::QPSK, engine::Scheme::QAM16,
        engine::Scheme::BFSK, engine::Scheme::OOK
    };

    // Precalculate theory curves (0 to 14 dB)
    constexpr int kTheoryPoints = 71;
    std::vector<double> curve_snr(kTheoryPoints);
    std::vector<double> curve_ber(kTheoryPoints);
    for (int i = 0; i < kTheoryPoints; ++i) {
        curve_snr[i] = static_cast<double>(i) * 0.2;
    }

    // --------------------------------------------------------
    // Main loop
    // --------------------------------------------------------
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // Start frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Full-viewport dockspace
        {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGuiWindowFlags dockspace_flags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove      |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_MenuBar;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::Begin("DockSpaceHost", nullptr, dockspace_flags);
            ImGui::PopStyleVar(2);
            ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.f, 0.f),
                             ImGuiDockNodeFlags_PassthruCentralNode);

            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Reset Statistics", "Ctrl+R")) sim.resetStats();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Alt+F4")) running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Simulation")) {
                    bool paused = sim.isPaused();
                    if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Space")) {
                        sim.setPause(!paused);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::End();
        }

        // Read real-time engine telemetry
        const double sim_rate = sim.getSimRateMsymS();
        const double latency_ms = sim.getLastLatencyMs();
        const auto ber_snap = sim.getBerSnapshot();
        const double measured_ber = ber_snap.total > 0
            ? static_cast<double>(ber_snap.errors) / static_cast<double>(ber_snap.total)
            : 0.0;
        const double current_ebn0 = sim.getEbN0();
        const double theory_ber = theoreticalBer(scheme_enums[current_scheme_idx], current_ebn0);

        // Update theoretical curve for active scheme
        for (int i = 0; i < kTheoryPoints; ++i) {
            curve_ber[i] = std::max(1e-7, theoreticalBer(scheme_enums[current_scheme_idx], curve_snr[i]));
        }

        // ----------------------------------------------------
        // Panel 1: Simulation Controls & Telemetry
        // ----------------------------------------------------
        ImGui::Begin("Simulation Controls");

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Modulation Scheme");
        if (ImGui::Combo("##Scheme", &current_scheme_idx, scheme_names, IM_ARRAYSIZE(scheme_names))) {
            sim.setScheme(scheme_enums[current_scheme_idx]);
            sim.resetStats();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Channel Parameters");
        ImGui::Text("Eb/N0: %.2f dB", ui_ebn0);
        if (ImGui::SliderFloat("Eb/N0 (dB)", &ui_ebn0, 0.0f, 16.0f, "%.1f dB")) {
            sim.setParam("ebn0_db", static_cast<double>(ui_ebn0));
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "State & Rate");
        bool is_paused = sim.isPaused();
        if (ImGui::Button(is_paused ? " > Resume " : " || Pause ", ImVec2(100, 0))) {
            sim.setPause(!is_paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Stats", ImVec2(100, 0))) {
            sim.resetStats();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Performance & Acceptance");
        ImGui::Text("Render Rate:       %.1f FPS", io.Framerate);
        ImGui::Text("Sim Throughput:    %.2f Msym/s", sim_rate);
        ImGui::Text("Parameter Latency: %.2f ms", latency_ms);
        ImGui::ProgressBar(static_cast<float>(sim_rate / 1.5), ImVec2(-1, 6), "");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Accumulated Metrics");
        ImGui::Text("Processed Bits:   %llu", static_cast<unsigned long long>(ber_snap.total));
        ImGui::Text("Bit Errors:       %llu", static_cast<unsigned long long>(ber_snap.errors));
        ImGui::Text("Measured BER:     %.3e", measured_ber);
        ImGui::Text("Theoretical BER:  %.3e", theory_ber);
        ImGui::End();

        // ----------------------------------------------------
        // Panel 2: Waveform Panel (ImPlot)
        // ----------------------------------------------------
        ImGui::Begin("Time-Domain Waveform");
        auto wf_buf = sim.getWaveformBuffer();
        if (wf_buf) {
            const auto& wf = wf_buf->read_buffer();
            if (!wf.time.empty()) {
                if (ImPlot::BeginPlot("Baseband I/Q Waveform", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("Sample Index", "Amplitude");
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -2.5, 2.5, ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, static_cast<double>(std::min<size_t>(wf.time.size(), 256)), ImPlotCond_Always);

                    const int plot_n = static_cast<int>(std::min<size_t>(wf.time.size(), 256));
                    ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), 1.5f);
                    ImPlot::PlotLine("I (In-Phase)", wf.time.data(), wf.i_data.data(), plot_n);

                    ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), 1.5f);
                    ImPlot::PlotLine("Q (Quadrature)", wf.time.data(), wf.q_data.data(), plot_n);

                    ImPlot::EndPlot();
                }
            } else {
                ImGui::TextDisabled("Awaiting simulation samples...");
            }
        }
        ImGui::End();

        // ----------------------------------------------------
        // Panel 3: BER Curve & Readout (ImPlot)
        // ----------------------------------------------------
        ImGui::Begin("BER Performance");
        if (ImPlot::BeginPlot("Bit Error Rate vs Eb/N0", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Eb/N0 (dB)", "Bit Error Rate (BER)");
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 14.0, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 1e-6, 1.0, ImPlotCond_Always);

            // Theoretical reference curve
            ImPlot::SetNextLineStyle(ImVec4(0.6f, 0.6f, 0.7f, 0.8f), 2.0f);
            ImPlot::PlotLine("Theory", curve_snr.data(), curve_ber.data(), kTheoryPoints);

            // Real-time operating point
            if (measured_ber > 0.0) {
                double pt_x = current_ebn0;
                double pt_y = std::max(1e-6, measured_ber);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, ImVec4(1.0f, 0.2f, 0.2f, 1.0f), 2.0f);
                ImPlot::PlotScatter("Simulation", &pt_x, &pt_y, 1);
            }

            ImPlot::EndPlot();
        }
        ImGui::End();

        // Render
        ImGui::Render();
        int display_w = 0, display_h = 0;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(CLEAR_R, CLEAR_G, CLEAR_B, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
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
