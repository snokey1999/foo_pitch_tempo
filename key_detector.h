#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <complex>

// ============================================================================
// Musical Key Detector
// Uses FFT -> HPCP (Harmonic Pitch Class Profile) -> Key Profile Matching
// Combines Krumhansl-Kessler and Temperley profiles for higher accuracy
// ============================================================================

// Key result
struct key_detection_result_t {
    int key_index;          // 0-11 (C, C#, D, ..., B)
    bool is_minor;          // true = minor, false = major
    float confidence;       // 0.0 - 1.0
    float chroma[12];       // Normalized chroma vector for display
    
    // Convenience
    const char* key_name_ascii() const;
    const char* key_name_utf8() const;
    const char* quality_utf8() const;   // "大调" / "小调"
    int semitones_to(int target_key, bool target_minor) const;
};

class KeyDetector {
public:
    KeyDetector();
    ~KeyDetector();
    
    // Reset state for a new track
    void reset();
    
    // Feed audio data (mono float, any sample rate)
    // Call this repeatedly with chunks of audio
    void feed(const float* mono_samples, size_t count, unsigned sample_rate);
    
    // Get current detection result
    // Can be called at any time after feeding some data
    key_detection_result_t detect() const;
    
    // How many seconds of audio have been analyzed
    double analyzed_seconds() const { return m_analyzed_seconds; }
    
private:
    // FFT
    static const int FFT_SIZE = 8192;
    static const int HOP_SIZE = 4096;   // 50% overlap
    
    void process_frame(const float* frame, unsigned sample_rate);
    void compute_fft(const float* input, std::complex<float>* output, int n) const;
    
    // Chroma accumulation
    double m_chroma_sum[12];
    size_t m_frame_count;
    double m_analyzed_seconds;
    
    // Input buffer for overlap
    std::vector<float> m_buffer;
    size_t m_buffer_pos;
    
    // Window function (pre-computed)
    float m_window[FFT_SIZE];
    
    // Working buffers (pre-allocated to avoid per-frame alloc)
    mutable std::vector<float> m_windowed;
    mutable std::vector<std::complex<float>> m_fft_out;
    mutable std::vector<float> m_magnitude;
};

// Key name tables
static const char* const g_key_names_ascii[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

static const char* const g_key_names_utf8[] = {
    "C", "C#/Db", "D", "D#/Eb", "E", "F",
    "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"
};
