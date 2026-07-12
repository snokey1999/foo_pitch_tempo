#pragma once
#include "resource.h"
#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atomic>
#include <vector>
#include <string>

extern bool is_openal_active();
extern void save_openal_spatial_settings_to_dsp();
extern void sync_openal_spatial_settings_from_dsp();

extern std::atomic<float> g_pitch_offset;
extern std::atomic<float> g_tempo_offset;

// Spatial overrides
extern std::atomic<int> g_spatial_override_preset;
extern std::atomic<float> g_spatial_override_rotation;
extern std::atomic<float> g_spatial_override_elevation;
extern std::atomic<float> g_spatial_override_distance;
extern std::atomic<float> g_spatial_override_mix;
extern std::atomic<float> g_spatial_override_azimuth;
extern std::atomic<float> g_spatial_override_reverb;

extern void save_current_track_settings();
extern void SpeakMsg(const char* utf8_text);

class CAccessibleParamsDialog : public CDialogImpl<CAccessibleParamsDialog> {
public:
    enum { IDD = IDD_ACCESSIBLE_PARAMS };

    BEGIN_MSG_MAP(CAccessibleParamsDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
        COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
        COMMAND_HANDLER(IDC_COMBO_PARAM_TYPE, CBN_SELCHANGE, OnTypeSelChange)
        COMMAND_HANDLER(IDC_COMBO_PARAM_VALUE, CBN_SELCHANGE, OnValueSelChange)
    END_MSG_MAP()

private:
    CComboBox m_comboType;
    CComboBox m_comboValue;

    // Backup state for Cancel
    float m_orig_pitch;
    float m_orig_tempo;
    int   m_orig_preset;
    float m_orig_rotation;
    float m_orig_elevation;
    float m_orig_distance;
    float m_orig_mix;
    float m_orig_azimuth;
    float m_orig_reverb;

    struct ValueOpt {
        std::wstring name;
        float value;
    };
    std::vector<ValueOpt> m_currentValues;
    std::vector<int> m_availableTypes;

    enum ParamType {
        PARAM_PITCH = 0,
        PARAM_FINE_PITCH,
        PARAM_SPEED,
        PARAM_FINE_SPEED,
        PARAM_SPATIAL_PRESET,
        PARAM_SPATIAL_ROTATION,
        PARAM_SPATIAL_ELEVATION,
        PARAM_SPATIAL_DISTANCE,
        PARAM_SPATIAL_MIX,
        PARAM_SPATIAL_AZIMUTH,
        PARAM_SPATIAL_REVERB,
        PARAM_COUNT
    };

    void BackupState() {
        m_orig_pitch = g_pitch_offset.load();
        m_orig_tempo = g_tempo_offset.load();
        m_orig_preset = g_spatial_override_preset.load();
        m_orig_rotation = g_spatial_override_rotation.load();
        m_orig_elevation = g_spatial_override_elevation.load();
        m_orig_distance = g_spatial_override_distance.load();
        m_orig_mix = g_spatial_override_mix.load();
        m_orig_azimuth = g_spatial_override_azimuth.load();
        m_orig_reverb = g_spatial_override_reverb.load();
    }

    void RestoreState() {
        g_pitch_offset.store(m_orig_pitch);
        g_tempo_offset.store(m_orig_tempo);
        g_spatial_override_preset.store(m_orig_preset);
        g_spatial_override_rotation.store(m_orig_rotation);
        g_spatial_override_elevation.store(m_orig_elevation);
        g_spatial_override_distance.store(m_orig_distance);
        g_spatial_override_mix.store(m_orig_mix);
        g_spatial_override_azimuth.store(m_orig_azimuth);
        g_spatial_override_reverb.store(m_orig_reverb);
    }

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
        m_comboType = GetDlgItem(IDC_COMBO_PARAM_TYPE);
        m_comboValue = GetDlgItem(IDC_COMBO_PARAM_VALUE);

        if (is_openal_active()) {
            sync_openal_spatial_settings_from_dsp();
        }

        BackupState();

        m_availableTypes.push_back(PARAM_PITCH);
        m_comboType.AddString(L"音高 (Pitch)");
        m_availableTypes.push_back(PARAM_FINE_PITCH);
        m_comboType.AddString(L"微调音高 (Fine Pitch)");
        m_availableTypes.push_back(PARAM_SPEED);
        m_comboType.AddString(L"速度 (Speed)");
        m_availableTypes.push_back(PARAM_FINE_SPEED);
        m_comboType.AddString(L"微调速度 (Fine Speed)");
        
        if (is_openal_active()) {
            m_availableTypes.push_back(PARAM_SPATIAL_PRESET);
            m_comboType.AddString(L"空间 - 预设环境 (Spatial Preset)");
            m_availableTypes.push_back(PARAM_SPATIAL_ROTATION);
            m_comboType.AddString(L"空间 - 旋转速度 (Spatial Rotation)");
            m_availableTypes.push_back(PARAM_SPATIAL_ELEVATION);
            m_comboType.AddString(L"空间 - 垂直仰角 (Spatial Elevation)");
            m_availableTypes.push_back(PARAM_SPATIAL_DISTANCE);
            m_comboType.AddString(L"空间 - 声源距离 (Spatial Distance)");
            m_availableTypes.push_back(PARAM_SPATIAL_MIX);
            m_comboType.AddString(L"空间 - 干湿混合 (Spatial Mix)");
            m_availableTypes.push_back(PARAM_SPATIAL_AZIMUTH);
            m_comboType.AddString(L"空间 - 水平方位角 (Spatial Azimuth)");
            m_availableTypes.push_back(PARAM_SPATIAL_REVERB);
            m_comboType.AddString(L"空间 - 混响强度 (Spatial Reverb Gain)");
        }

        m_comboType.SetCurSel(0);
        PopulateValues();

        CenterWindow(GetParent());
        return TRUE;
    }

    void PopulateValues() {
        int typeIndex = m_comboType.GetCurSel();
        if (typeIndex < 0 || typeIndex >= (int)m_availableTypes.size()) return;
        
        int paramType = m_availableTypes[typeIndex];
        m_comboValue.ResetContent();
        m_currentValues.clear();

        int selectedIndex = 0;
        float currentValue = 0;

        switch (paramType) {
            case PARAM_PITCH:
                currentValue = g_pitch_offset.load();
                for (int i = -24; i <= 24; i++) {
                    wchar_t buf[64];
                    if (i == 0) wcscpy(buf, L"0 半音 (原调)");
                    else swprintf(buf, 64, L"%+d 半音", i);
                    m_currentValues.push_back({buf, (float)i});
                    if (abs((float)i - currentValue) < 0.5f) selectedIndex = (int)m_currentValues.size() - 1;
                }
                break;

            case PARAM_FINE_PITCH:
                currentValue = g_pitch_offset.load();
                {
                    float fractional = currentValue - roundf(currentValue);
                    for (int i = -9; i <= 9; i++) {
                        float v = i * 0.1f;
                        wchar_t buf[64];
                        if (i == 0) wcscpy(buf, L"0 (无微调)");
                        else swprintf(buf, 64, L"%+.1f 半音", (double)v);
                        m_currentValues.push_back({buf, v});
                        if (abs(v - fractional) < 0.05f) selectedIndex = (int)m_currentValues.size() - 1;
                    }
                }
                break;

            case PARAM_SPEED:
                currentValue = g_tempo_offset.load();
                for (int i = -50; i <= 100; i += 5) {
                    wchar_t buf[64];
                    if (i == 0) wcscpy(buf, L"0% (原速)");
                    else swprintf(buf, 64, L"%+d%%", i);
                    m_currentValues.push_back({buf, (float)i});
                    if (abs((float)i - currentValue) < 2.5f) selectedIndex = (int)m_currentValues.size() - 1;
                }
                break;

            case PARAM_FINE_SPEED:
                currentValue = g_tempo_offset.load();
                {
                    float remainder = fmodf(currentValue, 5.0f);
                    if (remainder > 2.5f) remainder -= 5.0f;
                    else if (remainder < -2.5f) remainder += 5.0f;
                    
                    for (int i = -4; i <= 4; i++) {
                        float v = (float)i;
                        wchar_t buf[64];
                        if (i == 0) wcscpy(buf, L"0 (无微调)");
                        else swprintf(buf, 64, L"%+.0f%%", (double)v);
                        m_currentValues.push_back({buf, v});
                        if (abs(v - remainder) < 0.5f) selectedIndex = (int)m_currentValues.size() - 1;
                    }
                }
                break;

            case PARAM_SPATIAL_PRESET:
                currentValue = (float)g_spatial_override_preset.load();
                if (currentValue < 0) currentValue = 7;
                {
                    const wchar_t* names[] = {
                        L"0 - 无混响 (纯HRTF)", L"1 - 通用", L"2 - 房间", L"3 - 客厅",
                        L"4 - 浴室", L"5 - 石头房间", L"6 - 礼堂", L"7 - 音乐厅",
                        L"8 - 洞穴", L"9 - 森林", L"10 - 山脉", L"11 - 水下",
                        L"12 - 城市", L"13 - 地铁", L"14 - 城堡大厅", L"15 - 体育馆",
                        L"16 - 大管道", L"17 - 天堂", L"18 - 地狱", L"19 - 教堂", L"20 - 迷幻"
                    };
                    for (int i = 0; i < 21; i++) {
                        m_currentValues.push_back({names[i], (float)i});
                        if ((float)i == currentValue) selectedIndex = i;
                    }
                }
                break;

            case PARAM_SPATIAL_ROTATION:
                currentValue = g_spatial_override_rotation.load();
                if (currentValue < 0) currentValue = 50.0f;
                {
                    m_currentValues.push_back({L"0 秒/圈 (固定不转)", 0.0f});
                    for (int i = 1; i <= 60; i++) {
                        wchar_t buf[64];
                        swprintf(buf, 64, L"%d 秒/圈", i);
                        m_currentValues.push_back({buf, (float)i});
                        if (abs((float)i - currentValue) < 0.5f) selectedIndex = (int)m_currentValues.size() - 1;
                    }
                }
                break;

            case PARAM_SPATIAL_ELEVATION:
            case PARAM_SPATIAL_AZIMUTH:
                currentValue = (paramType == PARAM_SPATIAL_ELEVATION) ? g_spatial_override_elevation.load() : g_spatial_override_azimuth.load();
                if (currentValue <= -990.0f) currentValue = 0.0f;
                {
                    int min_val = (paramType == PARAM_SPATIAL_ELEVATION) ? -90 : -180;
                    int max_val = (paramType == PARAM_SPATIAL_ELEVATION) ? 90 : 180;
                    for (int i = min_val; i <= max_val; i++) {
                        wchar_t buf[64];
                        swprintf(buf, 64, L"%+d°", i);
                        if (i == 0) m_currentValues.push_back({L"0° (正前方/平齐)", (float)i});
                        else m_currentValues.push_back({buf, (float)i});
                        
                        if (abs((float)i - currentValue) < 0.5f) selectedIndex = (int)m_currentValues.size() - 1;
                    }
                }
                break;

            case PARAM_SPATIAL_DISTANCE:
                currentValue = g_spatial_override_distance.load();
                if (currentValue < 0) currentValue = 3.0f;
                for (int i = 1; i <= 100; i++) {
                    float v = i * 0.1f;
                    wchar_t buf[64];
                    swprintf(buf, 64, L"%.1f", (double)v);
                    m_currentValues.push_back({buf, v});
                    if (abs(v - currentValue) < 0.05f) selectedIndex = (int)m_currentValues.size() - 1;
                }
                break;

            case PARAM_SPATIAL_MIX:
                currentValue = g_spatial_override_mix.load();
                if (currentValue < 0) currentValue = 100.0f;
                for (int i = 0; i <= 100; i++) {
                    wchar_t buf[64];
                    swprintf(buf, 64, L"%d%%", i);
                    m_currentValues.push_back({buf, (float)i});
                    if (abs((float)i - currentValue) < 0.5f) selectedIndex = (int)m_currentValues.size() - 1;
                }
                break;
                
            case PARAM_SPATIAL_REVERB:
                currentValue = g_spatial_override_reverb.load();
                if (currentValue < 0) currentValue = 1.0f;
                for (int i = 0; i <= 200; i++) {
                    float v = i * 0.01f;
                    wchar_t buf[64];
                    swprintf(buf, 64, L"%.2f", (double)v);
                    m_currentValues.push_back({buf, v});
                    if (abs(v - currentValue) < 0.005f) selectedIndex = (int)m_currentValues.size() - 1;
                }
                break;
        }

        for (const auto& opt : m_currentValues) {
            m_comboValue.AddString(opt.name.c_str());
        }
        m_comboValue.SetCurSel(selectedIndex);
    }

    LRESULT OnTypeSelChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
        PopulateValues();
        return 0;
    }

    LRESULT OnValueSelChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
        int typeIndex = m_comboType.GetCurSel();
        int valIndex = m_comboValue.GetCurSel();
        if (valIndex >= 0 && valIndex < (int)m_currentValues.size() && typeIndex >= 0 && typeIndex < (int)m_availableTypes.size()) {
            int paramType = m_availableTypes[typeIndex];
            float v = m_currentValues[valIndex].value;
            
            switch (paramType) {
                case PARAM_PITCH: 
                case PARAM_FINE_PITCH:
                    {
                        float current = g_pitch_offset.load();
                        if (paramType == PARAM_PITCH) {
                            float fractional = current - roundf(current);
                            g_pitch_offset.store(v + fractional);
                        } else {
                            float integer = roundf(current);
                            g_pitch_offset.store(integer + v);
                        }
                    }
                    break;
                case PARAM_SPEED: 
                case PARAM_FINE_SPEED:
                    {
                        float current = g_tempo_offset.load();
                        if (paramType == PARAM_SPEED) {
                            float remainder = fmodf(current, 5.0f);
                            if (remainder > 2.5f) remainder -= 5.0f;
                            else if (remainder < -2.5f) remainder += 5.0f;
                            g_tempo_offset.store(v + remainder);
                        } else {
                            float base = current - fmodf(current, 5.0f);
                            if (fmodf(current, 5.0f) > 2.5f) base += 5.0f;
                            else if (fmodf(current, 5.0f) < -2.5f) base -= 5.0f;
                            g_tempo_offset.store(base + v);
                        }
                    }
                    break;
                case PARAM_SPATIAL_PRESET: g_spatial_override_preset.store((int)v); break;
                case PARAM_SPATIAL_ROTATION: g_spatial_override_rotation.store(v); break;
                case PARAM_SPATIAL_ELEVATION: g_spatial_override_elevation.store(v); break;
                case PARAM_SPATIAL_DISTANCE: g_spatial_override_distance.store(v); break;
                case PARAM_SPATIAL_MIX: g_spatial_override_mix.store(v); break;
                case PARAM_SPATIAL_AZIMUTH: g_spatial_override_azimuth.store(v); break;
                case PARAM_SPATIAL_REVERB: g_spatial_override_reverb.store(v); break;
            }
        }
        return 0;
    }

    LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
        if (wID == IDCANCEL) {
            RestoreState();
            SpeakMsg("参数已还原");
        } else if (wID == IDOK) {
            save_openal_spatial_settings_to_dsp();
            save_current_track_settings();
            SpeakMsg("参数已保存");
        }
        EndDialog(wID);
        return 0;
    }
};
