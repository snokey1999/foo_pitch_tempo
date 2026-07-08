#include "stdafx.h"
#include <atomic>
#include <thread>
#include <foobar2000/SDK/fileDialog.h>
#include <foobar2000/helpers/writer_wav.h>
#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <time.h>
#include <chrono>
#include "resource.h"

static double g_loop_start = -1.0;
static double g_loop_end = -1.0;

std::atomic<bool> g_is_seeking{false};
std::atomic<float> g_pitch_offset{0.0f};
std::atomic<float> g_tempo_offset{0.0f};
std::atomic<float> g_reverb_offset{0.0f};

static const GUID guid_cfg_fade_in = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x50 } };
static cfg_int cfg_fade_in(guid_cfg_fade_in, 500);

static const GUID guid_cfg_fade_out = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x51 } };
static cfg_int cfg_fade_out(guid_cfg_fade_out, 1000);

static const GUID guid_cfg_timer_mode = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x56 } };
static cfg_int cfg_timer_mode(guid_cfg_timer_mode, 1); // 0=15m, 1=30m, 2=60m, 3=Custom

static const GUID guid_cfg_timer_custom = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x57 } };
static cfg_int cfg_timer_custom(guid_cfg_timer_custom, 120);

const GUID guid_cfg_pitch_algo = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x58 } };
cfg_int cfg_pitch_algo(guid_cfg_pitch_algo, 2); // 0=极速 (Linear), 1=快速 (Quick Cubic), 2=标准 (Cubic), 3=极佳 (Shannon)

static std::atomic<bool> g_timer_running{false};
static std::atomic<time_t> g_target_quit_time{0};

class CDialogGlobalSettings : public CDialogImpl<CDialogGlobalSettings> {
public:
    enum { IDD = IDD_GLOBAL_SETTINGS };
    
    BEGIN_MSG_MAP(CDialogGlobalSettings)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnCloseCmd)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCloseCmd)
        COMMAND_HANDLER_EX(IDC_TIMER_MODE, CBN_SELCHANGE, OnModeChange)
        COMMAND_HANDLER_EX(IDC_TIMER_ENABLE, BN_CLICKED, OnTimerEnableChange)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam) {
        SetDlgItemInt(IDC_CFG_FADE_IN, cfg_fade_in.get_value(), FALSE);
        SetDlgItemInt(IDC_CFG_FADE_OUT, cfg_fade_out.get_value(), FALSE);
        
        bool enable = g_timer_running.load();
        CheckDlgButton(IDC_TIMER_ENABLE, enable ? BST_CHECKED : BST_UNCHECKED);
        
        CComboBox cb = GetDlgItem(IDC_TIMER_MODE);
        cb.AddString(L"15 分钟");
        cb.AddString(L"30 分钟");
        cb.AddString(L"60 分钟");
        cb.AddString(L"自定义...");
        cb.SetCurSel(cfg_timer_mode.get_value());
        
        SetDlgItemInt(IDC_TIMER_CUSTOM, cfg_timer_custom.get_value(), FALSE);
        GetDlgItem(IDC_TIMER_CUSTOM).EnableWindow(cfg_timer_mode.get_value() == 3);

        GetDlgItem(IDC_TIMER_MODE).ShowWindow(enable ? SW_SHOW : SW_HIDE);
        if (!enable) {
            GetDlgItem(IDC_TIMER_CUSTOM).ShowWindow(SW_HIDE);
        } else {
            GetDlgItem(IDC_TIMER_CUSTOM).ShowWindow((cfg_timer_mode.get_value() == 3) ? SW_SHOW : SW_HIDE);
        }
        
        CComboBox cbAlgo = GetDlgItem(IDC_PITCH_ALGO);
        cbAlgo.AddString(L"极速模式 (线性插值 - 极低 CPU 占用)");
        cbAlgo.AddString(L"快速模式 (快速三次插值 - 较低 CPU 占用)");
        cbAlgo.AddString(L"标准模式 (三次插值 - 普通品质)");
        cbAlgo.AddString(L"极佳模式 (香农插值 - 极高品质 / 高 CPU 占用)");
        cbAlgo.SetCurSel(cfg_pitch_algo.get_value());
        
        return TRUE;
    }
    
    void OnModeChange(UINT uNotifyCode, int nID, CWindow wndCtl) {
        CComboBox cb = GetDlgItem(IDC_TIMER_MODE);
        bool isCustom = (cb.GetCurSel() == 3);
        GetDlgItem(IDC_TIMER_CUSTOM).EnableWindow(isCustom);
        GetDlgItem(IDC_TIMER_CUSTOM).ShowWindow(isCustom ? SW_SHOW : SW_HIDE);
    }
    
    void OnTimerEnableChange(UINT uNotifyCode, int nID, CWindow wndCtl) {
        bool enable = (IsDlgButtonChecked(IDC_TIMER_ENABLE) == BST_CHECKED);
        GetDlgItem(IDC_TIMER_MODE).ShowWindow(enable ? SW_SHOW : SW_HIDE);
        if (!enable) {
            GetDlgItem(IDC_TIMER_CUSTOM).ShowWindow(SW_HIDE);
        } else {
            CComboBox cb = GetDlgItem(IDC_TIMER_MODE);
            bool isCustom = (cb.GetCurSel() == 3);
            GetDlgItem(IDC_TIMER_CUSTOM).ShowWindow(isCustom ? SW_SHOW : SW_HIDE);
        }
    }
    
    void OnCloseCmd(UINT uNotifyCode, int nID, CWindow wndCtl) {
        if (nID == IDOK) {
            cfg_fade_in = GetDlgItemInt(IDC_CFG_FADE_IN, NULL, FALSE);
            cfg_fade_out = GetDlgItemInt(IDC_CFG_FADE_OUT, NULL, FALSE);
            
            CComboBox cb = GetDlgItem(IDC_TIMER_MODE);
            cfg_timer_mode = cb.GetCurSel();
            cfg_timer_custom = GetDlgItemInt(IDC_TIMER_CUSTOM, NULL, FALSE);
            
            CComboBox cbAlgo = GetDlgItem(IDC_PITCH_ALGO);
            cfg_pitch_algo = cbAlgo.GetCurSel();
            
            bool enable = (IsDlgButtonChecked(IDC_TIMER_ENABLE) == BST_CHECKED);
            g_timer_running.store(enable);
            if (enable) {
                int mins = 0;
                if (cfg_timer_mode.get_value() == 0) mins = 15;
                else if (cfg_timer_mode.get_value() == 1) mins = 30;
                else if (cfg_timer_mode.get_value() == 2) mins = 60;
                else if (cfg_timer_mode.get_value() == 3) mins = cfg_timer_custom.get_value();
                
                g_target_quit_time.store(time(nullptr) + mins * 60);
                console::formatter() << u8"睡眠定时器已启动，foobar2000 将在 " << mins << u8" 分钟后退出";
            } else {
                g_target_quit_time.store(0);
                console::print(u8"睡眠定时器已关闭");
            }
        }
        EndDialog(nID);
    }
};

class initquit_timer : public initquit {
    std::atomic<bool> m_quit{false};
    std::thread m_thread;
public:
    void on_init() override {
        m_thread = std::thread([this]() {
            while (!m_quit.load()) {
                if (g_timer_running.load() && g_target_quit_time.load() > 0) {
                    if (time(nullptr) >= g_target_quit_time.load()) {
                        g_timer_running.store(false);
                        g_target_quit_time.store(0);
                        fb2k::inMainThread([] {
                            standard_commands::run_main(standard_commands::guid_main_exit);
                        });
                    }
                }
                for (int i = 0; i < 10 && !m_quit.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        });
    }
    void on_quit() override {
        m_quit = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
};
static initquit_factory_t<initquit_timer> g_initquit_timer_factory;

class CDialogExportFade : public CDialogImpl<CDialogExportFade> {
public:
    enum { IDD = IDD_EXPORT_FADE };
    
    int m_fade_in_ms = 0;
    int m_fade_out_ms = 0;
    
    BEGIN_MSG_MAP(CDialogExportFade)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnCloseCmd)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCloseCmd)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam) {
        SetDlgItemInt(IDC_FADE_IN, cfg_fade_in.get_value(), FALSE);
        SetDlgItemInt(IDC_FADE_OUT, cfg_fade_out.get_value(), FALSE);
        return TRUE;
    }
    
    void OnCloseCmd(UINT uNotifyCode, int nID, CWindow wndCtl) {
        if (nID == IDOK) {
            m_fade_in_ms = GetDlgItemInt(IDC_FADE_IN, NULL, FALSE);
            m_fade_out_ms = GetDlgItemInt(IDC_FADE_OUT, NULL, FALSE);
            cfg_fade_in = m_fade_in_ms;
            cfg_fade_out = m_fade_out_ms;
        }
        EndDialog(nID);
    }
};

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
        cmd_reverb_up,
        cmd_reverb_down,
        cmd_reverb_reset,
        cmd_export_loop,
        cmd_global_settings,
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
            case cmd_reverb_up: p_out = u8"混响 +5%"; break;
            case cmd_reverb_down: p_out = u8"混响 -5%"; break;
            case cmd_reverb_reset: p_out = u8"重置混响"; break;
            case cmd_export_loop: p_out = u8"导出当前循环片段为 WAV..."; break;
            case cmd_global_settings: p_out = u8"全局设置..."; break;
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
        static const GUID guid_reverb_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2D } };
        static const GUID guid_reverb_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2E } };
        static const GUID guid_reverb_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2F } };
        static const GUID guid_export_loop = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2C } };
        static const GUID guid_global_settings = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x55 } };
        
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
            case cmd_reverb_up: return guid_reverb_up;
            case cmd_reverb_down: return guid_reverb_down;
            case cmd_reverb_reset: return guid_reverb_reset;
            case cmd_export_loop: return guid_export_loop;
            case cmd_global_settings: return guid_global_settings;
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
            auto dcm = dsp_config_manager::get();
            dsp_preset_impl preset;
            static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };
            if (dcm->core_query_dsp(guid_bass_dsp, preset)) {
                float tempo = 0.0f;
                if (preset.get_data_size() == sizeof(float) * 2) tempo = ((const float*)preset.get_data())[1];
                float d[2] = { 0.0f, tempo };
                preset.set_data(&d, sizeof(d));
                dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
            }
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
            auto dcm = dsp_config_manager::get();
            dsp_preset_impl preset;
            static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };
            if (dcm->core_query_dsp(guid_bass_dsp, preset)) {
                float pitch = 0.0f;
                if (preset.get_data_size() == sizeof(float) * 2) pitch = ((const float*)preset.get_data())[0];
                float d[2] = { pitch, 0.0f };
                preset.set_data(&d, sizeof(d));
                dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
            }
            g_tempo_offset.store(0.0f);
            console::print(u8"已重置速度偏移");
        } else if (p_index == cmd_reverb_up) {
            ensure_dsp_active();
            float v = g_reverb_offset.load() + 5.0f;
            g_reverb_offset.store(v);
            console::formatter() << u8"混响偏移: " << v << "%";
        } else if (p_index == cmd_reverb_down) {
            ensure_dsp_active();
            float v = g_reverb_offset.load() - 5.0f;
            g_reverb_offset.store(v);
            console::formatter() << u8"混响偏移: " << v << "%";
        } else if (p_index == cmd_reverb_reset) {
            ensure_dsp_active();
            auto dcm = dsp_config_manager::get();
            dsp_preset_impl preset;
            static const GUID guid_reverb_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x30 } };
            if (dcm->core_query_dsp(guid_reverb_dsp, preset)) {
                float d[1] = { 30.0f };
                preset.set_data(&d, sizeof(d));
                dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
            }
            g_reverb_offset.store(0.0f);
            console::print(u8"已重置混响偏移");
        } else if (p_index == cmd_global_settings) {
            CDialogGlobalSettings dlg;
            dlg.DoModal(core_api::get_main_window());
        } else if (p_index == cmd_export_loop) {
            metadb_handle_ptr handle;
            if (!pc->get_now_playing(handle)) {
                console::print(u8"没有正在播放的音轨");
                return;
            }
            double start = g_loop_start;
            double end = g_loop_end;
            if (start < 0.0) start = 0.0;
            if (end < 0.0) end = handle->get_length();
            if (end <= start) {
                console::print(u8"无效的循环区间");
                return;
            }
            
            CDialogExportFade dlg;
            if (dlg.DoModal(core_api::get_main_window()) != IDOK) {
                return;
            }
            
            double fade_in_sec = dlg.m_fade_in_ms / 1000.0;
            double fade_out_sec = dlg.m_fade_out_ms / 1000.0;
            
            auto export_func = [handle, start, end, fade_in_sec, fade_out_sec](const char * savePath_in) {
                pfc::string8 savePath = savePath_in;
                std::thread([handle, start, end, fade_in_sec, fade_out_sec, savePath]() {
                    try {
                        abort_callback_dummy abort;
                        service_ptr_t<input_decoder> dec;
                        input_entry::g_open_for_decoding(dec, nullptr, handle->get_path(), abort);
                        
                        dec->initialize(handle->get_subsong_index(), input_flag_no_looping | input_flag_no_seeking, abort);
                        
                        dsp_preset_impl preset;
                        static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };
                        float d[2] = { 0.0f, 0.0f };
                        preset.set_owner(guid_bass_dsp);
                        preset.set_data(&d, sizeof(d));
                        
                        dsp::ptr my_dsp;
                        if (!dsp_entry::g_instantiate(my_dsp, preset)) {
                            throw pfc::exception("无法实例化 DSP");
                        }
                        
                        CWavWriter writer;
                        
                        if (start > 0.0 && dec->can_seek()) {
                            dec->seek(start, abort);
                        }
                        
                        double current_time = start;
                        dsp_chunk_list_impl chunk_list;
                        
                        std::vector<audio_sample> full_audio;
                        unsigned out_channels = 0;
                        unsigned out_sample_rate = 0;
                        
                        audio_chunk_impl chunk;
                        while (dec->run(chunk, abort)) {
                            double chunk_duration = (double)chunk.get_sample_count() / chunk.get_sample_rate();
                            
                            if (current_time >= end) break;
                            
                            if (current_time + chunk_duration > end) {
                                t_size keep_samples = (t_size)((end - current_time) * chunk.get_sample_rate());
                                if (keep_samples < chunk.get_sample_count()) {
                                    chunk.set_sample_count(keep_samples);
                                }
                            }
                            
                            current_time += chunk_duration;
                            
                            chunk_list.add_chunk(&chunk);
                            my_dsp->run_abortable(&chunk_list, nullptr, 0, abort);
                            
                            for (t_size i = 0; i < chunk_list.get_count(); ++i) {
                                audio_chunk * out_chunk = chunk_list.get_item(i);
                                if (!out_chunk->is_empty()) {
                                    if (out_channels == 0) out_channels = out_chunk->get_channels();
                                    if (out_sample_rate == 0) out_sample_rate = out_chunk->get_sample_rate();
                                    
                                    t_size count = out_chunk->get_sample_count();
                                    const audio_sample * data = out_chunk->get_data();
                                    full_audio.insert(full_audio.end(), data, data + count * out_channels);
                                }
                            }
                            chunk_list.remove_all();
                            current_time += chunk_duration;
                            if (current_time >= end) break;
                        }

                        
                        // Flush DSP tail (e.g. Reverb)
                        my_dsp->run_abortable(&chunk_list, nullptr, dsp::END_OF_TRACK | dsp::FLUSH, abort);
                        for (t_size i = 0; i < chunk_list.get_count(); ++i) {
                            audio_chunk * out_chunk = chunk_list.get_item(i);
                            if (!out_chunk->is_empty()) {
                                if (out_channels == 0) out_channels = out_chunk->get_channels();
                                if (out_sample_rate == 0) out_sample_rate = out_chunk->get_sample_rate();
                                
                                t_size count = out_chunk->get_sample_count();
                                const audio_sample * data = out_chunk->get_data();
                                full_audio.insert(full_audio.end(), data, data + count * out_channels);
                            }
                        }
                        chunk_list.remove_all();
                        
                        // Apply accurate Fade In/Out
                        if (!full_audio.empty() && out_channels > 0) {
                            t_size total_samples = full_audio.size() / out_channels;
                            t_size fade_in_samples = (t_size)(fade_in_sec * out_sample_rate);
                            t_size fade_out_samples = (t_size)(fade_out_sec * out_sample_rate);
                            
                            for (t_size s = 0; s < total_samples; ++s) {
                                float vol = 1.0f;
                                
                                if (s < fade_in_samples && fade_in_samples > 0) {
                                    float ratio = (float)s / fade_in_samples;
                                    vol = ratio * ratio * ratio; // Cubic fade-in (smooth start)
                                }
                                
                                t_size samples_from_end = total_samples - 1 - s;
                                if (samples_from_end < fade_out_samples && fade_out_samples > 0) {
                                    float ratio = (float)samples_from_end / fade_out_samples;
                                    float out_vol = ratio * ratio; // Square fade-out
                                    if (out_vol < vol) vol = out_vol;
                                }
                                
                                for (unsigned c = 0; c < out_channels; ++c) {
                                    full_audio[s * out_channels + c] *= vol;
                                }
                            }
                            
                            audio_chunk_impl final_chunk;
                            final_chunk.set_data(full_audio.data(), total_samples, out_channels, out_sample_rate);
                            
                            wavWriterSetup_t setup;
                            setup.initialize(final_chunk, 16, false, true);
                            writer.open(savePath.c_str(), setup, abort);
                            writer.write(final_chunk, abort);
                            writer.finalize(abort);
                            
                            fb2k::inMainThread([]() {
                                console::print(u8"片段导出完成");
                            });
                        } else {
                            fb2k::inMainThread([]() {
                                console::print(u8"片段导出失败: 没有数据");
                            });
                        }
                        
                    } catch (std::exception & e) {
                        pfc::string8 msg = e.what();
                        fb2k::inMainThread([msg]() {
                            console::formatter() << u8"导出失败: " << msg;
                        });
                    }
                }).detach();
            };
            
            pfc::string8 initialName;
            handle->format_title(nullptr, initialName, titleformat_compiler::get()->compile_force("%title% - loop"), nullptr);
            initialName += ".wav";

            auto fd_api = fb2k::fileDialog::tryGet();
            if (fd_api.is_valid()) {
                auto fd = fd_api->setupSave();
                fd->setTitle("导出循环片段为 WAV");
                fd->setFileTypes("WAV Audio Files|*.wav");
                fd->setDefaultExtension("wav");
                fd->setInitialValue(initialName);
                fd->runSimple([export_func](fb2k::stringRef path) {
                    export_func(path->c_str());
                });
            } else {
                pfc::string8 path = initialName;
                if (uGetOpenFileName(core_api::get_main_window(), "WAV Audio Files|*.wav", 0, "wav", "导出循环片段为 WAV", nullptr, path, TRUE)) {
                    export_func(path.c_str());
                }
            }
        }
    }
};

static mainmenu_commands_factory_t<loop_shortcuts> g_loop_shortcuts_factory;
