#include "key_detector.h"
#include <algorithm>
#include <numeric>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Key Profile Templates
// ============================================================================

// Krumhansl-Kessler profiles (cognitive probe tone study)
static const double KK_MAJOR[12] = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
};
static const double KK_MINOR[12] = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
};

// Temperley profiles (music corpus derived, often more accurate on pop/rock)
static const double TEMPERLEY_MAJOR[12] = {
    5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0
};
static const double TEMPERLEY_MINOR[12] = {
    5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0
};

// Aarden-Essen profiles (statistical distribution from large corpus)
static const double AARDEN_MAJOR[12] = {
    17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587,
    0.291248, 22.062, 0.145624, 8.15494, 0.232998, 4.95122
};
static const double AARDEN_MINOR[12] = {
    18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362,
    0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623
};

// ============================================================================
// Helper: Cosine similarity between two 12-element vectors
// ============================================================================
static double cosine_similarity(const double* a, const double* b) {
    double dot = 0, norm_a = 0, norm_b = 0;
    for (int i = 0; i < 12; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    if (norm_a < 1e-12 || norm_b < 1e-12) return 0;
    return dot / (sqrt(norm_a) * sqrt(norm_b));
}

// Rotate a profile by 'shift' positions (for transposing key templates)
static void rotate_profile(const double* src, double* dst, int shift) {
    for (int i = 0; i < 12; i++) {
        dst[i] = src[((i - shift) % 12 + 12) % 12];
    }
}

// ============================================================================
// KeyDetector Implementation
// ============================================================================

KeyDetector::KeyDetector() {
    // Pre-compute Hanning window
    for (int i = 0; i < FFT_SIZE; i++) {
        m_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));
    }
    
    m_windowed.resize(FFT_SIZE);
    m_fft_out.resize(FFT_SIZE);
    m_magnitude.resize(FFT_SIZE / 2 + 1);
    
    reset();
}

KeyDetector::~KeyDetector() {}

void KeyDetector::reset() {
    memset(m_chroma_sum, 0, sizeof(m_chroma_sum));
    m_frame_count = 0;
    m_analyzed_seconds = 0;
    m_buffer.clear();
    m_buffer_pos = 0;
}

void KeyDetector::feed(const float* mono_samples, size_t count, unsigned sample_rate) {
    // Append to internal buffer
    size_t old_size = m_buffer.size();
    m_buffer.resize(old_size + count);
    memcpy(m_buffer.data() + old_size, mono_samples, count * sizeof(float));
    
    // Process complete frames with overlap
    while (m_buffer_pos + FFT_SIZE <= m_buffer.size()) {
        process_frame(m_buffer.data() + m_buffer_pos, sample_rate);
        m_buffer_pos += HOP_SIZE;
        m_analyzed_seconds += (double)HOP_SIZE / sample_rate;
    }
    
    // Trim consumed data periodically to avoid unbounded growth
    if (m_buffer_pos > FFT_SIZE * 4) {
        size_t remaining = m_buffer.size() - m_buffer_pos;
        if (remaining > 0) {
            memmove(m_buffer.data(), m_buffer.data() + m_buffer_pos, remaining * sizeof(float));
        }
        m_buffer.resize(remaining);
        m_buffer_pos = 0;
    }
}

// ============================================================================
// Radix-2 Cooley-Tukey FFT (in-place, iterative)
// ============================================================================
void KeyDetector::compute_fft(const float* input, std::complex<float>* output, int n) const {
    // Copy input to output as complex
    for (int i = 0; i < n; i++) {
        output[i] = std::complex<float>(input[i], 0.0f);
    }
    
    // Bit-reversal permutation
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) std::swap(output[i], output[j]);
    }
    
    // Butterfly computation
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * (float)M_PI / len;
        std::complex<float> wn(cosf(angle), sinf(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; k++) {
                std::complex<float> u = output[i + k];
                std::complex<float> t = w * output[i + k + len / 2];
                output[i + k] = u + t;
                output[i + k + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

// ============================================================================
// Process one FFT frame -> accumulate HPCP (Harmonic Pitch Class Profile)
// ============================================================================
void KeyDetector::process_frame(const float* frame, unsigned sample_rate) {
    // Apply window
    for (int i = 0; i < FFT_SIZE; i++) {
        m_windowed[i] = frame[i] * m_window[i];
    }
    
    // FFT
    compute_fft(m_windowed.data(), m_fft_out.data(), FFT_SIZE);
    
    // Compute magnitude spectrum
    int num_bins = FFT_SIZE / 2 + 1;
    for (int i = 0; i < num_bins; i++) {
        m_magnitude[i] = std::abs(m_fft_out[i]);
    }
    
    // ---- Spectral Whitening ----
    // Compress dynamics to prevent loud bass from dominating
    // Use log-magnitude with a floor
    for (int i = 0; i < num_bins; i++) {
        if (m_magnitude[i] > 1e-8f) {
            m_magnitude[i] = logf(1.0f + m_magnitude[i]);
        } else {
            m_magnitude[i] = 0;
        }
    }
    
    // ---- HPCP: Map frequency bins to pitch classes ----
    // We consider frequencies from ~60Hz (B1) to ~5000Hz
    // For each bin, assign contribution to the nearest pitch class
    // Weight by harmonic structure: reduce contribution of harmonics
    
    double frame_chroma[12] = {0};
    
    double freq_resolution = (double)sample_rate / FFT_SIZE;
    
    // Reference frequency: A4 = 440 Hz
    // C0 = 440 * 2^(-57/12) ≈ 16.35 Hz
    // pitch_class = round(12 * log2(freq / C0)) % 12
    
    double ref_c0 = 440.0 * pow(2.0, -57.0 / 12.0); // ~16.35 Hz
    
    int min_bin = (int)(60.0 / freq_resolution);    // ~60 Hz
    int max_bin = (int)(5000.0 / freq_resolution);  // ~5000 Hz
    if (max_bin >= num_bins) max_bin = num_bins - 1;
    if (min_bin < 1) min_bin = 1;
    
    for (int bin = min_bin; bin <= max_bin; bin++) {
        double freq = bin * freq_resolution;
        if (freq < 30.0) continue;
        
        // Calculate pitch class (continuous)
        double pitch = 12.0 * log2(freq / ref_c0);
        int pitch_class = ((int)round(pitch)) % 12;
        if (pitch_class < 0) pitch_class += 12;
        
        // Weight: apply 1/sqrt(frequency) to balance low/high frequency contribution
        // Also use magnitude squared for energy-based weighting
        double weight = (double)m_magnitude[bin] * (double)m_magnitude[bin];
        
        // Apply frequency-dependent weighting:
        // Higher octaves get slightly less weight (harmonics tend to be there)
        double octave = pitch / 12.0;
        double octave_weight = 1.0;
        if (octave > 4.0) octave_weight = 0.8;
        if (octave > 5.0) octave_weight = 0.6;
        if (octave > 6.0) octave_weight = 0.4;
        
        frame_chroma[pitch_class] += weight * octave_weight;
    }
    
    // ---- Harmonic Product Spectrum enhancement ----
    // For each pitch class, also check sub-harmonics:
    // If a note is detected, its harmonics (2x, 3x frequency) should also be present
    // Weight the fundamental more than harmonics
    double enhanced_chroma[12] = {0};
    for (int pc = 0; pc < 12; pc++) {
        enhanced_chroma[pc] = frame_chroma[pc];
        // Add weighted contribution from what would be harmonics of this pitch class
        // 2nd harmonic is +12 semitones (same pitch class) -> already included
        // 3rd harmonic is +19 semitones -> (pc + 7) % 12 (a perfect 5th up)
        // Subtract some of the 5th's contribution as it's likely a harmonic
        int fifth_pc = (pc + 7) % 12;
        enhanced_chroma[pc] += frame_chroma[fifth_pc] * 0.2; // Boost if 5th is present (supports this key)
    }
    
    // Normalize frame chroma
    double max_val = 0;
    for (int i = 0; i < 12; i++) {
        if (enhanced_chroma[i] > max_val) max_val = enhanced_chroma[i];
    }
    if (max_val > 0) {
        for (int i = 0; i < 12; i++) {
            enhanced_chroma[i] /= max_val;
        }
    }
    
    // Accumulate
    for (int i = 0; i < 12; i++) {
        m_chroma_sum[i] += enhanced_chroma[i];
    }
    m_frame_count++;
}

// ============================================================================
// Key Detection: Match accumulated chroma against all 24 key profiles
// ============================================================================
key_detection_result_t KeyDetector::detect() const {
    key_detection_result_t result;
    memset(&result, 0, sizeof(result));
    
    if (m_frame_count == 0) {
        result.confidence = 0;
        return result;
    }
    
    // Normalize accumulated chroma
    double chroma[12];
    double total = 0;
    for (int i = 0; i < 12; i++) total += m_chroma_sum[i];
    if (total < 1e-12) {
        result.confidence = 0;
        return result;
    }
    for (int i = 0; i < 12; i++) {
        chroma[i] = m_chroma_sum[i] / total;
        result.chroma[i] = (float)chroma[i];
    }
    
    // Try all 24 keys (12 major + 12 minor) with multiple profiles
    double best_score = -999;
    int best_key = 0;
    bool best_minor = false;
    
    double scores[24]; // 0-11: major, 12-23: minor
    
    for (int key = 0; key < 12; key++) {
        double rotated_kk_major[12], rotated_kk_minor[12];
        double rotated_tmp_major[12], rotated_tmp_minor[12];
        double rotated_aar_major[12], rotated_aar_minor[12];
        
        rotate_profile(KK_MAJOR, rotated_kk_major, key);
        rotate_profile(KK_MINOR, rotated_kk_minor, key);
        rotate_profile(TEMPERLEY_MAJOR, rotated_tmp_major, key);
        rotate_profile(TEMPERLEY_MINOR, rotated_tmp_minor, key);
        rotate_profile(AARDEN_MAJOR, rotated_aar_major, key);
        rotate_profile(AARDEN_MINOR, rotated_aar_minor, key);
        
        // Weighted combination of all three profiles
        double score_major = cosine_similarity(chroma, rotated_kk_major) * 0.35
                           + cosine_similarity(chroma, rotated_tmp_major) * 0.35
                           + cosine_similarity(chroma, rotated_aar_major) * 0.30;
        
        double score_minor = cosine_similarity(chroma, rotated_kk_minor) * 0.35
                           + cosine_similarity(chroma, rotated_tmp_minor) * 0.35
                           + cosine_similarity(chroma, rotated_aar_minor) * 0.30;
        
        scores[key] = score_major;
        scores[key + 12] = score_minor;
        
        if (score_major > best_score) {
            best_score = score_major;
            best_key = key;
            best_minor = false;
        }
        if (score_minor > best_score) {
            best_score = score_minor;
            best_key = key;
            best_minor = true;
        }
    }
    
    // Calculate confidence: ratio of best score to second-best from a different tonal center
    // Exclude relative major/minor (same tonal center)
    double second_best = -999;
    int relative_key = best_minor ? ((best_key + 3) % 12) : ((best_key + 9) % 12);
    
    for (int i = 0; i < 24; i++) {
        int this_key = i % 12;
        if (this_key == best_key || this_key == relative_key) continue;
        if (scores[i] > second_best) second_best = scores[i];
    }
    
    // Confidence based on margin between best and second-best
    double margin = best_score - second_best;
    // Map margin to 0-1 confidence (empirically calibrated)
    // Typical margins: 0.015-0.06 for cosine similarity of chroma profiles
    float conf = (float)(margin / 0.035);  // 0.035 margin -> 100% confidence
    if (conf > 1.0f) conf = 1.0f;
    if (conf < 0.0f) conf = 0.0f;
    
    // Boost confidence if we have enough data
    if (m_analyzed_seconds < 5.0) {
        conf *= (float)(m_analyzed_seconds / 5.0);
    }
    
    result.key_index = best_key;
    result.is_minor = best_minor;
    result.confidence = conf;
    
    return result;
}

// ============================================================================
// key_detection_result_t helpers
// ============================================================================

const char* key_detection_result_t::key_name_ascii() const {
    if (key_index < 0 || key_index > 11) return "?";
    return g_key_names_ascii[key_index];
}

const char* key_detection_result_t::key_name_utf8() const {
    if (key_index < 0 || key_index > 11) return "?";
    return g_key_names_utf8[key_index];
}

const char* key_detection_result_t::quality_utf8() const {
    return is_minor ? u8"小调" : u8"大调";
}

int key_detection_result_t::semitones_to(int target_key, bool target_minor) const {
    (void)target_minor; // Mode doesn't affect semitone calculation
    int diff = (target_key - key_index + 12) % 12;
    if (diff > 6) diff -= 12;  // Take shortest path
    return diff;
}
