#include "stdafx.h"
#include <foobar2000/helpers/foobar2000-lite+atl.h>
#include <atlcrack.h>
#include <math.h>
#include <vector>
#include <memory>
#include <atomic>

static const GUID guid_pitch_tempo_dsp = { 0x85fbfd09, 0x0099, 0x4fe9, { 0x9d, 0xb6, 0x78, 0xdb, 0x6f, 0x60, 0xf8, 0x18 } };

extern cfg_int cfg_pitch_algo;
extern cfg_int cfg_dsp_engine;
extern std::atomic<float> g_pitch_offset;
extern std::atomic<float> g_tempo_offset;

#include "bass.h"
#include "bass_fx.h"
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

// Interface for DSP engine
class IDSPContext {
public:
    virtual ~IDSPContext() {}
    virtual void setup(unsigned sample_rate, unsigned channels, int algo) = 0;
    virtual void update_params(float pitch, float tempo) = 0;
    virtual void process(audio_chunk * chunk, std::vector<audio_sample> & accum, abort_callback & abort) = 0;
    virtual void drain(std::vector<audio_sample> & accum, abort_callback & abort) = 0;
    virtual void reset() = 0;
    virtual double get_latency(unsigned sample_rate) = 0;
};

// Bass Engine Implementation
class BassContext : public IDSPContext {
    HSTREAM m_push_stream = 0;
    HSTREAM m_fx_stream = 0;
    unsigned m_sample_rate = 0;
    unsigned m_channels = 0;
    std::vector<float> m_bass_buffer;
    std::vector<float> m_bass_push_buffer;

public:
    ~BassContext() {
        if (m_fx_stream) BASS_StreamFree(m_fx_stream);
    }

    void setup(unsigned sample_rate, unsigned channels, int algo) override {
        if (m_fx_stream) BASS_StreamFree(m_fx_stream);
        m_sample_rate = sample_rate;
        m_channels = channels;
        m_push_stream = BASS_StreamCreate(m_sample_rate, m_channels, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, STREAMPROC_PUSH, NULL);
        
        DWORD algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
        float quick_algo = 1.0f;
        float aa_filter = 1.0f;
        
        if (algo == 0) {
            algo_flag = BASS_FX_TEMPO_ALGO_LINEAR;
            quick_algo = 1.0f;
            aa_filter = 0.0f;
        } else if (algo == 1) {
            algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
            quick_algo = 1.0f;
            aa_filter = 1.0f;
        } else if (algo == 2) {
            algo_flag = BASS_FX_TEMPO_ALGO_CUBIC;
            quick_algo = 0.0f;
            aa_filter = 1.0f;
        } else if (algo == 3) {
            algo_flag = BASS_FX_TEMPO_ALGO_SHANNON;
            quick_algo = 0.0f;
            aa_filter = 1.0f;
        }
        
        m_fx_stream = BASS_FX_TempoCreate(m_push_stream, BASS_STREAM_DECODE | BASS_FX_FREESOURCE | algo_flag);
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_PREVENT_CLICK, 1.0f);
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, quick_algo);
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_OPTION_USE_AA_FILTER, aa_filter);
        
        m_bass_buffer.resize(m_sample_rate * m_channels);
    }

    void update_params(float pitch, float tempo) override {
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO, tempo);
        BASS_ChannelSetAttribute(m_fx_stream, BASS_ATTRIB_TEMPO_PITCH, pitch);
    }

    void process(audio_chunk * chunk, std::vector<audio_sample> & accum, abort_callback & abort) override {
        size_t sample_count = chunk->get_sample_count() * m_channels;
        m_bass_push_buffer.resize(sample_count);
        const audio_sample* data_in = chunk->get_data();
        for(size_t i = 0; i < sample_count; ++i) {
            m_bass_push_buffer[i] = (float)data_in[i];
        }

        BASS_StreamPutData(m_push_stream, m_bass_push_buffer.data(), (DWORD)(sample_count * sizeof(float)));
        accum.clear();
        
        while (true) {
            DWORD bytes_read = BASS_ChannelGetData(m_fx_stream, m_bass_buffer.data(), (DWORD)(m_bass_buffer.size() * sizeof(float)));
            if (bytes_read == 0 || bytes_read == (DWORD)-1) break;
            
            size_t floats_read = bytes_read / sizeof(float);
            size_t old_size = accum.size();
            accum.resize(old_size + floats_read);
            for(size_t i = 0; i < floats_read; ++i) {
                accum[old_size + i] = (audio_sample)m_bass_buffer[i];
            }
        }
    }

    void drain(std::vector<audio_sample> & accum, abort_callback & abort) override {
        if (m_push_stream && m_fx_stream) {
            char dummy = 0;
            BASS_StreamPutData(m_push_stream, &dummy, BASS_STREAMPROC_END);
            
            accum.clear();
            
            while (true) {
                abort.check();
                DWORD bytes_read = BASS_ChannelGetData(m_fx_stream, m_bass_buffer.data(), (DWORD)(m_bass_buffer.size() * sizeof(float)));
                if (bytes_read == 0 || bytes_read == (DWORD)-1) break;
                
                size_t floats_read = bytes_read / sizeof(float);
                size_t old_size = accum.size();
                accum.resize(old_size + floats_read);
                for(size_t i = 0; i < floats_read; ++i) {
                    accum[old_size + i] = (audio_sample)m_bass_buffer[i];
                }
            }
        }
    }

    void reset() override {
        if (m_fx_stream) {
            BASS_ChannelSetPosition(m_fx_stream, 0, BASS_POS_BYTE);
        }
        if (m_push_stream) {
            BASS_ChannelSetPosition(m_push_stream, 0, BASS_POS_BYTE);
        }
    }

    double get_latency(unsigned sample_rate) override { return 0; }
};

// RubberBand Engine Implementation
class RubberBandContext : public IDSPContext {
    std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
    unsigned m_channels = 0;
    std::vector<std::vector<float>> m_deinterleaved_in;
    std::vector<std::vector<float>> m_deinterleaved_out;
    std::vector<float*> m_in_ptrs;
    std::vector<float*> m_out_ptrs;

public:
    void setup(unsigned sample_rate, unsigned channels, int algo) override {
        m_channels = channels;
        int options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
        
        if (algo == 0) {
            options |= RubberBand::RubberBandStretcher::OptionEngineFaster;
            options |= RubberBand::RubberBandStretcher::OptionTransientsCrisp;
        } else if (algo == 1) {
            options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
        } else if (algo == 2) {
            options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
            options |= RubberBand::RubberBandStretcher::OptionPitchHighConsistency;
        } else if (algo == 3) {
            options |= RubberBand::RubberBandStretcher::OptionEngineFiner;
            options |= RubberBand::RubberBandStretcher::OptionFormantPreserved;
        }
        
        m_stretcher.reset(new RubberBand::RubberBandStretcher(sample_rate, channels, options));
        
        m_deinterleaved_in.resize(m_channels);
        m_deinterleaved_out.resize(m_channels);
        m_in_ptrs.resize(m_channels);
        m_out_ptrs.resize(m_channels);
    }

    void update_params(float pitch, float tempo) override {
        float speed_multiplier = 1.0f + (tempo / 100.0f);
        if (speed_multiplier <= 0.01f) speed_multiplier = 0.01f;
        float time_ratio = 1.0f / speed_multiplier;
        m_stretcher->setTimeRatio(time_ratio);
        
        float pitch_scale = powf(2.0f, pitch / 12.0f);
        m_stretcher->setPitchScale(pitch_scale);
    }

    void process(audio_chunk * chunk, std::vector<audio_sample> & accum, abort_callback & abort) override {
        size_t frames = chunk->get_sample_count();
        const audio_sample* data_in = chunk->get_data();
        
        accum.clear();
        const size_t MAX_BLOCK = 2048;
        
        for (size_t offset = 0; offset < frames; offset += MAX_BLOCK) {
            abort.check();
            size_t block_frames = frames - offset;
            if (block_frames > MAX_BLOCK) block_frames = MAX_BLOCK;
            
            for (size_t c = 0; c < m_channels; ++c) {
                m_deinterleaved_in[c].resize(block_frames);
                m_in_ptrs[c] = m_deinterleaved_in[c].data();
                for (size_t i = 0; i < block_frames; ++i) {
                    m_deinterleaved_in[c][i] = data_in[(offset + i) * m_channels + c];
                }
            }

            m_stretcher->process(m_in_ptrs.data(), block_frames, false);
            retrieve_available_frames(accum);
        }
    }

    void drain(std::vector<audio_sample> & accum, abort_callback & abort) override {
        if (m_stretcher) {
            m_stretcher->process(nullptr, 0, true);
            accum.clear();
            while (m_stretcher->available() > 0) {
                abort.check();
                retrieve_available_frames(accum);
            }
        }
    }

    void retrieve_available_frames(std::vector<audio_sample> & accum) {
        if (!m_stretcher) return;
        int available = m_stretcher->available();
        if (available > 0) {
            for (size_t c = 0; c < m_channels; ++c) {
                m_deinterleaved_out[c].resize(available);
                m_out_ptrs[c] = m_deinterleaved_out[c].data();
            }
            size_t retrieved = m_stretcher->retrieve(m_out_ptrs.data(), available);
            size_t old_size = accum.size();
            accum.resize(old_size + retrieved * m_channels);
            for (size_t i = 0; i < retrieved; ++i) {
                for (size_t c = 0; c < m_channels; ++c) {
                    accum[old_size + i * m_channels + c] = m_deinterleaved_out[c][i];
                }
            }
        }
    }

    void reset() override {
        if (m_stretcher) m_stretcher->reset();
    }

    double get_latency(unsigned sample_rate) override {
        if (m_stretcher) return (double)m_stretcher->getLatency() / sample_rate;
        return 0;
    }
};

class dsp_unified : public dsp_impl_base {
    std::unique_ptr<IDSPContext> m_ctx;
    
    unsigned m_sample_rate = 0;
    unsigned m_channels = 0;
    
    float m_pitch = 0.0f;
    float m_tempo = 0.0f;
    
    float m_last_pitch = -9999.0f;
    float m_last_tempo = -9999.0f;
    int m_last_algo = -1;
    int m_last_engine = -1;

    std::vector<audio_sample> m_accum;

public:
    dsp_unified(dsp_preset const & in) {
        parse_preset(in, m_pitch, m_tempo);
    }
    
    static GUID g_get_guid() { return guid_pitch_tempo_dsp; }
    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) { RunDSPConfigPopup(p_data, p_parent, p_callback); }
    static void g_get_name(pfc::string_base & p_out) { p_out = u8"变速变调 (双引擎)"; }
    static bool g_get_default_preset(dsp_preset & p_out) { make_preset(0.0f, 0.0f, p_out); return true; }

    bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) override {
        int algo = cfg_pitch_algo.get_value();
        int engine = cfg_dsp_engine.get_value();

        bool setup_needed = (chunk->get_sample_rate() != m_sample_rate || 
                             chunk->get_channels() != m_channels || 
                             algo != m_last_algo || 
                             engine != m_last_engine || 
                             !m_ctx);

        if (setup_needed) {
            m_sample_rate = chunk->get_sample_rate();
            m_channels = chunk->get_channels();
            m_last_algo = algo;
            m_last_engine = engine;

            if (engine == 0) m_ctx.reset(new BassContext());
            else m_ctx.reset(new RubberBandContext());
            
            m_ctx->setup(m_sample_rate, m_channels, m_last_algo);
            
            m_last_pitch = -9999.0f;
            m_last_tempo = -9999.0f;
        }

        float current_pitch = m_pitch + g_pitch_offset.load();
        float current_tempo = m_tempo + g_tempo_offset.load();

        if (current_tempo != m_last_tempo || current_pitch != m_last_pitch) {
            m_ctx->update_params(current_pitch, current_tempo);
            m_last_tempo = current_tempo;
            m_last_pitch = current_pitch;
        }

        if (chunk->get_sample_count() > 0) {
            m_ctx->process(chunk, m_accum, p_abort);
            if (m_accum.empty()) return false;
            chunk->set_data(m_accum.data(), m_accum.size() / m_channels, m_channels, m_sample_rate);
            return true;
        }
        
        return false;
    }

    void on_endofplayback(abort_callback & p_abort) override { drain(p_abort); flush(); }
    void on_endoftrack(abort_callback & p_abort) override { drain(p_abort); flush(); }
    
    void drain(abort_callback & p_abort) {
        if (m_ctx) {
            m_ctx->drain(m_accum, p_abort);
            if (!m_accum.empty()) {
                audio_chunk * out_chunk = insert_chunk(m_accum.size() / m_channels);
                out_chunk->set_data(m_accum.data(), m_accum.size() / m_channels, m_channels, m_sample_rate);
            }
        }
    }

    void flush() override {
        if (m_ctx) m_ctx->reset();
        m_last_pitch = -9999.0f;
        m_last_tempo = -9999.0f;
    }

    double get_latency() override { 
        if (m_ctx) return m_ctx->get_latency(m_sample_rate);
        return 0; 
    }
    bool need_track_change_mark() override { return false; }
};

class CDialogDSPPitchTempo : public CDialogImpl<CDialogDSPPitchTempo> {
public:
    enum { IDD = IDD_DSP_BASS };

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
        sliderPitch.SetRange(-24, 24);
        sliderPitch.SetPos((int)m_pitch);

        CTrackBarCtrl sliderTempo = GetDlgItem(IDC_TEMPO);
        sliderTempo.SetRange(-50, 100);
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

static dsp_factory_t<dsp_unified> g_dsp_unified_factory;
