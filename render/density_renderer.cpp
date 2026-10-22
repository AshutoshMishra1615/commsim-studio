#include "density_renderer.hpp"
#include "gl_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace render {

// ---------------------------------------------------------------------------
// Colormap Lookups
// ---------------------------------------------------------------------------
static ColormapRGBA evaluateColormap(Colormap type, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    ColormapRGBA c{0, 0, 0, 255};

    switch (type) {
        case Colormap::Turbo: {
            // Smooth Turbo colormap approximation
            float r = 0.1357f + t * (4.5974f + t * (-42.345f + t * (130.58f + t * (-150.56f + t * 58.137f))));
            float g = 0.0914f + t * (2.1856f + t * (4.8052f + t * (-14.019f + t * (4.2124f + t * 2.7747f))));
            float b = 0.1067f + t * (12.583f + t * (-78.026f + t * (177.25f + t * (-161.76f + t * 49.932f))));
            c.r = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, r)) * 255.0f);
            c.g = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, g)) * 255.0f);
            c.b = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, b)) * 255.0f);
            break;
        }
        case Colormap::Phosphor: {
            // Classic analog oscilloscope phosphor green
            float g = std::sqrt(t);
            float r = t * t * t * 0.3f;
            float b = t * t * 0.2f;
            c.r = static_cast<uint8_t>(r * 255.0f);
            c.g = static_cast<uint8_t>(g * 255.0f);
            c.b = static_cast<uint8_t>(b * 255.0f);
            break;
        }
        case Colormap::Inferno: {
            // Black -> Purple -> Orange -> Yellow -> White
            float r = t < 0.5f ? t * 2.0f * 0.7f : 0.7f + (t - 0.5f) * 2.0f * 0.3f;
            float g = t < 0.6f ? 0.0f : (t - 0.6f) * 2.5f;
            float b = t < 0.4f ? t * 2.5f : (t < 0.8f ? 1.0f - (t - 0.4f) * 2.5f : (t - 0.8f) * 5.0f);
            c.r = static_cast<uint8_t>(std::min(1.0f, r) * 255.0f);
            c.g = static_cast<uint8_t>(std::min(1.0f, g) * 255.0f);
            c.b = static_cast<uint8_t>(std::min(1.0f, b) * 255.0f);
            break;
        }
        case Colormap::Viridis: {
            float r = t < 0.5f ? 0.27f + t * 0.5f : 0.52f + (t - 0.5f) * 0.9f;
            float g = t * 0.9f;
            float b = t < 0.5f ? 0.33f + t * 0.7f : 0.68f - (t - 0.5f) * 0.7f;
            c.r = static_cast<uint8_t>(std::min(1.0f, r) * 255.0f);
            c.g = static_cast<uint8_t>(std::min(1.0f, g) * 255.0f);
            c.b = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, b)) * 255.0f);
            break;
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// Construction & Lifecycle
// ---------------------------------------------------------------------------
DensityRenderer::DensityRenderer(int width, int height)
    : width_(width), height_(height) {
    density_buffer_.resize(static_cast<size_t>(width_ * height_), 0.0f);
    rgba_buffer_.resize(static_cast<size_t>(width_ * height_), 0xFF000000);
}

DensityRenderer::~DensityRenderer() {
    if (output_texture_ != 0) {
        glDeleteTextures(1, &output_texture_);
        output_texture_ = 0;
    }
}

DensityRenderer::DensityRenderer(DensityRenderer&& o) noexcept
    : width_(o.width_), height_(o.height_), decay_(o.decay_), scale_(o.scale_),
      colormap_(o.colormap_), density_buffer_(std::move(o.density_buffer_)),
      rgba_buffer_(std::move(o.rgba_buffer_)), gl_initialized_(o.gl_initialized_),
      output_texture_(o.output_texture_) {
    o.output_texture_ = 0;
}

DensityRenderer& DensityRenderer::operator=(DensityRenderer&& o) noexcept {
    if (this != &o) {
        if (output_texture_ != 0) glDeleteTextures(1, &output_texture_);
        width_ = o.width_;
        height_ = o.height_;
        decay_ = o.decay_;
        scale_ = o.scale_;
        colormap_ = o.colormap_;
        density_buffer_ = std::move(o.density_buffer_);
        rgba_buffer_ = std::move(o.rgba_buffer_);
        gl_initialized_ = o.gl_initialized_;
        output_texture_ = o.output_texture_;
        o.output_texture_ = 0;
    }
    return *this;
}

void DensityRenderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_  = width;
    height_ = height;
    density_buffer_.assign(static_cast<size_t>(width_ * height_), 0.0f);
    rgba_buffer_.assign(static_cast<size_t>(width_ * height_), 0xFF000000);
    if (output_texture_ != 0) {
        glDeleteTextures(1, &output_texture_);
        output_texture_ = 0;
        gl_initialized_ = false;
    }
}

void DensityRenderer::clear() {
    std::fill(density_buffer_.begin(), density_buffer_.end(), 0.0f);
}

void DensityRenderer::decay() {
    const float d = decay_;
    for (float& val : density_buffer_) {
        val *= d;
    }
}

void DensityRenderer::sampleToPixel(float x, float y, int& px, int& py) const {
    const float norm_x = (x / scale_ + 1.0f) * 0.5f;
    const float norm_y = (-y / scale_ + 1.0f) * 0.5f;
    px = static_cast<int>(norm_x * static_cast<float>(width_ - 1) + 0.5f);
    py = static_cast<int>(norm_y * static_cast<float>(height_ - 1) + 0.5f);
}

void DensityRenderer::accumulateConstellation(const std::complex<float>* points, size_t count) {
    if (!points || count == 0) return;

    const int w = width_;
    const int h = height_;
    const float inv_scale = 1.0f / scale_;

    for (size_t i = 0; i < count; ++i) {
        const float x = points[i].real();
        const float y = points[i].imag();

        const float norm_x = (x * inv_scale + 1.0f) * 0.5f;
        const float norm_y = (-y * inv_scale + 1.0f) * 0.5f;

        const int px = static_cast<int>(norm_x * static_cast<float>(w - 1) + 0.5f);
        const int py = static_cast<int>(norm_y * static_cast<float>(h - 1) + 0.5f);

        if (px >= 1 && px < w - 1 && py >= 1 && py < h - 1) {
            density_buffer_[static_cast<size_t>(py * w + px)]         += 1.0f;
            density_buffer_[static_cast<size_t>(py * w + (px - 1))]   += 0.5f;
            density_buffer_[static_cast<size_t>(py * w + (px + 1))]   += 0.5f;
            density_buffer_[static_cast<size_t>((py - 1) * w + px)]   += 0.5f;
            density_buffer_[static_cast<size_t>((py + 1) * w + px)]   += 0.5f;
            density_buffer_[static_cast<size_t>((py - 1) * w + px - 1)] += 0.25f;
            density_buffer_[static_cast<size_t>((py - 1) * w + px + 1)] += 0.25f;
            density_buffer_[static_cast<size_t>((py + 1) * w + px - 1)] += 0.25f;
            density_buffer_[static_cast<size_t>((py + 1) * w + px + 1)] += 0.25f;
        } else if (px >= 0 && px < w && py >= 0 && py < h) {
            density_buffer_[static_cast<size_t>(py * w + px)] += 1.0f;
        }
    }
}

void DensityRenderer::accumulateEye(const float* times, const float* amps, size_t count) {
    if (!times || !amps || count == 0) return;

    const int w = width_;
    const int h = height_;
    const float inv_scale = 1.0f / scale_;

    for (size_t i = 0; i < count; ++i) {
        // Map time in [0, 2] to [0, 1]
        const float norm_x = std::fmod(times[i], 2.0f) * 0.5f;
        const float norm_y = (-amps[i] * inv_scale + 1.0f) * 0.5f;

        const int px = static_cast<int>(norm_x * static_cast<float>(w - 1) + 0.5f);
        const int py = static_cast<int>(norm_y * static_cast<float>(h - 1) + 0.5f);

        if (px >= 1 && px < w - 1 && py >= 1 && py < h - 1) {
            density_buffer_[static_cast<size_t>(py * w + px)]       += 1.0f;
            density_buffer_[static_cast<size_t>((py - 1) * w + px)] += 0.5f;
            density_buffer_[static_cast<size_t>((py + 1) * w + px)] += 0.5f;
        } else if (px >= 0 && px < w && py >= 0 && py < h) {
            density_buffer_[static_cast<size_t>(py * w + px)] += 1.0f;
        }
    }
}

void DensityRenderer::initGL() {
    if (gl_initialized_) return;

    glGenTextures(1, &output_texture_);
    glBindTexture(GL_TEXTURE_2D, output_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_initialized_ = true;
}

uint32_t DensityRenderer::render() {
    // Find maximum density for normalization
    float max_d = 1e-4f;
    for (float val : density_buffer_) {
        if (val > max_d) max_d = val;
    }

    const float inv_max_d = 1.0f / max_d;
    const size_t total_pixels = static_cast<size_t>(width_ * height_);

    for (size_t i = 0; i < total_pixels; ++i) {
        const float norm = std::sqrt(density_buffer_[i] * inv_max_d); // Gamma 0.5 for high dynamic range
        const ColormapRGBA col = evaluateColormap(colormap_, norm);
        rgba_buffer_[i] = (static_cast<uint32_t>(col.a) << 24) |
                          (static_cast<uint32_t>(col.b) << 16) |
                          (static_cast<uint32_t>(col.g) << 8)  |
                          static_cast<uint32_t>(col.r);
    }

    initGL();
    if (output_texture_ != 0) {
        glBindTexture(GL_TEXTURE_2D, output_texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_buffer_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return output_texture_;
}

float DensityRenderer::sampleDensity(float x, float y) const {
    int px = 0, py = 0;
    sampleToPixel(x, y, px, py);
    if (px < 0 || px >= width_ || py < 0 || py >= height_) return 0.0f;

    // Average 3x3 neighbourhood around (px, py)
    float sum = 0.0f;
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = px + dx;
            int cy = py + dy;
            if (cx >= 0 && cx < width_ && cy >= 0 && cy < height_) {
                sum += density_buffer_[static_cast<size_t>(cy * width_ + cx)];
                ++count;
            }
        }
    }
    return count > 0 ? (sum / static_cast<float>(count)) : 0.0f;
}

float DensityRenderer::measureClusterContrast(
    const std::vector<std::pair<float, float>>& peaks,
    const std::vector<std::pair<float, float>>& valleys) const {
    if (peaks.empty() || valleys.empty()) return 1.0f;

    float peak_sum = 0.0f;
    for (const auto& [x, y] : peaks) {
        peak_sum += sampleDensity(x, y);
    }
    const float avg_peak = peak_sum / static_cast<float>(peaks.size());

    float valley_sum = 0.0f;
    for (const auto& [x, y] : valleys) {
        valley_sum += sampleDensity(x, y);
    }
    const float avg_valley = valley_sum / static_cast<float>(valleys.size());

    if (avg_valley <= 1e-6f) {
        return avg_peak > 0.0f ? 1000.0f : 1.0f;
    }
    return avg_peak / avg_valley;
}

}  // namespace render
