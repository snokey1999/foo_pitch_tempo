#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <math.h>

static const GUID guid_bass_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };

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
        if (chunk->get_sample_rate() != m_sample_rate || chunk->get_channels() != m_channels) {
            if (m_fx_stream) BASS_StreamFree(m_fx_stream);
            m_sample_rate = chunk->get_sample_rate();
            m_channels = chunk->get_channels();
            m_push_stream = BASS_StreamCreate(m_sample_rate, m_channels, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, STREAMPROC_PUSH, NULL);
            m_fx_stream = BASS_FX_TempoCreate(m_push_stream, BASS_STREAM_DECODE | BASS_FX_FREESOURCE);
        }

        float current_pitch = m_pitch + g_pitch_offset.load();
        float current_tempo = m_tempo + g_tempo_offset.load();

        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO, current_tempo);
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_PITCH, current_pitch);

        BASS_StreamPutData(m_push_stream, chunk->get_data(), (DWORD)(chunk->get_data_size() * sizeof(audio_sample)));
        
        pfc::array_t<audio_sample> accum;
        pfc::array_t<audio_sample> buffer;
        buffer.set_size(m_sample_rate * m_channels); // 1 second buffer
        
        while (true) {
            DWORD bytes_read = BASS_ChannelGetData(m_fx_stream, buffer.get_ptr(), (DWORD)(buffer.get_size() * sizeof(audio_sample)));
            if (bytes_read == 0 || bytes_read == (DWORD)-1) break;
            
            size_t old_size = accum.get_size();
            size_t samples_read = bytes_read / sizeof(audio_sample);
            accum.set_size(old_size + samples_read);
            pfc::memcpy_t(accum.get_ptr() + old_size, buffer.get_ptr(), samples_read);
        }
        
        if (accum.get_size() > 0) {
            chunk->set_data(accum.get_ptr(), accum.get_size() / m_channels, m_channels, m_sample_rate);
            return true;
        }
        
        return false;
    }

    void on_endofplayback(abort_callback & p_abort) override {
        flush();
    }
    
    void on_endoftrack(abort_callback & p_abort) override {
        flush();
    }
    
    void flush() override {
        if (m_fx_stream) BASS_StreamFree(m_fx_stream);
        m_push_stream = 0;
        m_fx_stream = 0;
        m_sample_rate = 0;
        m_channels = 0;
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

    float m_pitch;
    float m_tempo;
    dsp_preset_impl m_preset;
};

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) {
    CDialogDSPBass dlg(p_data);
    if (dlg.DoModal(p_parent) == IDOK) {
        p_callback.on_preset_changed(dlg.GetPreset());
    }
}

static dsp_factory_t<dsp_bass> g_dsp_bass_factory;





