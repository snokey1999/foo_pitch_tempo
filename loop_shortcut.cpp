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
#include "tolk_wrapper.h"

static void SpeakMsg(const char* utf8_text) {
    pfc::stringcvt::string_wide_from_utf8 wstr(utf8_text);
    TolkWrapper_Output(std::wstring(wstr.get_ptr()));
}


static double g_loop_start = -1.0;
static double g_loop_end = -1.0;

std::atomic<bool> g_is_seeking{false};
std::atomic<float> g_pitch_offset{0.0f};
std::atomic<float> g_tempo_offset{0.0f};
std::atomic<float> g_reverb_offset{0.0f};

// --- Per-track Settings DB ---
struct track_settings_t {
    float pitch_offset = 0.0f;
    float tempo_offset = 0.0f;
    double loop_start = -1.0;
    double loop_end = -1.0;
};

static const GUID guid_pitch_tempo_index = { 0x3d7b89f2, 0x1b2c, 0x4a9e, { 0x92, 0x11, 0x44, 0x82, 0x7a, 0xb8, 0xc1, 0x4f } };
static const char strSettingsPinTo[] = "%path%|%subsong%";

class metadb_index_client_impl : public metadb_index_client {
public:
    metadb_index_client_impl(const char* pinTo) {
        static_api_ptr_t<titleformat_compiler>()->compile_force(m_keyObj, pinTo);
    }
    metadb_index_hash transform(const file_info & info, const playable_location & location) override {
        pfc::string_formatter str;
        m_keyObj->run_simple(location, &info, str);
        return static_api_ptr_t<hasher_md5>()->process_single_string(str).xorHalve();
    }
private:
    titleformat_object::ptr m_keyObj;
};

static metadb_index_client_impl* get_settings_client() {
    static metadb_index_client_impl* g_client = new service_impl_single_t<metadb_index_client_impl>(strSettingsPinTo);
    return g_client;
}

static metadb_index_manager::ptr get_index_manager() {
    static metadb_index_manager* cached = metadb_index_manager::get().detach();
    return cached;
}

class init_stage_callback_settings : public init_stage_callback {
public:
    void on_init_stage(t_uint32 stage) override {
        if (stage == init_stages::before_config_read) {
            auto api = get_index_manager();
            try {
                api->add(get_settings_client(), guid_pitch_tempo_index, system_time_periods::week * 52); // Retain for 1 year
            } catch (std::exception const & e) {
                api->remove(guid_pitch_tempo_index);
                {
                pfc::string_formatter _msg;
                _msg << "Failed to init pitch/tempo index: " << e.what();
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
                return;
            }
            api->dispatch_global_refresh();
        }
    }
};
static service_factory_single_t<init_stage_callback_settings> g_init_stage_settings;

static track_settings_t settings_get(metadb_index_hash hash) {
    track_settings_t ret;
    mem_block_container_impl temp;
    get_index_manager()->get_user_data(guid_pitch_tempo_index, hash, temp);
    if (temp.get_size() == sizeof(track_settings_t)) {
        memcpy(&ret, temp.get_ptr(), sizeof(track_settings_t));
    }
    return ret;
}

static void settings_set(metadb_index_hash hash, const track_settings_t & record) {
    get_index_manager()->set_user_data(guid_pitch_tempo_index, hash, &record, sizeof(track_settings_t));
}

static void save_current_track_settings() {
    auto pc = playback_control::get();
    metadb_handle_ptr handle;
    if (pc->get_now_playing(handle)) {
        metadb_index_hash hash;
        if (get_settings_client()->hashHandle(handle, hash)) {
            track_settings_t rec;
            rec.pitch_offset = g_pitch_offset.load();
            rec.tempo_offset = g_tempo_offset.load();
            rec.loop_start = g_loop_start;
            rec.loop_end = g_loop_end;
            settings_set(hash, rec);
        }
    }
}
// -----------------------------

static const GUID guid_cfg_fade_in = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x50 } };
static cfg_int cfg_fade_in(guid_cfg_fade_in, 500);

static const GUID guid_cfg_export_format = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x59 } };
static cfg_int cfg_export_format(guid_cfg_export_format, 0); // 0=WAV, 1=MP3, 2=FLAC, 3=MP4

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
        
        CComboBox cbFmt = GetDlgItem(IDC_EXPORT_FORMAT);
        cbFmt.AddString(L"WAV (无损)");
        cbFmt.AddString(L"MP3 (有损音频)");
        cbFmt.AddString(L"FLAC (无损音频)");
        cbFmt.AddString(L"MP4 (带图片视频)");
        cbFmt.SetCurSel(cfg_export_format.get_value());
        
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
            
            CComboBox cbFmt = GetDlgItem(IDC_EXPORT_FORMAT);
            cfg_export_format = cbFmt.GetCurSel();
            
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
                {
                pfc::string_formatter _msg;
                _msg << u8"睡眠定时器已启动，foobar2000 将在 " << mins << u8" 分钟后退出";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
            } else {
                g_target_quit_time.store(0);
                {
                SpeakMsg(u8"睡眠定时器已关闭");
                console::print(u8"睡眠定时器已关闭");
            }
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
        return flag_on_playback_time | flag_on_playback_stop | flag_on_playback_seek | flag_on_playback_new_track;
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
    void on_playback_new_track(metadb_handle_ptr p_track) override {
        metadb_index_hash hash;
        if (get_settings_client()->hashHandle(p_track, hash)) {
            track_settings_t rec = settings_get(hash);
            g_pitch_offset.store(rec.pitch_offset);
            g_tempo_offset.store(rec.tempo_offset);
            g_loop_start = rec.loop_start;
            g_loop_end = rec.loop_end;

            auto dcm = dsp_config_manager::get();
            dsp_preset_impl preset;
            static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };
            if (!dcm->core_query_dsp(guid_bass_dsp, preset) && (rec.pitch_offset != 0.0f || rec.tempo_offset != 0.0f)) {
                float d[2] = { 0.0f, 0.0f };
                preset.set_owner(guid_bass_dsp);
                preset.set_data(&d, sizeof(d));
                dcm->core_enable_dsp(preset, dsp_config_manager::default_insert_first);
            }
        }
    }
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
        cmd_loop_start_forward,
        cmd_loop_start_backward,
        cmd_loop_end_forward,
        cmd_loop_end_backward,
        cmd_pitch_up,
        cmd_pitch_down,
        cmd_pitch_up_fine,
        cmd_pitch_down_fine,
        cmd_pitch_reset,
        cmd_tempo_up,
        cmd_tempo_down,
        cmd_tempo_up_fine,
        cmd_tempo_down_fine,
        cmd_tempo_reset,
        cmd_reverb_up,
        cmd_reverb_down,
        cmd_reverb_reset,
        cmd_export_loop,
        cmd_global_settings,
        cmd_read_loop_duration,
        cmd_total
    };
    
    t_uint32 get_command_count() override { return cmd_total; }
    
    void get_name(t_uint32 p_index, pfc::string_base & p_out) override {
        switch (p_index) {
            case cmd_set_start: p_out = u8"设置循环起点 A"; break;
            case cmd_set_end: p_out = u8"设置循环终点 B"; break;
            case cmd_clear: p_out = u8"清除循环"; break;
            case cmd_loop_start_forward: p_out = u8"循环起点 A 延后 100ms"; break;
            case cmd_loop_start_backward: p_out = u8"循环起点 A 提前 100ms"; break;
            case cmd_loop_end_forward: p_out = u8"循环终点 B 延后 100ms"; break;
            case cmd_loop_end_backward: p_out = u8"循环终点 B 提前 100ms"; break;
            case cmd_pitch_up: p_out = u8"音高 +1 半音"; break;
            case cmd_pitch_down: p_out = u8"音高 -1 半音"; break;
            case cmd_pitch_up_fine: p_out = u8"音高微调 +0.1 半音"; break;
            case cmd_pitch_down_fine: p_out = u8"音高微调 -0.1 半音"; break;
            case cmd_pitch_reset: p_out = u8"重置音高"; break;
            case cmd_tempo_up: p_out = u8"速度 +5%"; break;
            case cmd_tempo_down: p_out = u8"速度 -5%"; break;
            case cmd_tempo_up_fine: p_out = u8"速度微调 +1%"; break;
            case cmd_tempo_down_fine: p_out = u8"速度微调 -1%"; break;
            case cmd_tempo_reset: p_out = u8"重置速度"; break;
            case cmd_reverb_up: p_out = u8"混响 +5%"; break;
            case cmd_reverb_down: p_out = u8"混响 -5%"; break;
            case cmd_reverb_reset: p_out = u8"重置混响"; break;
            case cmd_export_loop: p_out = u8"导出当前循环片段为 WAV..."; break;
            case cmd_global_settings: p_out = u8"全局设置..."; break;
            case cmd_read_loop_duration: p_out = u8"播报当前循环时长"; break;
        }
    }
    
    bool get_description(t_uint32 p_index, pfc::string_base & p_out) override {
        return true;
    }
    
    GUID get_command(t_uint32 p_index) override {
        static const GUID guid_set_start = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x20 } };
        static const GUID guid_set_end = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x21 } };
        static const GUID guid_clear = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x22 } };
        static const GUID guid_loop_start_forward = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x70 } };
        static const GUID guid_loop_start_backward = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x71 } };
        static const GUID guid_loop_end_forward = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x72 } };
        static const GUID guid_loop_end_backward = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x73 } };
        static const GUID guid_pitch_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x26 } };
        static const GUID guid_pitch_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x27 } };
        static const GUID guid_pitch_up_fine = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x74 } };
        static const GUID guid_pitch_down_fine = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x75 } };
        static const GUID guid_pitch_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x28 } };
        static const GUID guid_tempo_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x29 } };
        static const GUID guid_tempo_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2A } };
        static const GUID guid_tempo_up_fine = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x76 } };
        static const GUID guid_tempo_down_fine = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x77 } };
        static const GUID guid_tempo_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2B } };
        static const GUID guid_reverb_up = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2D } };
        static const GUID guid_reverb_down = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2E } };
        static const GUID guid_reverb_reset = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2F } };
        static const GUID guid_export_loop = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x2C } };
        static const GUID guid_global_settings = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x55 } };
        static const GUID guid_read_loop_duration = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x78 } };
        
        switch (p_index) {
            case cmd_set_start: return guid_set_start;
            case cmd_set_end: return guid_set_end;
            case cmd_clear: return guid_clear;
            case cmd_loop_start_forward: return guid_loop_start_forward;
            case cmd_loop_start_backward: return guid_loop_start_backward;
            case cmd_loop_end_forward: return guid_loop_end_forward;
            case cmd_loop_end_backward: return guid_loop_end_backward;
            case cmd_pitch_up: return guid_pitch_up;
            case cmd_pitch_down: return guid_pitch_down;
            case cmd_pitch_up_fine: return guid_pitch_up_fine;
            case cmd_pitch_down_fine: return guid_pitch_down_fine;
            case cmd_pitch_reset: return guid_pitch_reset;
            case cmd_tempo_up: return guid_tempo_up;
            case cmd_tempo_down: return guid_tempo_down;
            case cmd_tempo_up_fine: return guid_tempo_up_fine;
            case cmd_tempo_down_fine: return guid_tempo_down_fine;
            case cmd_tempo_reset: return guid_tempo_reset;
            case cmd_reverb_up: return guid_reverb_up;
            case cmd_reverb_down: return guid_reverb_down;
            case cmd_reverb_reset: return guid_reverb_reset;
            case cmd_export_loop: return guid_export_loop;
            case cmd_global_settings: return guid_global_settings;
            case cmd_read_loop_duration: return guid_read_loop_duration;
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
                {
                pfc::string_formatter _msg;
                _msg << u8"循环起点设为 " << pfc::format_time_ex(time);
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
            } else {
                g_loop_end = time;
                {
                pfc::string_formatter _msg;
                _msg << u8"循环终点设为 " << pfc::format_time_ex(time);
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
            }
            save_current_track_settings();
        } else if (p_index == cmd_loop_start_forward) {
            if (g_loop_start >= 0.0) { g_loop_start += 0.1; save_current_track_settings(); {
                SpeakMsg(u8"起点延后");
                console::print(u8"起点延后");
            } }
        } else if (p_index == cmd_loop_start_backward) {
            if (g_loop_start >= 0.1) { g_loop_start -= 0.1; save_current_track_settings(); {
                SpeakMsg(u8"起点提前");
                console::print(u8"起点提前");
            } }
        } else if (p_index == cmd_loop_end_forward) {
            if (g_loop_end >= 0.0) { g_loop_end += 0.1; save_current_track_settings(); {
                SpeakMsg(u8"终点延后");
                console::print(u8"终点延后");
            } }
        } else if (p_index == cmd_loop_end_backward) {
            if (g_loop_end >= 0.1) { g_loop_end -= 0.1; save_current_track_settings(); {
                SpeakMsg(u8"终点提前");
                console::print(u8"终点提前");
            } }
        } else if (p_index == cmd_clear) {
            g_loop_start = -1.0;
            g_loop_end = -1.0;
            save_current_track_settings();
            {
                SpeakMsg(u8"已清除循环");
                console::print(u8"已清除循环");
            }
        } else if (p_index == cmd_pitch_up) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() + 1.0f;
            g_pitch_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(v, 0, 2) << u8" 半音";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_pitch_down) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() - 1.0f;
            g_pitch_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(v, 0, 2) << u8" 半音";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_pitch_up_fine) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() + 0.1f;
            g_pitch_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                if (v > 0) {
                    if (fmod(std::abs(v), 2.0f) == 0.0f) _msg << u8"升高" << pfc::format_float(v / 2.0f, 0, 2) << u8"全音";
                    else _msg << u8"升高" << pfc::format_float(v, 0, 2) << u8"半音";
                } else if (v < 0) {
                    if (fmod(std::abs(v), 2.0f) == 0.0f) _msg << u8"降低" << pfc::format_float(std::abs(v) / 2.0f, 0, 2) << u8"全音";
                    else _msg << u8"降低" << pfc::format_float(std::abs(v), 0, 2) << u8"半音";
                } else {
                    _msg << u8"原调";
                }
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_pitch_down_fine) {
            ensure_dsp_active();
            float v = g_pitch_offset.load() - 0.1f;
            g_pitch_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(v, 0, 2) << u8" 半音";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
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
            save_current_track_settings();
            {
                SpeakMsg(u8"0 半音");
                console::print(u8"0 半音");
            }
        } else if (p_index == cmd_tempo_up) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() + 5.0f;
            g_tempo_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(1.0f + v / 100.0f, 0, 6) << u8" 对象速率";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_tempo_down) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() - 5.0f;
            g_tempo_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(1.0f + v / 100.0f, 0, 6) << u8" 对象速率";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_tempo_up_fine) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() + 1.0f;
            g_tempo_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(1.0f + v / 100.0f, 0, 6) << u8" 对象速率";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_tempo_down_fine) {
            ensure_dsp_active();
            float v = g_tempo_offset.load() - 1.0f;
            g_tempo_offset.store(v);
            save_current_track_settings();
            {
                pfc::string_formatter _msg;
                _msg << pfc::format_float(1.0f + v / 100.0f, 0, 6) << u8" 对象速率";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
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
            save_current_track_settings();
            {
                SpeakMsg(u8"1 对象速率");
                console::print(u8"1 对象速率");
            }
        } else if (p_index == cmd_reverb_up) {
            ensure_dsp_active();
            float v = g_reverb_offset.load() + 5.0f;
            g_reverb_offset.store(v);
            {
                pfc::string_formatter _msg;
                _msg << u8"混响" << pfc::format_float(30.0f + v, 0, 1) << "%";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
        } else if (p_index == cmd_reverb_down) {
            ensure_dsp_active();
            float v = g_reverb_offset.load() - 5.0f;
            g_reverb_offset.store(v);
            {
                pfc::string_formatter _msg;
                _msg << u8"混响" << pfc::format_float(30.0f + v, 0, 1) << "%";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            }
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
            {
                SpeakMsg(u8"已重置混响偏移");
                console::print(u8"已重置混响偏移");
            }
        } else if (p_index == cmd_global_settings) {
            CDialogGlobalSettings dlg;
            dlg.DoModal(core_api::get_main_window());
        } else if (p_index == cmd_read_loop_duration) {
            if (g_loop_start >= 0.0 && g_loop_end >= 0.0 && g_loop_end > g_loop_start) {
                pfc::string_formatter _msg;
                _msg << u8"循环时长 " << pfc::format_float(g_loop_end - g_loop_start, 0, 2) << u8" 秒";
                SpeakMsg(_msg.get_ptr());
                console::print(_msg.get_ptr());
            } else {
                SpeakMsg(u8"当前未设置循环");
                console::print(u8"当前未设置循环");
            }
        } else if (p_index == cmd_export_loop) {
            metadb_handle_ptr handle;
            if (!pc->get_now_playing(handle)) {
                {
                SpeakMsg(u8"没有正在播放的音轨");
                console::print(u8"没有正在播放的音轨");
            }
                return;
            }
            double start = g_loop_start;
            double end = g_loop_end;
            if (start < 0.0) start = 0.0;
            if (end < 0.0) end = handle->get_length();
            if (end <= start) {
                {
                SpeakMsg(u8"无效的循环区间");
                console::print(u8"无效的循环区间");
            }
                return;
            }
            
            double fade_in_sec = cfg_fade_in.get_value() / 1000.0;
            double fade_out_sec = cfg_fade_out.get_value() / 1000.0;
            
            int export_fmt = cfg_export_format.get_value();
            
            auto fd_api = fb2k::fileDialog::tryGet();
            if (!fd_api.is_valid()) return;

            auto export_func = [handle, start, end, fade_in_sec, fade_out_sec, export_fmt](const char * savePath_in, const char * image_path_in) {
                pfc::string8 savePath = savePath_in;
                pfc::string8 image_path = image_path_in ? image_path_in : "";
                auto tpc = threaded_process_callback_lambda::create(
                    [handle, start, end, fade_in_sec, fade_out_sec, savePath, export_fmt, image_path](threaded_process_status & status, abort_callback & abort) {
                        try {
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
                        
                        double duration = end - start;
                        if (duration <= 0) duration = 1.0;
                        status.set_item("正在解码音频...");
                        
                        // 修正1：增加微小的误差容忍量（0.001秒），防止浮点数精度截断导致最后一块进不来
                        while (current_time < (end - 0.001) && dec->run(chunk, abort)) {
                            status.set_progress_float((current_time - start) / duration * 0.5f);
                            double chunk_duration = (double)chunk.get_sample_count() / chunk.get_sample_rate();
                            
                            if (current_time + chunk_duration > end) {
                                t_size keep_samples = (t_size)((end - current_time) * chunk.get_sample_rate());
                                if (keep_samples < chunk.get_sample_count()) {
                                    chunk.set_sample_count(keep_samples);
                                    chunk_duration = (double)keep_samples / chunk.get_sample_rate();
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
                        }

                        // 修正2：彻底移除冲突的 dsp::FLUSH 标志，仅保留 dsp::END_OF_TRACK
                        // 这样 DSP 才会完全交出最后的缓冲数据
                        my_dsp->run_abortable(&chunk_list, nullptr, dsp::END_OF_TRACK, abort);
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
                            
                            pfc::string8 temp_wav = savePath;
                            if (export_fmt != 0) {
                                temp_wav += ".tmp.wav";
                            }
                            
                            writer.open(temp_wav.c_str(), setup, abort);
                            writer.write(final_chunk, abort);
                            writer.finalize(abort);
                            writer.close(); // MUST close before FFmpeg accesses it
                            
                            if (export_fmt != 0) {
                                status.set_progress_float(1.0);
                                status.set_item("正在压制中(FFmpeg)...");
                                pfc::string8 cmd;
                                
                                pfc::string8 ffmpeg_temp_wav = temp_wav;
                                if (strncmp(ffmpeg_temp_wav.get_ptr(), "file://", 7) == 0) ffmpeg_temp_wav = ffmpeg_temp_wav.get_ptr() + 7;
                                
                                pfc::string8 ffmpeg_savePath = savePath;
                                if (strncmp(ffmpeg_savePath.get_ptr(), "file://", 7) == 0) ffmpeg_savePath = ffmpeg_savePath.get_ptr() + 7;
                                
                                pfc::string8 ffmpeg_image = image_path;
                                if (strncmp(ffmpeg_image.get_ptr(), "file://", 7) == 0) ffmpeg_image = ffmpeg_image.get_ptr() + 7;
                                
                                if (export_fmt == 1) {
                                    cmd = pfc::string_formatter() << "ffmpeg -y -i \"" << ffmpeg_temp_wav << "\" -b:a 320k \"" << ffmpeg_savePath << "\"";
                                } else if (export_fmt == 2) {
                                    cmd = pfc::string_formatter() << "ffmpeg -y -i \"" << ffmpeg_temp_wav << "\" -c:a flac \"" << ffmpeg_savePath << "\"";
                                } else if (export_fmt == 3) {
                                    if (ffmpeg_image.is_empty()) {
                                        cmd = pfc::string_formatter() << "ffmpeg -y -f lavfi -i color=c=black:s=1280x720:r=1 -i \"" << ffmpeg_temp_wav << "\" -c:v libx264 -tune stillimage -c:a aac -b:a 256k -pix_fmt yuv420p -shortest \"" << ffmpeg_savePath << "\"";
                                    } else {
                                        cmd = pfc::string_formatter() << "ffmpeg -y -loop 1 -framerate 1 -i \"" << ffmpeg_image << "\" -i \"" << ffmpeg_temp_wav << "\" -c:v libx264 -tune stillimage -vf \"scale=trunc(iw/2)*2:trunc(ih/2)*2\" -c:a aac -b:a 256k -pix_fmt yuv420p -shortest \"" << ffmpeg_savePath << "\"";
                                    }
                                }
                                
                                HANDLE hRead, hWrite;
                                SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
                                if (!CreatePipe(&hRead, &hWrite, &sa, 0)) throw pfc::exception("创建输出管道失败。");
                                SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
                                
                                STARTUPINFOW si = { sizeof(si) };
                                si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
                                si.wShowWindow = SW_HIDE;
                                si.hStdOutput = hWrite;
                                si.hStdError = hWrite;
                                
                                PROCESS_INFORMATION pi = { 0 };
                                std::wstring cmdStr = pfc::stringcvt::string_wide_from_utf8(cmd).get_ptr();
                                std::vector<wchar_t> cmdBuf(cmdStr.begin(), cmdStr.end());
                                cmdBuf.push_back(0);
                                
                                if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                                    CloseHandle(hWrite);
                                    
                                    std::string out;
                                    std::string line_buffer;
                                    char buf[1024];
                                    DWORD readBytes;
                                    
                                    while (true) {
                                        if (abort.is_aborting()) {
                                            TerminateProcess(pi.hProcess, 1);
                                            break;
                                        }
                                        
                                        DWORD bytesAvail = 0;
                                        if (PeekNamedPipe(hRead, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
                                            if (ReadFile(hRead, buf, sizeof(buf) - 1, &readBytes, NULL) && readBytes > 0) {
                                                buf[readBytes] = '\0';
                                                out.append(buf, readBytes);
                                                line_buffer.append(buf, readBytes);
                                                
                                                size_t pos = line_buffer.find("time=");
                                                if (pos != std::string::npos) {
                                                    size_t end_pos = line_buffer.find(" ", pos);
                                                    if (end_pos != std::string::npos) {
                                                        std::string time_str = line_buffer.substr(pos + 5, end_pos - pos - 5);
                                                        int h = 0, m = 0; float s = 0.0f;
                                                        if (sscanf(time_str.c_str(), "%d:%d:%f", &h, &m, &s) == 3) {
                                                            double current_sec = h * 3600.0 + m * 60.0 + s;
                                                            double p = current_sec / duration;
                                                            if (p > 1.0) p = 1.0;
                                                            status.set_progress_float(0.5f + (float)p * 0.5f);
                                                        }
                                                        line_buffer = line_buffer.substr(end_pos);
                                                    }
                                                }
                                                if (line_buffer.size() > 1024) line_buffer.clear();
                                            }
                                        } else {
                                            DWORD ec = 0;
                                            if (GetExitCodeProcess(pi.hProcess, &ec) && ec != STILL_ACTIVE) {
                                                while (ReadFile(hRead, buf, sizeof(buf) - 1, &readBytes, NULL) && readBytes > 0) {
                                                    buf[readBytes] = '\0';
                                                    out.append(buf, readBytes);
                                                }
                                                break;
                                            }
                                            Sleep(20);
                                        }
                                    }
                                    
                                    CloseHandle(hRead);
                                    
                                    WaitForSingleObject(pi.hProcess, INFINITE);
                                    DWORD exitCode = 0;
                                    GetExitCodeProcess(pi.hProcess, &exitCode);
                                    CloseHandle(pi.hProcess);
                                    CloseHandle(pi.hThread);
                                    
                                    _wremove(pfc::stringcvt::string_wide_from_utf8(ffmpeg_temp_wav));
                                    
                                    if (abort.is_aborting()) {
                                        throw pfc::exception("导出已取消。");
                                    }
                                    
                                    if (exitCode != 0) {
                                        pfc::string8 err_msg = pfc::string_formatter() << "FFmpeg 编码失败 (错误码: " << exitCode << ")。";
                                        if (!out.empty()) {
                                            pfc::string8 log_content = pfc::stringcvt::string_utf8_from_ansi(out.c_str()).get_ptr();
                                            if (log_content.length() > 600) {
                                                log_content = log_content.subString(log_content.length() - 600);
                                            }
                                            err_msg << "\n日志：\n" << log_content;
                                        }
                                        throw pfc::exception(err_msg.get_ptr());
                                    }
                                } else {
                                    CloseHandle(hRead);
                                    CloseHandle(hWrite);
                                    _wremove(pfc::stringcvt::string_wide_from_utf8(ffmpeg_temp_wav));
                                    throw pfc::exception("无法启动 FFmpeg，请检查环境变量或路径。");
                                }
                            }
                            
                            fb2k::inMainThread([]() {
                                {
                SpeakMsg(u8"片段导出完成");
                console::print(u8"片段导出完成");
            }
                            });
                        } else {
                            fb2k::inMainThread([]() {
                                {
                SpeakMsg(u8"片段导出失败: 没有数据");
                console::print(u8"片段导出失败: 没有数据");
            }
                            });
                        }
                        
                    } catch (std::exception & e) {
                        pfc::string8 msg = e.what();
                        fb2k::inMainThread([msg]() {
                            {
                                pfc::string_formatter _msg;
                                _msg << u8"导出失败: " << msg;
                                SpeakMsg(_msg.get_ptr());
                                console::print(_msg.get_ptr());
                                popup_message::g_show(_msg.get_ptr(), "Foo Pitch Tempo 导出失败");
                            }
                        });
                    }
                });
                threaded_process::g_run_modeless(tpc, threaded_process::flag_show_progress | threaded_process::flag_show_abort | threaded_process::flag_show_item, core_api::get_main_window(), "导出片段");
            };
            
            auto prompt_save = [export_fmt, handle, export_func, fd_api](pfc::string8 image_path) {
                pfc::string8 initialName;
                handle->format_title(nullptr, initialName, titleformat_compiler::get()->compile_force("%title% - exported"), nullptr);
                
                auto fd = fd_api->setupSave();
                fd->setTitle("导出片段");
                
                if (export_fmt == 0) {
                    fd->setFileTypes("WAV Audio Files|*.wav");
                    fd->setDefaultExtension("wav");
                    initialName += ".wav";
                } else if (export_fmt == 1) {
                    fd->setFileTypes("MP3 Audio Files|*.mp3");
                    fd->setDefaultExtension("mp3");
                    initialName += ".mp3";
                } else if (export_fmt == 2) {
                    fd->setFileTypes("FLAC Audio Files|*.flac");
                    fd->setDefaultExtension("flac");
                    initialName += ".flac";
                } else if (export_fmt == 3) {
                    fd->setFileTypes("MP4 Video Files|*.mp4");
                    fd->setDefaultExtension("mp4");
                    initialName += ".mp4";
                }
                
                fd->setInitialValue(initialName);
                fd->runSimple([export_func, image_path](fb2k::stringRef path) {
                    export_func(path->c_str(), image_path.c_str());
                });
            };
            
            if (export_fmt == 3) {
                pfc::string8 out_img;
                if (uGetOpenFileName(core_api::get_main_window(), "Image Files|*.jpg;*.jpeg;*.png|All Files|*.*", 0, "jpg", "请选择视频背景图片 (取消则可导出纯黑视频)", nullptr, out_img, FALSE)) {
                    prompt_save(out_img.get_ptr());
                } else {
                    if (uMessageBox(core_api::get_main_window(), "未选择背景图片。是否直接生成纯黑画面的视频？\n(点击“是”继续导出纯黑视频，点击“否”取消本次操作)", "导出提示", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        prompt_save("");
                    }
                }
            } else {
                prompt_save("");
            }
        }
    }
};

static mainmenu_commands_factory_t<loop_shortcuts> g_loop_shortcuts_factory;