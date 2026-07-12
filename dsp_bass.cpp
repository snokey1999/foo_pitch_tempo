#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <math.h>
#include <vector>

static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };

extern cfg_int cfg_pitch_algo;

static void parse_preset(const dsp_preset & in, float & pitch, float & tempo) {
    if (in.get_data_size() == sizeof(float) * 2) {
        const float * d = (const float *)in.get_data();
        pitch = d[0];
        tempo = d[1];
    } else {
        pitch = 0.0f;
        tempo = 0.0f;
    }
}

static void make_preset(float pitch, float tempo, dsp_preset & out) {
    float d[2] = { pitch, tempo };
    out.set_owner(guid_bass_dsp);
    out.set_data(&d, sizeof(d));
}

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback);

#include <atomic>
extern std::atomic<float> g_pitch_offset;
extern std::atomic<float> g_tempo_offset;

class dsp_bass : public dsp_impl_base {
    HSTREAM m_push_stream = 0;
    HSTREAM m_fx_stream = 0;
    unsigned m_sample_rate = 0;
    unsigned m_channels = 0;
    
    float m_pitch = 0.0f;
    float m_tempo = 0.0f;
    
    float m_last_pitch = -9999.0f;
    float m_last_tempo = -9999.0f;
    int m_last_algo_choice = -1;

    std::vector<audio_sample> m_accum;
    std::vector<float> m_bass_buffer;
    std::vector<float> m_bass_push_buffer;

public:
    dsp_bass(dsp_preset const & in) {
        parse_preset(in, m_pitch, m_tempo);
    }
    
    ~dsp_bass() {
        if (m_fx_stream) BASS_StreamFree(m_fx_stream);
    }

    static GUID g_get_guid() { return guid_bass_dsp; }
    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) { RunDSPConfigPopup(p_data, p_parent, p_callback); }
    static void g_get_name(pfc::string_base & p_out) { p_out = u8"变速变调"; }
    static bool g_get_default_preset(dsp_preset & p_out) { make_preset(0.0f, 0.0f, p_out); return true; }

    bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) override {
        int algo_choice = cfg_pitch_algo.get_value();
        bool algo_changed = (algo_choice != m_last_algo_choice);

        if (chunk->get_sample_rate() != m_sample_rate || chunk->get_channels() != m_channels || algo_changed) {
            if (m_fx_stream) BASS_StreamFree(m_fx_stream);
            m_sample_rate = chunk->get_sample_rate();
            m_channels = chunk->get_channels();
            m_last_algo_choice = algo_choice;
            m_push_stream = BASS_StreamCreate(m_sample_rate, m_channels, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, STREAMPROC_PUSH, NULL);
            
            DWORD algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
            float quick_algo = 1.0f;
            float aa_filter = 1.0f;
            
            if (algo_choice == 0) {
                algo_flag = BASS_FX_TEMPO_ALGO_LINEAR;
                quick_algo = 1.0f;
                aa_filter = 0.0f;
            } else if (algo_choice == 1) {
                algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
                quick_algo = 1.0f;
                aa_filter = 1.0f;
            } else if (algo_choice == 2) {
                algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
                quick_algo = 0.0f;
                aa_filter = 1.0f;
            } else if (algo_choice == 3) {
                algo_flag = BASS_FX_TEMPO_ALGO_SHANNON;
                quick_algo = 0.0f;
                aa_filter = 1.0f;
            }
            
            m_fx_stream = BASS_FX_TempoCreate(m_push_stream, BASS_STREAM_DECODE | BASS_FX_FREESOURCE | algo_flag);
            
            BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_PREVENT_CLICK, 1.0f);
            BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, quick_algo);
            BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_USE_AA_FILTER, aa_filter);
            
            // 核心修复点：流被重建了，必须清空历史速度/音调缓存，强制触发下方SetAttribute
            m_last_pitch = -9999.0f;
            m_last_tempo = -9999.0f;

            m_bass_buffer.resize(m_sample_rate * m_channels);
            m_accum.reserve(m_sample_rate * m_channels * 2);
        }

        float current_pitch = m_pitch + g_pitch_offset.load();
        float current_tempo = m_tempo + g_tempo_offset.load();

        if (current_tempo != m_last_tempo) {
            BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO, current_tempo);
            m_last_tempo = current_tempo;
        }
        if (current_pitch != m_last_pitch) {
            BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_PITCH, current_pitch);
            m_last_pitch = current_pitch;
        }

        size_t sample_count = chunk->get_sample_count() * m_channels;
        m_bass_push_buffer.resize(sample_count);
        const audio_sample* data_in = chunk->get_data();
        for(size_t i = 0; i < sample_count; ++i) {
            m_bass_push_buffer[i] = (float)data_in[i];
        }

        BASS_StreamPutData(m_push_stream, m_bass_push_buffer.data(), (DWORD)(sample_count * sizeof(float)));
        
        m_accum.clear();
        
        while (true) {
            // 注意：BASS_ChannelGetData 应该直接从处理节点 m_fx_stream 获取
            DWORD bytes_read = BASS_ChannelGetData(m_fx_stream, m_bass_buffer.data(), (DWORD)(m_bass_buffer.size() * sizeof(float)));
            if (bytes_read == 0 || bytes_read == (DWORD)-1) break;
            
            size_t floats_read = bytes_read / sizeof(float);
            size_t old_size = m_accum.size();
            m_accum.resize(old_size + floats_read);
            for(size_t i = 0; i < floats_read; ++i) {
                m_accum[old_size + i] = (audio_sample)m_bass_buffer[i];
            }
        }
        
        chunk->set_data(m_accum.empty() ? nullptr : m_accum.data(), m_accum.size() / m_channels, m_channels, m_sample_rate);
        return true;
    }

    void on_endofplayback(abort_callback & p_abort) override {
        drain(p_abort);
        flush();
    }
    
    void on_endoftrack(abort_callback & p_abort) override {
        drain(p_abort);
        flush();
    }
    
    void drain(abort_callback & p_abort) {
        if (m_push_stream && m_fx_stream) {
            char dummy = 0;
            BASS_StreamPutData(m_push_stream, &dummy, BASS_STREAMPROC_END);
            
            m_accum.clear();
            
            while (true) {
                p_abort.check();
                DWORD bytes_read = BASS_ChannelGetData(m_fx_stream, m_bass_buffer.data(), (DWORD)(m_bass_buffer.size() * sizeof(float)));
                if (bytes_read == 0 || bytes_read == (DWORD)-1) break;
                
                size_t floats_read = bytes_read / sizeof(float);
                size_t old_size = m_accum.size();
                m_accum.resize(old_size + floats_read);
                for(size_t i = 0; i < floats_read; ++i) {
                    m_accum[old_size + i] = (audio_sample)m_bass_buffer[i];
                }
            }
            
            if (!m_accum.empty()) {
                audio_chunk * out_chunk = insert_chunk(m_accum.size() / m_channels);
                out_chunk->set_data(m_accum.data(), m_accum.size() / m_channels, m_channels, m_sample_rate);
            }
        }
    }

    void flush() override {
        if (m_fx_stream) BASS_StreamFree(m_fx_stream);
        m_push_stream = 0;
        m_fx_stream = 0;
        m_sample_rate = 0;
        m_channels = 0;
        m_last_pitch = -9999.0f;
        m_last_tempo = -9999.0f;
        m_last_algo_choice = -1;
    }

    double get_latency() override { return 0; }
    bool need_track_change_mark() override { return false; }
};

class CDialogDSPBass : public CDialogImpl<CDialogDSPBass> {
public:
    enum { IDD = IDD_DSP_BASS };

    CDialogDSPBass(const dsp_preset & initData) {
        parse_preset(initData, m_pitch, m_tempo);
    }
    
    const dsp_preset & GetPreset() const { return m_preset; }

    BEGIN_MSG_MAP(CDialogDSPBass)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnCloseCmd)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCloseCmd)
        MSG_WM_HSCROLL(OnHScroll)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam) {
        CTrackBarCtrl sliderPitch = GetDlgItem(IDC_PITCH);
        sliderPitch.SetRange(-24, 24); // -24 to +24 semitones
        sliderPitch.SetPos((int)m_pitch);

        CTrackBarCtrl sliderTempo = GetDlgItem(IDC_TEMPO);
        sliderTempo.SetRange(-50, 100); // -50% to +100% speed
        sliderTempo.SetPos((int)m_tempo);
        
        UpdateLabels();
        return TRUE;
    }

    void OnCloseCmd(UINT uNotifyCode, int nID, CWindow wndCtl) {
        if (nID == IDOK) {
            CTrackBarCtrl sliderPitch = GetDlgItem(IDC_PITCH);
            CTrackBarCtrl sliderTempo = GetDlgItem(IDC_TEMPO);
            m_pitch = (float)sliderPitch.GetPos();
            m_tempo = (float)sliderTempo.GetPos();
            make_preset(m_pitch, m_tempo, m_preset);
        }
        EndDialog(nID);
    }

    void OnHScroll(int nSBCode, short nPos, CScrollBar pScrollBar) {
        UpdateLabels();
    }

    void UpdateLabels() {
        CTrackBarCtrl sliderPitch = GetDlgItem(IDC_PITCH);
        CTrackBarCtrl sliderTempo = GetDlgItem(IDC_TEMPO);
        
        pfc::string8 str;
        str = pfc::format_float(sliderPitch.GetPos(), 0, 1);
        uSetDlgItemText(*this, IDC_PITCH_LABEL, str);
        
        str = pfc::format_float(sliderTempo.GetPos(), 0, 1);
        str += "%";
        uSetDlgItemText(*this, IDC_TEMPO_LABEL, str);
    }
    
    dsp_preset_impl m_preset;
    float m_pitch = 0.0f;
    float m_tempo = 0.0f;
};

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) {
    CDialogDSPBass dlg(p_data);
    if (dlg.DoModal(p_parent) == IDOK) {
        p_callback.on_preset_changed(dlg.GetPreset());
    }
}

static dsp_factory_t<dsp_bass> g_dsp_bass_factory;