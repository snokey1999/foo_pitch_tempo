#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <math.h>
#include <vector>
#include <memory>
#include <atomic>

// This replaces the old bass_dsp with the same GUID to preserve user configurations
static const GUID guid_pitch_tempo_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };

extern cfg_int cfg_pitch_algo;
extern std::atomic<float> g_pitch_offset;
extern std::atomic<float> g_tempo_offset;

// Include RubberBand single-file wrapper header
#include "../../../rubberband-3.3.0/rubberband/RubberBandStretcher.h"

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
    out.set_owner(guid_pitch_tempo_dsp);
    out.set_data(&d, sizeof(d));
}

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback);

class dsp_rubberband : public dsp_impl_base {
    std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
    
    unsigned m_sample_rate = 0;
    unsigned m_channels = 0;
    
    float m_pitch = 0.0f;
    float m_tempo = 0.0f;
    
    float m_last_pitch = -9999.0f;
    float m_last_tempo = -9999.0f;
    int m_last_algo_choice = -1;

    std::vector<audio_sample> m_accum;
    std::vector<std::vector<float>> m_deinterleaved_in;
    std::vector<std::vector<float>> m_deinterleaved_out;
    std::vector<float*> m_in_ptrs;
    std::vector<float*> m_out_ptrs;

public:
    dsp_rubberband(dsp_preset const & in) {
        parse_preset(in, m_pitch, m_tempo);
    }
    
    ~dsp_rubberband() {
    }

    static GUID g_get_guid() { return guid_pitch_tempo_dsp; }
    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) { RunDSPConfigPopup(p_data, p_parent, p_callback); }
    static void g_get_name(pfc::string_base & p_out) { p_out = u8"变速变调 (RubberBand)"; }
    static bool g_get_default_preset(dsp_preset & p_out) { make_preset(0.0f, 0.0f, p_out); return true; }

    bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) override {
        int algo_choice = cfg_pitch_algo.get_value();
        bool algo_changed = (algo_choice != m_last_algo_choice);

        if (chunk->get_sample_rate() != m_sample_rate || chunk->get_channels() != m_channels || algo_changed) {
            m_sample_rate = chunk->get_sample_rate();
            m_channels = chunk->get_channels();
            m_last_algo_choice = algo_choice;
            
            // Map our 4 UI algorithms to RubberBand options
            int options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
            
            if (algo_choice == 0) {
                // Fast mode (R2 engine, crisp transients)
                options |= RubberBand::RubberBandStretcher::OptionEngineFaster;
                options |= RubberBand::RubberBandStretcher::OptionTransientsCrisp;
            } else if (algo_choice == 1) {
                // High Quality (R3 engine, default)
                options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
            } else if (algo_choice == 2) {
                // Smooth Pitch Consistency (R3 engine)
                options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
                options |= RubberBand::RubberBandStretcher::OptionPitchHighConsistency;
            } else if (algo_choice == 3) {
                // Formant Preserving (R3 engine)
                options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
                options |= RubberBand::RubberBandStretcher::OptionFormantPreserved;
            }
            
            m_stretcher.reset(new RubberBand::RubberBandStretcher(m_sample_rate, m_channels, options));
            
            m_last_pitch = -9999.0f;
            m_last_tempo = -9999.0f;

            m_deinterleaved_in.resize(m_channels);
            m_deinterleaved_out.resize(m_channels);
            m_in_ptrs.resize(m_channels);
            m_out_ptrs.resize(m_channels);
        }

        float current_pitch = m_pitch + g_pitch_offset.load();
        float current_tempo = m_tempo + g_tempo_offset.load();

        if (current_tempo != m_last_tempo || current_pitch != m_last_pitch) {
            // Tempo UI is percentage (-50 to 100), where 0 is 100% speed.
            float speed_multiplier = 1.0f + (current_tempo / 100.0f);
            if (speed_multiplier <= 0.01f) speed_multiplier = 0.01f;
            float time_ratio = 1.0f / speed_multiplier;
            m_stretcher->setTimeRatio(time_ratio);
            
            // Pitch UI is semitones (-24 to 24)
            float pitch_scale = powf(2.0f, current_pitch / 12.0f);
            m_stretcher->setPitchScale(pitch_scale);
            
            m_last_tempo = current_tempo;
            m_last_pitch = current_pitch;
        }

        size_t frames = chunk->get_sample_count();
        if (frames == 0) return true;

        const audio_sample* data_in = chunk->get_data();
        
        // Deinterleave input
        for (size_t c = 0; c < m_channels; ++c) {
            m_deinterleaved_in[c].resize(frames);
            m_in_ptrs[c] = m_deinterleaved_in[c].data();
            for (size_t i = 0; i < frames; ++i) {
                m_deinterleaved_in[c][i] = data_in[i * m_channels + c];
            }
        }

        m_stretcher->process(m_in_ptrs.data(), frames, false);
        
        m_accum.clear();
        
        retrieve_available_frames();
        
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
    
    void retrieve_available_frames() {
        if (!m_stretcher) return;
        
        int available = m_stretcher->available();
        if (available > 0) {
            for (size_t c = 0; c < m_channels; ++c) {
                m_deinterleaved_out[c].resize(available);
                m_out_ptrs[c] = m_deinterleaved_out[c].data();
            }
            
            size_t retrieved = m_stretcher->retrieve(m_out_ptrs.data(), available);
            
            size_t old_size = m_accum.size();
            m_accum.resize(old_size + retrieved * m_channels);
            
            // Interleave output
            for (size_t i = 0; i < retrieved; ++i) {
                for (size_t c = 0; c < m_channels; ++c) {
                    m_accum[old_size + i * m_channels + c] = m_deinterleaved_out[c][i];
                }
            }
        }
    }
    
    void drain(abort_callback & p_abort) {
        if (m_stretcher) {
            m_stretcher->process(nullptr, 0, true); // Finalize
            
            m_accum.clear();
            
            while (m_stretcher->available() > 0) {
                p_abort.check();
                retrieve_available_frames();
            }
            
            if (!m_accum.empty()) {
                audio_chunk * out_chunk = insert_chunk(m_accum.size() / m_channels);
                out_chunk->set_data(m_accum.data(), m_accum.size() / m_channels, m_channels, m_sample_rate);
            }
        }
    }

    void flush() override {
        if (m_stretcher) {
            m_stretcher->reset();
        }
        m_last_pitch = -9999.0f;
        m_last_tempo = -9999.0f;
    }

    double get_latency() override { 
        if (m_stretcher) return (double)m_stretcher->getLatency() / m_sample_rate;
        return 0; 
    }
    bool need_track_change_mark() override { return false; }
};

class CDialogDSPPitchTempo : public CDialogImpl<CDialogDSPPitchTempo> {
public:
    enum { IDD = IDD_DSP_BASS }; // Reuse the same UI dialog ID

    CDialogDSPPitchTempo(const dsp_preset & initData) {
        parse_preset(initData, m_pitch, m_tempo);
    }
    
    const dsp_preset & GetPreset() const { return m_preset; }

    BEGIN_MSG_MAP(CDialogDSPPitchTempo)
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
        
        int pitch = sliderPitch.GetPos();
        int tempo = sliderTempo.GetPos();

        pfc::string8 strPitch;
        if (pitch > 0) strPitch << "+" << pitch << " 半音";
        else if (pitch < 0) strPitch << pitch << " 半音";
        else strPitch << "0 半音";

        pfc::string8 strTempo;
        if (tempo > 0) strTempo << "+" << tempo << "%";
        else if (tempo < 0) strTempo << tempo << "%";
        else strTempo << "0%";

        SetDlgItemText(IDC_PITCH_LABEL, pfc::stringcvt::string_os_from_utf8(strPitch));
        SetDlgItemText(IDC_TEMPO_LABEL, pfc::stringcvt::string_os_from_utf8(strTempo));
    }

    float m_pitch;
    float m_tempo;
    dsp_preset_impl m_preset;
};

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) {
    CDialogDSPPitchTempo dlg(p_data);
    if (dlg.DoModal(p_parent) == IDOK) {
        p_callback.on_preset_changed(dlg.GetPreset());
    }
}

static dsp_factory_t<dsp_rubberband> g_dsp_rubberband_factory;
