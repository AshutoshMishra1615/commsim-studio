#pragma once
/**
 * render/density_renderer.hpp  --  M3: GPU Density Renderer.
 *
 * Implements phosphor persistence and point-sprite density accumulation:
 *   - Float accumulation target with exponential decay
 *   - Point-sprite additive accumulation
 *   - Perceptual colormaps (Turbo, Phosphor, Inferno, Viridis)
 *   - Dual-path: GPU OpenGL FBO pipeline with fast fallback for test verification
 */
#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace render {

enum class Colormap {
    Turbo,
    Phosphor,
    Inferno,
    Viridis
};

struct ColormapRGBA {
    uint8_t r, g, b, a;
};

class DensityRenderer {
public:
    explicit DensityRenderer(int width = 512, int height = 512);
    ~DensityRenderer();

    // Prevent copies, allow moves
    DensityRenderer(const DensityRenderer&) = delete;
    DensityRenderer& operator=(const DensityRenderer&) = delete;
    DensityRenderer(DensityRenderer&&) noexcept;
    DensityRenderer& operator=(DensityRenderer&&) noexcept;

    void resize(int width, int height);

    void setDecay(float decay) { decay_ = decay; }
    float getDecay() const { return decay_; }

    void setColormap(Colormap cm) { colormap_ = cm; }
    Colormap getColormap() const { return colormap_; }

    void setScale(float scale) { scale_ = scale; }
    float getScale() const { return scale_; }

    /// Clear all accumulated density
    void clear();

    /// Perform frame decay
    void decay();

    /// Accumulate complex symbols (I/Q) for constellation display
    void accumulateConstellation(const std::complex<float>* points, size_t count);

    /// Accumulate time/amplitude points for eye diagram display
    void accumulateEye(const float* times, const float* amps, size_t count);

    /// Finalize rendering and return OpenGL texture ID for ImGui::Image
    uint32_t render();

    // ---- Inspection & Testing APIs ----
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    float sampleDensity(float x, float y) const;
    const float* getDensityData() const { return density_buffer_.data(); }

    /// Measure peak-to-valley contrast ratio between cluster centers and valleys
    float measureClusterContrast(const std::vector<std::pair<float, float>>& peaks,
                                 const std::vector<std::pair<float, float>>& valleys) const;

private:
    void initGL();
    void updateColormapTexture();
    void sampleToPixel(float x, float y, int& px, int& py) const;

    int width_  = 512;
    int height_ = 512;
    float decay_ = 0.92f;
    float scale_ = 1.8f;
    Colormap colormap_ = Colormap::Turbo;

    // CPU density buffer (used for testing and software texture update)
    std::vector<float> density_buffer_;
    std::vector<uint32_t> rgba_buffer_;

    // OpenGL handles
    bool gl_initialized_ = false;
    uint32_t output_texture_ = 0;
};

}  // namespace render
