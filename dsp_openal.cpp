#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <math.h>
#include <vector>
#include <string>

// OpenAL headers (for type definitions and constants only - we load dynamically)
#include "openal/include/AL/al.h"
#include "openal/include/AL/alc.h"
#include "openal/include/AL/alext.h"
#include "openal/include/AL/efx.h"
#include "openal/include/AL/efx-presets.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// OpenAL Dynamic Loader
// ============================================================================

static HMODULE g_hOpenAL = nullptr;

// Core AL functions
static decltype(&alGetError) p_alGetError = nullptr;
static decltype(&alGenSources) p_alGenSources = nullptr;
static decltype(&alDeleteSources) p_alDeleteSources = nullptr;
static decltype(&alGenBuffers) p_alGenBuffers = nullptr;
static decltype(&alDeleteBuffers) p_alDeleteBuffers = nullptr;
static decltype(&alBufferData) p_alBufferData = nullptr;
static decltype(&alSourcei) p_alSourcei = nullptr;
static decltype(&alSourcef) p_alSourcef = nullptr;
static decltype(&alSource3f) p_alSource3f = nullptr;
static decltype(&alSource3i) p_alSource3i = nullptr;
static decltype(&alSourcePlay) p_alSourcePlay = nullptr;
static decltype(&alSourceStop) p_alSourceStop = nullptr;
static decltype(&alGetSourcei) p_alGetSourcei = nullptr;
static decltype(&alSourceQueueBuffers) p_alSourceQueueBuffers = nullptr;
static decltype(&alSourceUnqueueBuffers) p_alSourceUnqueueBuffers = nullptr;
static decltype(&alListenerf) p_alListenerf = nullptr;
static decltype(&alListener3f) p_alListener3f = nullptr;
static decltype(&alListenerfv) p_alListenerfv = nullptr;
static decltype(&alDistanceModel) p_alDistanceModel = nullptr;

// Core ALC functions
static decltype(&alcCloseDevice) p_alcCloseDevice = nullptr;
static decltype(&alcCreateContext) p_alcCreateContext = nullptr;
static decltype(&alcDestroyContext) p_alcDestroyContext = nullptr;
static decltype(&alcMakeContextCurrent) p_alcMakeContextCurrent = nullptr;
static decltype(&alcGetError) p_alcGetError = nullptr;
static decltype(&alcGetProcAddress) p_alcGetProcAddress = nullptr;
static decltype(&alcIsExtensionPresent) p_alcIsExtensionPresent = nullptr;

// Loopback extension
static LPALCLOOPBACKOPENDEVICESOFT p_alcLoopbackOpenDeviceSOFT = nullptr;
static LPALCISRENDERFORMATSUPPORTEDSOFT p_alcIsRenderFormatSupportedSOFT = nullptr;
static LPALCRENDERSAMPLESSOFT p_alcRenderSamplesSOFT = nullptr;

// EFX functions
static LPALGENEFFECTS p_alGenEffects = nullptr;
static LPALDELETEEFFECTS p_alDeleteEffects = nullptr;
static LPALEFFECTI p_alEffecti = nullptr;
static LPALEFFECTF p_alEffectf = nullptr;
static LPALEFFECTFV p_alEffectfv = nullptr;
static LPALGENAUXILIARYEFFECTSLOTS p_alGenAuxiliaryEffectSlots = nullptr;
static LPALDELETEAUXILIARYEFFECTSLOTS p_alDeleteAuxiliaryEffectSlots = nullptr;
static LPALAUXILIARYEFFECTSLOTI p_alAuxiliaryEffectSloti = nullptr;
static LPALAUXILIARYEFFECTSLOTF p_alAuxiliaryEffectSlotf = nullptr;

static bool g_openal_available = false;

bool OpenAL_Init(HMODULE hComponent) {
    if (g_hOpenAL) return g_openal_available;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hComponent, path, MAX_PATH);
    std::wstring basePath(path);
    size_t pos = basePath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) basePath = basePath.substr(0, pos);

    std::wstring oalDir = basePath + L"\\openal";
    std::wstring oalPath = oalDir + L"\\soft_oal.dll";

    SetDllDirectoryW(oalDir.c_str());
    g_hOpenAL = LoadLibraryW(oalPath.c_str());
    SetDllDirectoryW(nullptr);

    if (!g_hOpenAL) {
        console::printf("OpenAL: Failed to load soft_oal.dll from %ls", oalPath.c_str());
        return false;
    }

    #define LOAD_AL(name) p_##name = (decltype(p_##name))GetProcAddress(g_hOpenAL, #name)
    LOAD_AL(alGetError);
    LOAD_AL(alGenSources);
    LOAD_AL(alDeleteSources);
    LOAD_AL(alGenBuffers);
    LOAD_AL(alDeleteBuffers);
    LOAD_AL(alBufferData);
    LOAD_AL(alSourcei);
    LOAD_AL(alSourcef);
    LOAD_AL(alSource3f);
    LOAD_AL(alSource3i);
    LOAD_AL(alSourcePlay);
    LOAD_AL(alSourceStop);
    LOAD_AL(alGetSourcei);
    LOAD_AL(alSourceQueueBuffers);
    LOAD_AL(alSourceUnqueueBuffers);
    LOAD_AL(alListenerf);
    LOAD_AL(alListener3f);
    LOAD_AL(alListenerfv);
    LOAD_AL(alDistanceModel);
    LOAD_AL(alcCloseDevice);
    LOAD_AL(alcCreateContext);
    LOAD_AL(alcDestroyContext);
    LOAD_AL(alcMakeContextCurrent);
    LOAD_AL(alcGetError);
    LOAD_AL(alcGetProcAddress);
    LOAD_AL(alcIsExtensionPresent);
    #undef LOAD_AL

    // Loopback extension (must use alcGetProcAddress after creating a NULL device, 
    // or just GetProcAddress since soft_oal always has it)
    p_alcLoopbackOpenDeviceSOFT = (LPALCLOOPBACKOPENDEVICESOFT)GetProcAddress(g_hOpenAL, "alcLoopbackOpenDeviceSOFT");
    p_alcIsRenderFormatSupportedSOFT = (LPALCISRENDERFORMATSUPPORTEDSOFT)GetProcAddress(g_hOpenAL, "alcIsRenderFormatSupportedSOFT");
    p_alcRenderSamplesSOFT = (LPALCRENDERSAMPLESSOFT)GetProcAddress(g_hOpenAL, "alcRenderSamplesSOFT");

    // EFX functions
    p_alGenEffects = (LPALGENEFFECTS)GetProcAddress(g_hOpenAL, "alGenEffects");
    p_alDeleteEffects = (LPALDELETEEFFECTS)GetProcAddress(g_hOpenAL, "alDeleteEffects");
    p_alEffecti = (LPALEFFECTI)GetProcAddress(g_hOpenAL, "alEffecti");
    p_alEffectf = (LPALEFFECTF)GetProcAddress(g_hOpenAL, "alEffectf");
    p_alEffectfv = (LPALEFFECTFV)GetProcAddress(g_hOpenAL, "alEffectfv");
    p_alGenAuxiliaryEffectSlots = (LPALGENAUXILIARYEFFECTSLOTS)GetProcAddress(g_hOpenAL, "alGenAuxiliaryEffectSlots");
    p_alDeleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS)GetProcAddress(g_hOpenAL, "alDeleteAuxiliaryEffectSlots");
    p_alAuxiliaryEffectSloti = (LPALAUXILIARYEFFECTSLOTI)GetProcAddress(g_hOpenAL, "alAuxiliaryEffectSloti");
    p_alAuxiliaryEffectSlotf = (LPALAUXILIARYEFFECTSLOTF)GetProcAddress(g_hOpenAL, "alAuxiliaryEffectSlotf");

    // Verify critical functions
    if (!p_alcLoopbackOpenDeviceSOFT || !p_alcRenderSamplesSOFT ||
        !p_alGenSources || !p_alGenBuffers || !p_alBufferData ||
        !p_alcCreateContext || !p_alcMakeContextCurrent) {
        console::print("OpenAL: Missing critical functions");
        FreeLibrary(g_hOpenAL);
        g_hOpenAL = nullptr;
        return false;
    }

    g_openal_available = true;
    console::print("OpenAL: soft_oal.dll loaded successfully");
    return true;
}

void OpenAL_Uninit() {
    g_hOpenAL = nullptr;
    g_openal_available = false;
}

// ============================================================================
// Spatial Audio Preset Definitions
// ============================================================================

struct spatial_preset_t {
    const char* name;           // Internal name
    const char* display_name;   // Chinese display name
    EFXEAXREVERBPROPERTIES reverb;
    bool has_reverb;            // false = no reverb (pure HRTF only)
};

static const spatial_preset_t g_spatial_presets[] = {
    { "none",           u8"无混响 (纯HRTF)",      {}, false },
    { "generic",        u8"通用",                  EFX_REVERB_PRESET_GENERIC, true },
    { "room",           u8"房间",                  EFX_REVERB_PRESET_ROOM, true },
    { "livingroom",     u8"客厅",                  EFX_REVERB_PRESET_LIVINGROOM, true },
    { "bathroom",       u8"浴室",                  EFX_REVERB_PRESET_BATHROOM, true },
    { "stoneroom",      u8"石头房间",              EFX_REVERB_PRESET_STONEROOM, true },
    { "auditorium",     u8"礼堂",                  EFX_REVERB_PRESET_AUDITORIUM, true },
    { "concerthall",    u8"音乐厅",                EFX_REVERB_PRESET_CONCERTHALL, true },
    { "cave",           u8"洞穴",                  EFX_REVERB_PRESET_CAVE, true },
    { "forest",         u8"森林",                  EFX_REVERB_PRESET_FOREST, true },
    { "mountains",      u8"山脉",                  EFX_REVERB_PRESET_MOUNTAINS, true },
    { "underwater",     u8"水下",                  EFX_REVERB_PRESET_UNDERWATER, true },
    { "city",           u8"城市",                  EFX_REVERB_PRESET_CITY, true },
    { "subway",         u8"地铁",                  EFX_REVERB_PRESET_CITY_SUBWAY, true },
    { "castle_hall",    u8"城堡大厅",              EFX_REVERB_PRESET_CASTLE_HALL, true },
    { "gymnasium",      u8"体育馆",                EFX_REVERB_PRESET_SPORT_GYMNASIUM, true },
    { "pipe_large",     u8"大管道",                EFX_REVERB_PRESET_PIPE_LARGE, true },
    { "heaven",         u8"天堂",                  EFX_REVERB_PRESET_MOOD_HEAVEN, true },
    { "hell",           u8"地狱",                  EFX_REVERB_PRESET_MOOD_HELL, true },
    { "chapel",         u8"教堂",                  EFX_REVERB_PRESET_CHAPEL, true },
    { "psychotic",      u8"迷幻",                  EFX_REVERB_PRESET_PSYCHOTIC, true },
};

static const int NUM_SPATIAL_PRESETS = sizeof(g_spatial_presets) / sizeof(g_spatial_presets[0]);

// ============================================================================
// DSP Preset Serialization
// ============================================================================

static const GUID guid_openal_spatial_dsp = 
    { 0xa7c3e201, 0x4b8f, 0x4d2a, { 0x91, 0x5c, 0x6d, 0x82, 0xf3, 0x1a, 0xb4, 0x77 } };

struct openal_spatial_params_t {
    int   preset_index;     // Index into g_spatial_presets
    float rotation_speed;   // Seconds per full revolution (0 = no rotation)
    float elevation;        // Elevation angle in degrees (-90 to +90)
    float distance;         // Distance from listener (0.1 to 10.0)
    float mix;              // Wet/dry mix (0-100%)
    float azimuth;          // Static azimuth when rotation_speed is 0 (-180 to +180)
    float reverb_gain;      // Reverb intensity (0.0 to 2.0)
};

static void parse_openal_preset(const dsp_preset& in, openal_spatial_params_t& params) {
    if (in.get_owner() == guid_openal_spatial_dsp && 
        in.get_data_size() == sizeof(openal_spatial_params_t)) {
        memcpy(&params, in.get_data(), sizeof(params));
        // Validate
        if (params.preset_index < 0 || params.preset_index >= NUM_SPATIAL_PRESETS)
            params.preset_index = 0;
        if (params.rotation_speed < 0) params.rotation_speed = 0;
        if (params.distance < 0.1f) params.distance = 0.1f;
        if (params.distance > 10.0f) params.distance = 10.0f;
        if (params.mix < 0.0f) params.mix = 0.0f;
        if (params.mix > 100.0f) params.mix = 100.0f;
        if (in.get_data_size() == sizeof(openal_spatial_params_t)) {
            // New format with reverb_gain
            if (params.reverb_gain < 0.0f) params.reverb_gain = 0.0f;
            if (params.reverb_gain > 2.0f) params.reverb_gain = 2.0f;
        } else {
            // Old format fallback
            params.reverb_gain = 1.0f;
        }
    } else {
        // Default: HiFi 3D - Concert Hall, slow rotation, wide distance
        params.preset_index = 7; // Concert Hall
        params.rotation_speed = 50.0f; // 50s per revolution (slow)
        params.elevation = 0.0f;
        params.distance = 3.0f; // Large rotation radius
        params.mix = 100.0f;
        params.azimuth = 0.0f;
        params.reverb_gain = 1.0f;
    }
}

// Global overrides for live preview (Accessibility Dialog)
std::atomic<int> g_spatial_override_preset{-1};
std::atomic<float> g_spatial_override_rotation{-1.0f};
std::atomic<float> g_spatial_override_elevation{-999.0f};
std::atomic<float> g_spatial_override_distance{-1.0f};
std::atomic<float> g_spatial_override_mix{-1.0f};
std::atomic<float> g_spatial_override_azimuth{-999.0f};
std::atomic<float> g_spatial_override_reverb{-1.0f};

static void make_openal_preset(const openal_spatial_params_t& params, dsp_preset& out) {
    out.set_owner(guid_openal_spatial_dsp);
    out.set_data(&params, sizeof(params));
}

// ============================================================================
// OpenAL 3D Spatial Audio DSP
// ============================================================================

static void RunOpenALSpatialConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback);

class dsp_openal_spatial : public dsp_impl_base {
    // OpenAL objects
    ALCdevice*  m_device = nullptr;
    ALCcontext* m_context = nullptr;
    ALuint      m_source = 0;
    ALuint      m_effect = 0;
    ALuint      m_effect_slot = 0;

    // Audio format tracking
    unsigned m_sample_rate = 0;
    unsigned m_channels = 0;

    // Parameters
    openal_spatial_params_t m_params;

    // Rotation state
    double m_rotation_angle = 0.0; // Current angle in radians

    // Pre-allocated render buffer (member, never stack-allocated in hot path)
    std::vector<float> m_render_buf;

    // Pre-allocated output buffer (must be audio_sample, which is double on x64)
    std::vector<audio_sample> m_output_buffer;

    // Parameter accumulators for fixed-block processing
    static const int BLOCK_SIZE = 2048;
    std::vector<float> m_input_accum;
    std::vector<float> m_dry_accum;
    bool m_is_first_block = true;
    std::vector<ALuint> m_free_buffers;

    int m_last_preset_index = -1;
    float m_last_reverb_gain = -1.0f;

public:
    dsp_openal_spatial(dsp_preset const& in) {
        parse_openal_preset(in, m_params);
    }

    ~dsp_openal_spatial() {
        cleanup_openal();
    }

    static GUID g_get_guid() { return guid_openal_spatial_dsp; }
    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
        RunOpenALSpatialConfigPopup(p_data, p_parent, p_callback);
    }
    static void g_get_name(pfc::string_base& p_out) { p_out = u8"时间魔术师 - 3D空间音频"; }
    static bool g_get_default_preset(dsp_preset& p_out) {
        openal_spatial_params_t params;
        params.preset_index = 7;      // Concert Hall
        params.rotation_speed = 50.0f; // 50 seconds per revolution
        params.elevation = 0.0f;
        params.distance = 3.0f;
        params.mix = 100.0f;
        params.azimuth = 0.0f;
        params.reverb_gain = 1.0f;
        make_openal_preset(params, p_out);
        return true;
    }

    bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) override {
        if (!g_openal_available) return true;

        unsigned sr = chunk->get_sample_rate();
        unsigned ch = chunk->get_channels();
        size_t sample_count = chunk->get_sample_count();
        if (sample_count == 0) return true;

        // Reinitialize if format changed
        if (sr != m_sample_rate || ch != m_channels) {
            cleanup_openal();
            m_sample_rate = sr;
            m_channels = ch;
            if (!init_openal()) {
                return true; // Pass through on failure
            }
        }

        if (!m_device || !m_context || !m_source) return true;

        // Make our context current
        p_alcMakeContextCurrent(m_context);

        // Append input to accumulators using direct indexed writes (no push_back)
        const audio_sample* data = chunk->get_data();
        size_t old_input_size = m_input_accum.size();
        size_t new_input_size = old_input_size + sample_count;
        m_input_accum.resize(new_input_size);
        m_dry_accum.resize(new_input_size * 2);

        for (size_t i = 0; i < sample_count; i++) {
            float sum = 0.0f;
            for (unsigned c = 0; c < ch; c++) {
                sum += (float)data[i * ch + c];
            }
            m_input_accum[old_input_size + i] = sum / (float)ch;

            // Save stereo dry signal for mixing
            size_t dry_base = (old_input_size + i) * 2;
            if (ch >= 2) {
                m_dry_accum[dry_base + 0] = (float)data[i * ch + 0];
                m_dry_accum[dry_base + 1] = (float)data[i * ch + 1];
            } else {
                float mono = sum / (float)ch;
                m_dry_accum[dry_base + 0] = mono;
                m_dry_accum[dry_base + 1] = mono;
            }
        }

        // Count how many complete blocks we can process
        size_t total_input = m_input_accum.size();
        size_t num_blocks = total_input / BLOCK_SIZE;
        if (num_blocks == 0) {
            // Not enough data yet — tell framework to drop this chunk
            return false;
        }

        // Pre-allocate output for exact number of output samples
        size_t output_floats = num_blocks * BLOCK_SIZE * 2;
        m_output_buffer.resize(output_floats);
        size_t out_pos = 0;

        size_t input_idx = 0;
        size_t dry_idx = 0;

        for (size_t block = 0; block < num_blocks; block++) {
            if (m_is_first_block) {
                m_is_first_block = false;
                if (m_free_buffers.empty()) break;
                ALuint lead_buf = m_free_buffers.back();
                m_free_buffers.pop_back();
                // Use render_buf as temp silence buffer
                memset(m_render_buf.data(), 0, BLOCK_SIZE * sizeof(float));
                p_alBufferData(lead_buf, AL_FORMAT_MONO_FLOAT32, m_render_buf.data(), BLOCK_SIZE * sizeof(float), m_sample_rate);
                p_alSourceQueueBuffers(m_source, 1, &lead_buf);
                p_alSourcePlay(m_source);
            }

            // Update source position
            float live_rotation = g_spatial_override_rotation.load();
            float eff_rotation = (live_rotation >= 0.0f) ? live_rotation : m_params.rotation_speed;

            float live_azimuth = g_spatial_override_azimuth.load();
            float eff_azimuth = (live_azimuth > -990.0f) ? live_azimuth : m_params.azimuth;

            float live_elevation = g_spatial_override_elevation.load();
            float eff_elevation = (live_elevation > -990.0f) ? live_elevation : m_params.elevation;

            float live_distance = g_spatial_override_distance.load();
            float eff_distance = (live_distance >= 0.0f) ? live_distance : m_params.distance;

            double chunk_duration = (double)BLOCK_SIZE / (double)m_sample_rate;
            if (eff_rotation > 0.0f) {
                double radians_per_second = (2.0 * M_PI) / (double)eff_rotation;
                m_rotation_angle += radians_per_second * chunk_duration;
                if (m_rotation_angle > 2.0 * M_PI) m_rotation_angle -= 2.0 * M_PI;
            } else {
                m_rotation_angle = (double)eff_azimuth * M_PI / 180.0;
            }

            float elev_rad = eff_elevation * (float)(M_PI / 180.0);
            float cos_elev = cosf(elev_rad);
            float x = sinf((float)m_rotation_angle) * eff_distance * cos_elev;
            float y = sinf(elev_rad) * eff_distance;
            float z = -cosf((float)m_rotation_angle) * eff_distance * cos_elev;
            p_alSource3f(m_source, AL_POSITION, x, y, z);

            int live_preset = g_spatial_override_preset.load();
            int eff_preset = (live_preset >= 0) ? live_preset : m_params.preset_index;
            if (eff_preset != m_last_preset_index) {
                apply_reverb_preset(eff_preset);
                m_last_preset_index = eff_preset;
                m_last_reverb_gain = -1.0f;
            }

            float live_reverb = g_spatial_override_reverb.load();
            float eff_reverb = (live_reverb >= 0.0f) ? live_reverb : m_params.reverb_gain;
            if (eff_reverb != m_last_reverb_gain) {
                apply_reverb_preset(eff_preset); // Reapply preset to scale EFX gains
                if (p_alAuxiliaryEffectSlotf && m_effect_slot) {
                    p_alAuxiliaryEffectSlotf(m_effect_slot, AL_EFFECTSLOT_GAIN, (std::min)(1.0f, eff_reverb));
                }
                m_last_reverb_gain = eff_reverb;
            }

            // Get a free OpenAL buffer — NEVER call alSourceUnqueueBuffers when processed == 0
            ALuint use_buf = 0;
            if (!m_free_buffers.empty()) {
                use_buf = m_free_buffers.back();
                m_free_buffers.pop_back();
            } else {
                // Try to recycle processed buffers
                ALint processed = 0;
                p_alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
                if (processed > 0) {
                    p_alSourceUnqueueBuffers(m_source, 1, &use_buf);
                } else {
                    // No buffers available at all — output dry signal for this block
                    for (size_t i = 0; i < BLOCK_SIZE; i++) {
                        m_output_buffer[out_pos++] = (audio_sample)m_dry_accum[dry_idx + i * 2 + 0];
                        m_output_buffer[out_pos++] = (audio_sample)m_dry_accum[dry_idx + i * 2 + 1];
                    }
                    input_idx += BLOCK_SIZE;
                    dry_idx += BLOCK_SIZE * 2;
                    continue;
                }
            }

            // Queue mono data into OpenAL
            p_alBufferData(use_buf, AL_FORMAT_MONO_FLOAT32, m_input_accum.data() + input_idx, BLOCK_SIZE * sizeof(float), m_sample_rate);
            p_alSourceQueueBuffers(m_source, 1, &use_buf);

            // Keep source playing
            ALint state = 0;
            p_alGetSourcei(m_source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                p_alSourcePlay(m_source);
            }

            // Render into pre-allocated member buffer (no heap alloc here)
            p_alcRenderSamplesSOFT(m_device, m_render_buf.data(), BLOCK_SIZE);

            // Mix wet/dry directly into pre-sized output buffer (indexed, no push_back)
            float live_mix = g_spatial_override_mix.load();
            float eff_mix = (live_mix >= 0.0f) ? live_mix : m_params.mix;
            float wet = eff_mix / 100.0f;
            float dry_mix = 1.0f - wet;
            for (size_t i = 0; i < BLOCK_SIZE; i++) {
                float dl = m_dry_accum[dry_idx + i * 2 + 0];
                float dr = m_dry_accum[dry_idx + i * 2 + 1];
                m_output_buffer[out_pos++] = (audio_sample)(m_render_buf[i * 2 + 0] * wet + dl * dry_mix);
                m_output_buffer[out_pos++] = (audio_sample)(m_render_buf[i * 2 + 1] * wet + dr * dry_mix);
            }

            input_idx += BLOCK_SIZE;
            dry_idx += BLOCK_SIZE * 2;

            // Recycle processed buffers
            ALint processed = 0;
            p_alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
            while (processed > 0) {
                ALuint buf;
                p_alSourceUnqueueBuffers(m_source, 1, &buf);
                m_free_buffers.push_back(buf);
                processed--;
            }
        }

        // Shift remaining unconsumed data to front using memmove (overlap-safe)
        if (input_idx > 0) {
            size_t remaining = m_input_accum.size() - input_idx;
            if (remaining > 0) {
                memmove(m_input_accum.data(), m_input_accum.data() + input_idx, remaining * sizeof(float));
            }
            m_input_accum.resize(remaining);

            size_t dry_remaining = m_dry_accum.size() - dry_idx;
            if (dry_remaining > 0) {
                memmove(m_dry_accum.data(), m_dry_accum.data() + dry_idx, dry_remaining * sizeof(float));
            }
            m_dry_accum.resize(dry_remaining);
        }

        if (out_pos == 0) {
            // No output produced — tell framework to drop this chunk
            return false;
        }

        chunk->set_data(m_output_buffer.data(), out_pos / 2, 2, m_sample_rate);
        return true;
    }

    void on_endofplayback(abort_callback& p_abort) override {
        drain(p_abort);
        flush();
    }

    void on_endoftrack(abort_callback& p_abort) override {
        drain(p_abort);
        flush();
    }

    void drain(abort_callback& p_abort) {
        if (!m_device || !m_source || m_input_accum.empty()) return;

        p_alcMakeContextCurrent(m_context);

        // Pad remaining data to fill the last block
        // Use modular arithmetic to avoid unsigned underflow
        size_t current_size = m_input_accum.size();
        size_t remainder = current_size % BLOCK_SIZE;
        if (remainder > 0) {
            size_t pad_samples = BLOCK_SIZE - remainder;
            m_input_accum.resize(current_size + pad_samples, 0.0f);
            m_dry_accum.resize((current_size + pad_samples) * 2, 0.0f);
        }

        size_t num_blocks = m_input_accum.size() / BLOCK_SIZE;
        if (num_blocks == 0) return;

        m_output_buffer.resize(num_blocks * BLOCK_SIZE * 2);
        size_t out_pos = 0;

        size_t input_idx = 0;
        size_t dry_idx = 0;

        for (size_t block = 0; block < num_blocks; block++) {
            p_abort.check();

            ALuint use_buf = 0;
            if (!m_free_buffers.empty()) {
                use_buf = m_free_buffers.back();
                m_free_buffers.pop_back();
            } else {
                ALint processed = 0;
                p_alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
                if (processed > 0) {
                    p_alSourceUnqueueBuffers(m_source, 1, &use_buf);
                } else {
                    break; // No buffers — stop draining
                }
            }

            p_alBufferData(use_buf, AL_FORMAT_MONO_FLOAT32, m_input_accum.data() + input_idx, BLOCK_SIZE * sizeof(float), m_sample_rate);
            p_alSourceQueueBuffers(m_source, 1, &use_buf);

            ALint state = 0;
            p_alGetSourcei(m_source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) p_alSourcePlay(m_source);

            p_alcRenderSamplesSOFT(m_device, m_render_buf.data(), BLOCK_SIZE);

            float live_mix = g_spatial_override_mix.load();
            float eff_mix = (live_mix >= 0.0f) ? live_mix : m_params.mix;
            float wet = eff_mix / 100.0f;
            float dry_mix = 1.0f - wet;
            for (size_t i = 0; i < BLOCK_SIZE; i++) {
                m_output_buffer[out_pos++] = (audio_sample)(m_render_buf[i * 2 + 0] * wet + m_dry_accum[dry_idx + i * 2 + 0] * dry_mix);
                m_output_buffer[out_pos++] = (audio_sample)(m_render_buf[i * 2 + 1] * wet + m_dry_accum[dry_idx + i * 2 + 1] * dry_mix);
            }

            input_idx += BLOCK_SIZE;
            dry_idx += BLOCK_SIZE * 2;
        }

        m_input_accum.clear();
        m_dry_accum.clear();

        if (out_pos > 0) {
            audio_chunk* out_chunk = insert_chunk(out_pos / 2);
            out_chunk->set_data(m_output_buffer.data(), out_pos / 2, 2, m_sample_rate);
        }
    }

    void flush() override {
        if (m_source && m_device && m_context) {
            p_alcMakeContextCurrent(m_context);
            p_alSourceStop(m_source);

            // Unqueue all buffers
            ALint queued = 0;
            p_alGetSourcei(m_source, AL_BUFFERS_QUEUED, &queued);
            while (queued > 0) {
                ALuint buf;
                p_alSourceUnqueueBuffers(m_source, 1, &buf);
                m_free_buffers.push_back(buf);
                queued--;
            }
        }
        m_rotation_angle = 0.0;
        m_input_accum.clear();
        m_dry_accum.clear();
        m_is_first_block = true;
    }

    double get_latency() override { 
        if (m_sample_rate > 0) return (double)BLOCK_SIZE / (double)m_sample_rate;
        return 0;
    }
    bool need_track_change_mark() override { return false; }

private:
    bool init_openal() {
        if (!g_openal_available) return false;

        // Create loopback device with HRTF
        m_device = p_alcLoopbackOpenDeviceSOFT(nullptr);
        if (!m_device) {
            console::print("OpenAL Spatial: Failed to create loopback device");
            return false;
        }

        // Set up attributes: stereo float output with HRTF
        ALCint attrs[] = {
            ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
            ALC_FORMAT_TYPE_SOFT,     ALC_FLOAT_SOFT,
            ALC_FREQUENCY,            (ALCint)m_sample_rate,
            ALC_HRTF_SOFT,            ALC_TRUE,
            0
        };

        m_context = p_alcCreateContext(m_device, attrs);
        if (!m_context) {
            console::printf("OpenAL Spatial: Failed to create context (error: 0x%x)", p_alcGetError(m_device));
            p_alcCloseDevice(m_device);
            m_device = nullptr;
            return false;
        }

        p_alcMakeContextCurrent(m_context);

        // Disable distance attenuation (we control distance manually)
        p_alDistanceModel(AL_NONE);

        // Create source
        p_alGenSources(1, &m_source);
        p_alSourcef(m_source, AL_GAIN, 1.0f);
        p_alSourcef(m_source, AL_PITCH, 1.0f);
        p_alSourcei(m_source, AL_LOOPING, AL_FALSE);
        p_alSourcei(m_source, AL_SOURCE_RELATIVE, AL_FALSE);

        // Set listener at origin, facing -Z
        p_alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
        ALfloat ori[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
        p_alListenerfv(AL_ORIENTATION, ori);

        // Create buffers for streaming
        static const int NUM_AL_BUFFERS = 16;
        ALuint al_buffers[NUM_AL_BUFFERS];
        p_alGenBuffers(NUM_AL_BUFFERS, al_buffers);
        m_free_buffers.clear();
        for(int i=0; i<NUM_AL_BUFFERS; i++) {
            m_free_buffers.push_back(al_buffers[i]);
        }

        // Pre-allocate render buffer (reused every block, never reallocated in hot path)
        m_render_buf.assign(BLOCK_SIZE * 2, 0.0f);

        // Set up EFX if available
        if (p_alGenEffects && p_alGenAuxiliaryEffectSlots) {
            p_alGenEffects(1, &m_effect);
            p_alGenAuxiliaryEffectSlots(1, &m_effect_slot);
            
            // Connect source to effect slot
            p_alSource3i(m_source, AL_AUXILIARY_SEND_FILTER, (ALint)m_effect_slot, 0, AL_FILTER_NULL);
        }

        m_last_preset_index = -1; // Force preset application
        m_rotation_angle = 0.0;

        return true;
    }

    void apply_reverb_preset(int index) {
        if (!m_effect || !m_effect_slot || !p_alEffecti) return;

        if (index < 0 || index >= NUM_SPATIAL_PRESETS) index = 0;

        const spatial_preset_t& preset = g_spatial_presets[index];

        if (!preset.has_reverb) {
            // Disable reverb - set null effect
            p_alAuxiliaryEffectSloti(m_effect_slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
            return;
        }

        const EFXEAXREVERBPROPERTIES& r = preset.reverb;

        p_alEffecti(m_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

        // Apply reverb gain scaling for presets with weak inherent gain (like Room)
        float eff_reverb = g_spatial_override_reverb.load();
        if (eff_reverb < 0.0f) eff_reverb = m_params.reverb_gain;
        float scaled_gain = (std::min)(1.0f, r.flGain * eff_reverb);
        float scaled_ref_gain = (std::min)(3.16f, r.flReflectionsGain * eff_reverb);
        float scaled_late_gain = (std::min)(3.16f, r.flLateReverbGain * eff_reverb);

        p_alEffectf(m_effect, AL_EAXREVERB_DENSITY, r.flDensity);
        p_alEffectf(m_effect, AL_EAXREVERB_DIFFUSION, r.flDiffusion);
        p_alEffectf(m_effect, AL_EAXREVERB_GAIN, scaled_gain);
        p_alEffectf(m_effect, AL_EAXREVERB_GAINHF, r.flGainHF);
        p_alEffectf(m_effect, AL_EAXREVERB_GAINLF, r.flGainLF);
        p_alEffectf(m_effect, AL_EAXREVERB_DECAY_TIME, r.flDecayTime);
        p_alEffectf(m_effect, AL_EAXREVERB_DECAY_HFRATIO, r.flDecayHFRatio);
        p_alEffectf(m_effect, AL_EAXREVERB_DECAY_LFRATIO, r.flDecayLFRatio);
        p_alEffectf(m_effect, AL_EAXREVERB_REFLECTIONS_GAIN, scaled_ref_gain);
        p_alEffectf(m_effect, AL_EAXREVERB_REFLECTIONS_DELAY, r.flReflectionsDelay);
        p_alEffectfv(m_effect, AL_EAXREVERB_REFLECTIONS_PAN, r.flReflectionsPan);
        p_alEffectf(m_effect, AL_EAXREVERB_LATE_REVERB_GAIN, scaled_late_gain);
        p_alEffectf(m_effect, AL_EAXREVERB_LATE_REVERB_DELAY, r.flLateReverbDelay);
        p_alEffectfv(m_effect, AL_EAXREVERB_LATE_REVERB_PAN, r.flLateReverbPan);
        p_alEffectf(m_effect, AL_EAXREVERB_ECHO_TIME, r.flEchoTime);
        p_alEffectf(m_effect, AL_EAXREVERB_ECHO_DEPTH, r.flEchoDepth);
        p_alEffectf(m_effect, AL_EAXREVERB_MODULATION_TIME, r.flModulationTime);
        p_alEffectf(m_effect, AL_EAXREVERB_MODULATION_DEPTH, r.flModulationDepth);
        p_alEffectf(m_effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, r.flAirAbsorptionGainHF);
        p_alEffectf(m_effect, AL_EAXREVERB_HFREFERENCE, r.flHFReference);
        p_alEffectf(m_effect, AL_EAXREVERB_LFREFERENCE, r.flLFReference);
        p_alEffectf(m_effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, r.flRoomRolloffFactor);
        p_alEffecti(m_effect, AL_EAXREVERB_DECAY_HFLIMIT, r.iDecayHFLimit);

        // Attach effect to slot
        p_alAuxiliaryEffectSloti(m_effect_slot, AL_EFFECTSLOT_EFFECT, (ALint)m_effect);
    }

    void cleanup_openal() {
        if (!g_hOpenAL) {
            m_context = nullptr;
            m_device = nullptr;
            return;
        }

        if (m_context) {
            p_alcMakeContextCurrent(m_context);

            if (m_source) {
                p_alSourceStop(m_source);
                // Unqueue all buffers
                ALint queued = 0;
                p_alGetSourcei(m_source, AL_BUFFERS_QUEUED, &queued);
                while (queued > 0) {
                    ALuint buf;
                    p_alSourceUnqueueBuffers(m_source, 1, &buf);
                    queued--;
                }
                p_alDeleteSources(1, &m_source);
                m_source = 0;
            }

            if (!m_free_buffers.empty()) {
                p_alDeleteBuffers((ALsizei)m_free_buffers.size(), m_free_buffers.data());
                m_free_buffers.clear();
            }

            if (m_effect_slot) {
                p_alDeleteAuxiliaryEffectSlots(1, &m_effect_slot);
                m_effect_slot = 0;
            }
            if (m_effect) {
                p_alDeleteEffects(1, &m_effect);
                m_effect = 0;
            }

            p_alcMakeContextCurrent(nullptr);
            p_alcDestroyContext(m_context);
            m_context = nullptr;
        }

        if (m_device) {
            p_alcCloseDevice(m_device);
            m_device = nullptr;
        }

        m_sample_rate = 0;
        m_channels = 0;
        m_is_first_block = true;
        m_last_preset_index = -1;
    }
};

// ============================================================================
// Configuration Dialog
// ============================================================================

class CDialogDSPOpenALSpatial : public CDialogImpl<CDialogDSPOpenALSpatial> {
public:
    enum { IDD = IDD_DSP_OPENAL_SPATIAL };

    CDialogDSPOpenALSpatial(const dsp_preset& initData) {
        parse_openal_preset(initData, m_params);
    }

    const dsp_preset& GetPreset() const { return m_preset; }

    BEGIN_MSG_MAP(CDialogDSPOpenALSpatial)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnCloseCmd)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCloseCmd)
        COMMAND_HANDLER_EX(IDC_SPATIAL_PRESET, CBN_SELCHANGE, OnPresetChanged)
        MSG_WM_HSCROLL(OnHScroll)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam) {
        // Fill preset combo box
        CComboBox combo = GetDlgItem(IDC_SPATIAL_PRESET);
        for (int i = 0; i < NUM_SPATIAL_PRESETS; i++) {
            pfc::stringcvt::string_wide_from_utf8 wstr(g_spatial_presets[i].display_name);
            combo.AddString(wstr);
        }
        combo.SetCurSel(m_params.preset_index);

        // Rotation speed slider (0 = no rotation, 1-60 seconds)
        CTrackBarCtrl sliderRotation = GetDlgItem(IDC_SPATIAL_ROTATION);
        sliderRotation.SetRange(0, 60);
        sliderRotation.SetPos((int)m_params.rotation_speed);

        // Elevation slider (-90 to +90)
        CTrackBarCtrl sliderElevation = GetDlgItem(IDC_SPATIAL_ELEVATION);
        sliderElevation.SetRange(-90, 90);
        sliderElevation.SetPos((int)m_params.elevation);
        
        // Azimuth slider (-180 to +180)
        CTrackBarCtrl sliderAzimuth = GetDlgItem(IDC_SPATIAL_AZIMUTH);
        sliderAzimuth.SetRange(-180, 180);
        sliderAzimuth.SetPos((int)m_params.azimuth);

        // Distance slider (1 to 100, representing 0.1 to 10.0)
        CTrackBarCtrl sliderDistance = GetDlgItem(IDC_SPATIAL_DISTANCE);
        sliderDistance.SetRange(1, 100);
        sliderDistance.SetPos((int)(m_params.distance * 10.0f));
        
        // Reverb Gain slider (0 to 200, representing 0.0 to 2.0)
        CTrackBarCtrl sliderReverbGain = GetDlgItem(IDC_SPATIAL_REVERB_GAIN);
        sliderReverbGain.SetRange(0, 200);
        sliderReverbGain.SetPos((int)(m_params.reverb_gain * 100.0f));

        // Mix slider (0 to 100)
        CTrackBarCtrl sliderMix = GetDlgItem(IDC_SPATIAL_MIX);
        sliderMix.SetRange(0, 100);
        sliderMix.SetPos((int)m_params.mix);

        UpdateLabels();
        return TRUE;
    }

    void OnCloseCmd(UINT uNotifyCode, int nID, CWindow wndCtl) {
        if (nID == IDOK) {
            CComboBox combo = GetDlgItem(IDC_SPATIAL_PRESET);
            m_params.preset_index = combo.GetCurSel();

            CTrackBarCtrl sliderRotation = GetDlgItem(IDC_SPATIAL_ROTATION);
            m_params.rotation_speed = (float)sliderRotation.GetPos();

            CTrackBarCtrl sliderElevation = GetDlgItem(IDC_SPATIAL_ELEVATION);
            m_params.elevation = (float)sliderElevation.GetPos();
            
            CTrackBarCtrl sliderAzimuth = GetDlgItem(IDC_SPATIAL_AZIMUTH);
            m_params.azimuth = (float)sliderAzimuth.GetPos();

            CTrackBarCtrl sliderDistance = GetDlgItem(IDC_SPATIAL_DISTANCE);
            m_params.distance = (float)sliderDistance.GetPos() / 10.0f;
            
            CTrackBarCtrl sliderReverbGain = GetDlgItem(IDC_SPATIAL_REVERB_GAIN);
            m_params.reverb_gain = (float)sliderReverbGain.GetPos() / 100.0f;

            CTrackBarCtrl sliderMix = GetDlgItem(IDC_SPATIAL_MIX);
            m_params.mix = (float)sliderMix.GetPos();

            make_openal_preset(m_params, m_preset);
        }
        EndDialog(nID);
    }

    void OnPresetChanged(UINT uNotifyCode, int nID, CWindow wndCtl) {
        UpdateLabels();
    }

    void OnHScroll(int nSBCode, short nPos, CScrollBar pScrollBar) {
        UpdateLabels();
    }

    void UpdateLabels() {
        CTrackBarCtrl sliderRotation = GetDlgItem(IDC_SPATIAL_ROTATION);
        CTrackBarCtrl sliderElevation = GetDlgItem(IDC_SPATIAL_ELEVATION);
        CTrackBarCtrl sliderAzimuth = GetDlgItem(IDC_SPATIAL_AZIMUTH);
        CTrackBarCtrl sliderDistance = GetDlgItem(IDC_SPATIAL_DISTANCE);
        CTrackBarCtrl sliderReverbGain = GetDlgItem(IDC_SPATIAL_REVERB_GAIN);
        CTrackBarCtrl sliderMix = GetDlgItem(IDC_SPATIAL_MIX);

        pfc::string8 str;
        
        int rot_val = sliderRotation.GetPos();
        if (rot_val == 0) {
            str = u8"不旋转 (启用方位角)";
        } else {
            str = pfc::format_int(rot_val);
            str += u8" 秒/圈";
        }
        uSetDlgItemText(*this, IDC_SPATIAL_ROTATION_LABEL, str);

        str = pfc::format_int(sliderElevation.GetPos());
        str += u8"°";
        uSetDlgItemText(*this, IDC_SPATIAL_ELEVATION_LABEL, str);
        
        str = pfc::format_int(sliderAzimuth.GetPos());
        str += u8"°";
        uSetDlgItemText(*this, IDC_SPATIAL_AZIMUTH_LABEL, str);

        float dist = (float)sliderDistance.GetPos() / 10.0f;
        str = pfc::format_float(dist, 0, 1);
        uSetDlgItemText(*this, IDC_SPATIAL_DISTANCE_LABEL, str);
        
        float gain = (float)sliderReverbGain.GetPos() / 100.0f;
        str = pfc::format_float(gain, 0, 2);
        uSetDlgItemText(*this, IDC_SPATIAL_REVERB_GAIN_LABEL, str);

        str = pfc::format_int(sliderMix.GetPos());
        str += "%";
        uSetDlgItemText(*this, IDC_SPATIAL_MIX_LABEL, str);
    }

    dsp_preset_impl m_preset;
    openal_spatial_params_t m_params;
};

static void RunOpenALSpatialConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
    CDialogDSPOpenALSpatial dlg(p_data);
    if (dlg.DoModal(p_parent) == IDOK) {
        p_callback.on_preset_changed(dlg.GetPreset());
    }
}

static dsp_factory_t<dsp_openal_spatial> g_dsp_openal_spatial_factory;

bool is_openal_active() {
    auto dcm = dsp_config_manager::get();
    dsp_preset_impl preset;
    return dcm->core_query_dsp(guid_openal_spatial_dsp, preset);
}

void save_openal_spatial_settings_to_dsp() {
    auto dcm = dsp_config_manager::get();
    dsp_preset_impl preset;
    if (!dcm->core_query_dsp(guid_openal_spatial_dsp, preset)) {
        return; // Do not auto-enable, just return if not in chain
    }
    
    openal_spatial_params_t params;
    parse_openal_preset(preset, params);
    
    if (g_spatial_override_preset.load() >= 0) params.preset_index = g_spatial_override_preset.load();
    if (g_spatial_override_rotation.load() >= 0.0f) params.rotation_speed = g_spatial_override_rotation.load();
    if (g_spatial_override_elevation.load() > -990.0f) params.elevation = g_spatial_override_elevation.load();
    if (g_spatial_override_distance.load() >= 0.0f) params.distance = g_spatial_override_distance.load();
    if (g_spatial_override_mix.load() >= 0.0f) params.mix = g_spatial_override_mix.load();
    if (g_spatial_override_azimuth.load() > -990.0f) params.azimuth = g_spatial_override_azimuth.load();
    if (g_spatial_override_reverb.load() >= 0.0f) params.reverb_gain = g_spatial_override_reverb.load();
    
    preset.set_owner(guid_openal_spatial_dsp);
    preset.set_data(&params, sizeof(params));
    
    dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
}

void sync_openal_spatial_settings_from_dsp() {
    auto dcm = dsp_config_manager::get();
    dsp_preset_impl preset;
    if (dcm->core_query_dsp(guid_openal_spatial_dsp, preset)) {
        openal_spatial_params_t params;
        parse_openal_preset(preset, params);
        
        g_spatial_override_preset.store(params.preset_index);
        g_spatial_override_rotation.store(params.rotation_speed);
        g_spatial_override_elevation.store(params.elevation);
        g_spatial_override_distance.store(params.distance);
        g_spatial_override_mix.store(params.mix);
        g_spatial_override_azimuth.store(params.azimuth);
        g_spatial_override_reverb.store(params.reverb_gain);
    }
}
