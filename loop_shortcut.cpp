#include "stdafx.h"
#include <atomic>

static double g_loop_start = -1.0;
static double g_loop_end = -1.0;

std::atomic<bool> g_is_seeking{false};
std::atomic<float> g_pitch_offset{0.0f};
std::atomic<float> g_tempo_offset{0.0f};

class loop_callback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_time | flag_on_playback_stop | flag_on_playback_seek;
    }
    
    void on_playback_time(double p_time) override {
        if (g_loop_start >= 0.0 && g_loop_end >= 0.0 && g_loop_end > g_loop_start) {
            if (p_time >= g_loop_end) {
                bool expected = false;
                if (g_is_seeking.compare_exchange_strong(expected, true)) {
                    double seek_to = g_loop_start;
                    fb2k::inMainThread([seek_to] {
                        playback_control::get()->playback_seek(seek_to);
                    });
                }
            }
        }
    }
    
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {}
    void on_playback_new_track(metadb_handle_ptr p_track) override {}
    void on_playback_stop(play_control::t_stop_reason p_reason) override {
        g_is_seeking = false;
    }
    void on_playback_seek(double p_time) override {
        g_is_seeking = false;
    }
    void on_playback_pause(bool p_state) override {}
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info(const file_info & p_info) override {}
    void on_playback_dynamic_info_track(const file_info & p_info) override {}
    void on_volume_change(float p_new_val) override {}
};

static play_callback_static_factory_t<loop_callback> g_loop_callback_factory;

static const GUID guid_loop_group = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x25 } };
static mainmenu_group_popup_factory g_loop_group(guid_loop_group, mainmenu_groups::playback, mainmenu_commands::sort_priority_dontcare, u8"变速变调与循环");

class loop_shortcuts : public mainmenu_commands {
public:
    enum {
        cmd_set_start = 0,
        cmd_set_end,
        cmd_clear,
        cmd_pitch_up,
        cmd_pitch_down,
        cmd_pitch_reset,
        cmd_tempo_up,
        cmd_tempo_down,
        cmd_tempo_reset,
        cmd_total
    };
    
    t_uint32 get_command_count() override { return cmd_total; }
    
    void get_name(t_uint32 p_index, pfc::string_base & p_out) override {
        switch (p_index) {
            case cmd_set_start: p_out = u8"设置循环起点 A"; break;
            case cmd_set_end: p_out = u8"设置循环终点 B"; break;
            case cmd_clear: p_out = u8"清除循环"; break;
            case cmd_pitch_up: p_out = u8"音高 +1 半音"; break;
            case cmd_pitch_down: p_out = u8"音高 -1 半音"; break;
            case cmd_pitch_reset: p_out = u8"重置音高"; break;
            case cmd_tempo_up: p_out = u8"速度 +5%"; break;
            case cmd_tempo_down: p_out = u8"速度 -5%"; break;
            case cmd_tempo_reset: p_out = u8"重置速度"; break;
        }
    }
    
    bool get_description(t_uint32 p_index, pfc::string_base & p_out) override {
        return true;
    }
    
    GUID get_command(t_uint32 p_index) override {
        static const GUID guid_set_start = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x20 } };
        static const GUID guid_set_end = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x21 } };
        static const GUID guid_clear = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x22 } };
        static const GUID guid_pitch_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x26 } };
        static const GUID guid_pitch_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x27 } };
        static const GUID guid_pitch_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x28 } };
        static const GUID guid_tempo_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x29 } };
        static const GUID guid_tempo_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2A } };
        static const GUID guid_tempo_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2B } };
        
        switch (p_index) {
            case cmd_set_start: return guid_set_start;
            case cmd_set_end: return guid_set_end;
            case cmd_clear: return guid_clear;
            case cmd_pitch_up: return guid_pitch_up;
            case cmd_pitch_down: return guid_pitch_down;
            case cmd_pitch_reset: return guid_pitch_reset;
            case cmd_tempo_up: return guid_tempo_up;
            case cmd_tempo_down: return guid_tempo_down;
            case cmd_tempo_reset: return guid_tempo_reset;
        }
        return pfc::guid_null;
    }
    
    GUID get_parent() override { return guid_loop_group; }
    
    void ensure_dsp_active() {
        auto dcm = dsp_config_manager::get();
        dsp_preset_impl preset;
        static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };
        if (!dcm->core_query_dsp(guid_bass_dsp, preset)) {
            float d[2] = { 0.0f, 0.0f };
            preset.set_owner(guid_bass_dsp);
            preset.set_data(&d, sizeof(d));
            dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_first);
        }
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        auto pc = playback_control::get();
        if (p_index == cmd_set_start || p_index == cmd_set_end) {
            if (!pc->is_playing()) return;
            double time = pc->playback_get_position();
            if (p_index == cmd_set_start) {
                g_loop_start = time;
                console::formatter() << u8"循环起点设为 " << pfc::format_time_ex(time);
            } else {
                g_loop_end = time;
                console::formatter() << u8"循环终点设为 " << pfc::format_time_ex(time);
            }
        } else if (p_index == cmd_clear) {
            g_loop_start = -1.0;
            g_loop_end = -1.0;
            console::print(u8"已清除循环");
        } else if (p_index == cmd_pitch_up) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() + 1.0f;
            g_pitch_offset.store(v);
            console::formatter() << u8"音高偏移: " << v;
        } else if (p_index == cmd_pitch_down) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() - 1.0f;
            g_pitch_offset.store(v);
            console::formatter() << u8"音高偏移: " << v;
        } else if (p_index == cmd_pitch_reset) {
            ensure_dsp_active();
            g_pitch_offset.store(0.0f);
            console::print(u8"已重置音高偏移");
        } else if (p_index == cmd_tempo_up) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() + 5.0f;
            g_tempo_offset.store(v);
            console::formatter() << u8"速度偏移: " << v << "%";
        } else if (p_index == cmd_tempo_down) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() - 5.0f;
            g_tempo_offset.store(v);
            console::formatter() << u8"速度偏移: " << v << "%";
        } else if (p_index == cmd_tempo_reset) {
            ensure_dsp_active();
            g_tempo_offset.store(0.0f);
            console::print(u8"已重置速度偏移");
        }
    }
};

static mainmenu_commands_factory_t<loop_shortcuts> g_loop_shortcuts_factory;
