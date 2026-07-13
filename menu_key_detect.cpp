#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include "resource.h"
#include "key_detector.h"
#include "tolk_wrapper.h"

// External pitch offset from loop_shortcut.cpp
extern std::atomic<float> g_pitch_offset;

static void SpeakText(const char* utf8_text) {
    pfc::stringcvt::string_wide_from_utf8 wstr(utf8_text);
    TolkWrapper_Output(std::wstring(wstr.get_ptr()));
}

// ============================================================================
// Key Detection Dialog
// ============================================================================

class CKeyDetectDialog : public CDialogImpl<CKeyDetectDialog> {
public:
    enum { IDD = IDD_KEY_DETECT };

    BEGIN_MSG_MAP(CKeyDetectDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDC_KEY_ANALYZE, OnAnalyze)
        COMMAND_ID_HANDLER_EX(IDC_KEY_APPLY, OnApply)
        COMMAND_ID_HANDLER_EX(IDC_KEY_RESET, OnReset)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
        COMMAND_HANDLER_EX(IDC_KEY_TARGET, CBN_SELCHANGE, OnTargetChanged)
        COMMAND_HANDLER_EX(IDC_KEY_TARGET_QUALITY, CBN_SELCHANGE, OnTargetChanged)
    END_MSG_MAP()

private:
    key_detection_result_t m_result;
    bool m_has_result = false;
    float m_applied_offset = 0.0f;

    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam) {
        CenterWindow();
        
        // Fill target key combo
        CComboBox cbTarget = GetDlgItem(IDC_KEY_TARGET);
        const char* key_names[] = {
            "C", "C#/Db", "D", "D#/Eb", "E", "F",
            "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"
        };
        for (int i = 0; i < 12; i++) {
            pfc::stringcvt::string_wide_from_utf8 w(key_names[i]);
            cbTarget.AddString(w);
        }
        cbTarget.SetCurSel(0);
        
        // Fill quality combo (Major/Minor)
        CComboBox cbQuality = GetDlgItem(IDC_KEY_TARGET_QUALITY);
        cbQuality.AddString(L"\x5927\x8C03"); // 大调
        cbQuality.AddString(L"\x5C0F\x8C03"); // 小调
        cbQuality.SetCurSel(0);
        
        // Set initial status
        uSetDlgItemText(*this, IDC_KEY_STATUS, u8"就绪。点击 \"分析\" 检测当前播放曲目的调性。");
        uSetDlgItemText(*this, IDC_KEY_RESULT, u8"尚未分析");
        uSetDlgItemText(*this, IDC_KEY_SHIFT_INFO, u8"");
        
        // Disable apply/reset until analysis is done
        ::EnableWindow(GetDlgItem(IDC_KEY_APPLY), FALSE);
        ::EnableWindow(GetDlgItem(IDC_KEY_RESET), FALSE);
        ::EnableWindow(GetDlgItem(IDC_KEY_TARGET), FALSE);
        ::EnableWindow(GetDlgItem(IDC_KEY_TARGET_QUALITY), FALSE);

        return TRUE;
    }

    void OnClose() {
        // If we applied an offset, ask if they want to keep it
        EndDialog(IDCANCEL);
    }

    void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl) {
        EndDialog(IDCANCEL);
    }

    void OnAnalyze(UINT uNotifyCode, int nID, CWindow wndCtl) {
        // Get currently playing track
        auto pc = playback_control::get();
        metadb_handle_ptr track;
        if (!pc->get_now_playing(track)) {
            uSetDlgItemText(*this, IDC_KEY_STATUS, u8"错误：没有正在播放的曲目。请先播放一首歌。");
            SpeakText(u8"没有正在播放的曲目");
            return;
        }

        // Show track name
        pfc::string_formatter track_name;
        {
            // Get track title
            service_ptr_t<titleformat_object> script;
            titleformat_compiler::get()->compile_force(script, "%title% - %artist%");
            track->format_title(nullptr, track_name, script, nullptr);
        }
        
        pfc::string_formatter status_msg;
        status_msg << u8"正在分析: " << track_name.get_ptr() << u8" ...";
        uSetDlgItemText(*this, IDC_KEY_STATUS, status_msg.get_ptr());
        
        // Disable analyze button during analysis
        ::EnableWindow(GetDlgItem(IDC_KEY_ANALYZE), FALSE);

        // Create threaded process for analysis
        auto callback = fb2k::service_new<threaded_process_callback_lambda>();
        
        // Shared state
        struct AnalysisState {
            key_detection_result_t result;
            bool success = false;
            pfc::string8 error_msg;
            pfc::string8 track_name;
        };
        auto state = std::make_shared<AnalysisState>();
        state->track_name = track_name;
        
        HWND hDlg = m_hWnd;
        metadb_handle_ptr track_copy = track;

        callback->m_run = [state, track_copy](threaded_process_status& p_status, abort_callback& p_abort) {
            try {
                p_status.set_item(u8"正在解码音频...");
                p_status.set_progress_float(0);
                
                // Open for decoding
                input_decoder::ptr decoder;
                input_entry::g_open_for_decoding(decoder, nullptr, track_copy->get_path(), p_abort);
                decoder->initialize(track_copy->get_subsong_index(), input_flag_simpledecode | input_flag_no_looping, p_abort);
                
                // Get track length for progress
                file_info_impl info;
                decoder->get_info(track_copy->get_subsong_index(), info, p_abort);
                double total_length = info.get_length();
                if (total_length <= 0) total_length = 300.0; // Assume 5 min if unknown
                
                // We analyze up to 90 seconds for balance of accuracy and speed
                double max_analyze = 90.0;
                if (total_length < max_analyze) max_analyze = total_length;
                
                KeyDetector detector;
                audio_chunk_impl_temporary chunk;
                double decoded_seconds = 0;
                
                std::vector<float> mono_buf;
                
                while (decoder->run(chunk, p_abort)) {
                    p_abort.check();
                    
                    unsigned sr = chunk.get_sample_rate();
                    unsigned ch = chunk.get_channels();
                    size_t samples = chunk.get_sample_count();
                    const audio_sample* data = chunk.get_data();
                    
                    // Downmix to mono
                    mono_buf.resize(samples);
                    for (size_t i = 0; i < samples; i++) {
                        float sum = 0;
                        for (unsigned c = 0; c < ch; c++) {
                            sum += (float)data[i * ch + c];
                        }
                        mono_buf[i] = sum / (float)ch;
                    }
                    
                    detector.feed(mono_buf.data(), samples, sr);
                    decoded_seconds += (double)samples / sr;
                    
                    // Update progress
                    double progress = decoded_seconds / max_analyze;
                    if (progress > 1.0) progress = 1.0;
                    p_status.set_progress_float(progress);
                    
                    pfc::string_formatter item_text;
                    item_text << u8"已分析 " << pfc::format_int((int)decoded_seconds) << u8" 秒...";
                    p_status.set_item(item_text.get_ptr());
                    
                    // Stop after max_analyze seconds
                    if (decoded_seconds >= max_analyze) break;
                }
                
                if (detector.analyzed_seconds() < 2.0) {
                    state->success = false;
                    state->error_msg = u8"音频数据不足，无法分析。";
                    return;
                }
                
                state->result = detector.detect();
                state->success = true;
                
            } catch (const exception_aborted&) {
                state->success = false;
                state->error_msg = u8"分析被用户取消。";
            } catch (const std::exception& e) {
                state->success = false;
                state->error_msg << u8"分析出错: " << e.what();
            }
        };
        
        callback->m_on_done = [this, state, hDlg](HWND p_wnd, bool p_was_aborted) {
            if (!::IsWindow(hDlg)) return;
            
            ::EnableWindow(GetDlgItem(IDC_KEY_ANALYZE), TRUE);
            
            if (p_was_aborted || !state->success) {
                pfc::string_formatter msg;
                if (state->error_msg.get_length() > 0) {
                    msg << state->error_msg;
                } else {
                    msg << u8"分析失败。";
                }
                uSetDlgItemText(*this, IDC_KEY_STATUS, msg.get_ptr());
                SpeakText(msg.get_ptr());
                return;
            }
            
            m_result = state->result;
            m_has_result = true;
            
            // Format result
            pfc::string_formatter result_text;
            result_text << m_result.key_name_utf8() << " " << m_result.quality_utf8();
            result_text << u8"  (置信度: " << pfc::format_int((int)(m_result.confidence * 100)) << "%)";
            uSetDlgItemText(*this, IDC_KEY_RESULT, result_text.get_ptr());
            
            // Set status
            pfc::string_formatter status_text;
            status_text << u8"分析完成: " << state->track_name 
                       << u8" → " << m_result.key_name_utf8() << " " << m_result.quality_utf8();
            uSetDlgItemText(*this, IDC_KEY_STATUS, status_text.get_ptr());
            
            // Auto-select detected key in target combo
            CComboBox cbTarget = GetDlgItem(IDC_KEY_TARGET);
            cbTarget.SetCurSel(m_result.key_index);
            
            CComboBox cbQuality = GetDlgItem(IDC_KEY_TARGET_QUALITY);
            cbQuality.SetCurSel(m_result.is_minor ? 1 : 0);
            
            // Enable controls
            ::EnableWindow(GetDlgItem(IDC_KEY_APPLY), TRUE);
            ::EnableWindow(GetDlgItem(IDC_KEY_RESET), TRUE);
            ::EnableWindow(GetDlgItem(IDC_KEY_TARGET), TRUE);
            ::EnableWindow(GetDlgItem(IDC_KEY_TARGET_QUALITY), TRUE);
            
            // Update shift info
            UpdateShiftInfo();
            
            // Speak result
            pfc::string_formatter speak_text;
            speak_text << u8"检测到调性: " << m_result.key_name_utf8() << " " << m_result.quality_utf8()
                      << u8"，置信度 " << pfc::format_int((int)(m_result.confidence * 100)) << u8"%. "
                      << u8"请选择目标调性并点击应用。";
            SpeakText(speak_text.get_ptr());
        };
        
        threaded_process::g_run_modeless(
            callback,
            threaded_process::flag_show_abort | threaded_process::flag_show_progress | threaded_process::flag_show_item,
            core_api::get_main_window(),
            u8"正在分析调性..."
        );
    }

    void OnApply(UINT uNotifyCode, int nID, CWindow wndCtl) {
        if (!m_has_result) return;
        
        CComboBox cbTarget = GetDlgItem(IDC_KEY_TARGET);
        CComboBox cbQuality = GetDlgItem(IDC_KEY_TARGET_QUALITY);
        
        int target_key = cbTarget.GetCurSel();
        bool target_minor = (cbQuality.GetCurSel() == 1);
        
        int semitones = m_result.semitones_to(target_key, target_minor);
        
        if (semitones == 0) {
            pfc::string_formatter msg;
            msg << u8"目标调与原调相同，无需变调。";
            uSetDlgItemText(*this, IDC_KEY_STATUS, msg.get_ptr());
            SpeakText(msg.get_ptr());
            return;
        }
        
        // Ensure BASS DSP is active
        {
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
        
        // Apply pitch offset
        m_applied_offset = (float)semitones;
        g_pitch_offset.store(m_applied_offset);
        
        // Update status
        const char* target_name = g_key_names_utf8[target_key];
        const char* target_quality = target_minor ? u8"小调" : u8"大调";
        
        pfc::string_formatter msg;
        msg << u8"已应用变调: " << m_result.key_name_utf8() << " " << m_result.quality_utf8()
            << u8" → " << target_name << " " << target_quality
            << u8" (" << (semitones > 0 ? "+" : "") << pfc::format_int(semitones) << u8" 半音)";
        uSetDlgItemText(*this, IDC_KEY_STATUS, msg.get_ptr());
        SpeakText(msg.get_ptr());
        
        console::print(msg.get_ptr());
    }

    void OnReset(UINT uNotifyCode, int nID, CWindow wndCtl) {
        g_pitch_offset.store(0.0f);
        m_applied_offset = 0.0f;
        
        if (m_has_result) {
            CComboBox cbTarget = GetDlgItem(IDC_KEY_TARGET);
            cbTarget.SetCurSel(m_result.key_index);
            
            CComboBox cbQuality = GetDlgItem(IDC_KEY_TARGET_QUALITY);
            cbQuality.SetCurSel(m_result.is_minor ? 1 : 0);
            
            UpdateShiftInfo();
        }
        
        uSetDlgItemText(*this, IDC_KEY_STATUS, u8"已重置音高偏移。");
        SpeakText(u8"已重置音高偏移为零");
    }

    void OnTargetChanged(UINT uNotifyCode, int nID, CWindow wndCtl) {
        UpdateShiftInfo();
    }

    void UpdateShiftInfo() {
        if (!m_has_result) return;
        
        CComboBox cbTarget = GetDlgItem(IDC_KEY_TARGET);
        CComboBox cbQuality = GetDlgItem(IDC_KEY_TARGET_QUALITY);
        
        int target_key = cbTarget.GetCurSel();
        bool target_minor = (cbQuality.GetCurSel() == 1);
        int semitones = m_result.semitones_to(target_key, target_minor);
        
        const char* target_name = g_key_names_utf8[target_key];
        const char* target_quality = target_minor ? u8"小调" : u8"大调";
        
        pfc::string_formatter info;
        if (semitones == 0) {
            info << u8"无需变调 (相同调性)";
        } else {
            info << m_result.key_name_utf8() << " " << m_result.quality_utf8()
                 << u8" → " << target_name << " " << target_quality
                 << u8": " << (semitones > 0 ? "+" : "") << pfc::format_int(semitones) << u8" 半音";
        }
        uSetDlgItemText(*this, IDC_KEY_SHIFT_INFO, info.get_ptr());
    }
};

// ============================================================================
// Menu Integration: 菜单栏 / 播放 / 变速变调与循环 / 调性检测
// (Placed under the existing "变速变调与循环" menu group to avoid creating
//  a separate top-level menu — more consistent with existing UX)
// ============================================================================

// We'll use a separate sub-popup group under the existing loop group
// But the user requested 工具/调性检测, so let's create a "工具" top-level menu

extern const GUID guid_loop_group;

// GUID for the key detection command
static const GUID guid_key_detect_cmd = 
    { 0xc8f20a11, 0x3b7e, 0x4d5a, { 0xa1, 0x2c, 0x9e, 0x8f, 0x4b, 0x3d, 0x7c, 0x10 } };

class key_detect_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }
    
    GUID get_command(t_uint32 p_index) override {
        return guid_key_detect_cmd;
    }
    
    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = u8"调性检测(&K)...";
    }
    
    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = u8"检测当前播放曲目的音乐调性，并可一键变调到目标调";
        return true;
    }
    
    GUID get_parent() override { return guid_loop_group; }
    
    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        if (p_index == 0) {
            // Show key detection dialog (modeless)
            CKeyDetectDialog dlg;
            dlg.DoModal(core_api::get_main_window());
        }
    }
};

static mainmenu_commands_factory_t<key_detect_mainmenu> g_key_detect_menu_factory;
