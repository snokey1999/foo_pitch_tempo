#include "stdafx.h"
#include "tolk_wrapper.h"

typedef void (*Tolk_Load_Func)();
typedef void (*Tolk_Unload_Func)();
typedef bool (*Tolk_Output_Func)(const wchar_t* str, bool interrupt);
typedef void (*Tolk_TrySAPI_Func)(bool trySAPI);

static HMODULE g_hTolkModule = nullptr;
static Tolk_Load_Func g_Tolk_Load = nullptr;
static Tolk_Unload_Func g_Tolk_Unload = nullptr;
static Tolk_Output_Func g_Tolk_Output = nullptr;
static Tolk_TrySAPI_Func g_Tolk_TrySAPI = nullptr;

void TolkWrapper_Init(HMODULE hComponent) {
    if (g_hTolkModule) return;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hComponent, path, MAX_PATH);
    std::wstring basePath(path);
    size_t pos = basePath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        basePath = basePath.substr(0, pos);
    }
    
    // Path inside fb2k component folder: components\foo_pitch_tempo\tolk\Tolk.dll
    // wait, fb2k packages extract to `profile\user-components\foo_pitch_tempo\`.
    // And dlls inside root. So we should probably check if it's there.
    std::wstring tolkDir = basePath + L"\\tolk";
    std::wstring tolkPath = tolkDir + L"\\Tolk.dll";

    SetDllDirectoryW(tolkDir.c_str());
    g_hTolkModule = LoadLibraryW(tolkPath.c_str());

    if (g_hTolkModule) {
        g_Tolk_Load = (Tolk_Load_Func)GetProcAddress(g_hTolkModule, "Tolk_Load");
        g_Tolk_Unload = (Tolk_Unload_Func)GetProcAddress(g_hTolkModule, "Tolk_Unload");
        g_Tolk_Output = (Tolk_Output_Func)GetProcAddress(g_hTolkModule, "Tolk_Output");
        g_Tolk_TrySAPI = (Tolk_TrySAPI_Func)GetProcAddress(g_hTolkModule, "Tolk_TrySAPI");

        if (g_Tolk_Load) g_Tolk_Load();
        if (g_Tolk_TrySAPI) g_Tolk_TrySAPI(true);
    }
    
    SetDllDirectoryW(nullptr);
}

void TolkWrapper_Uninit() {
    g_hTolkModule = nullptr;
}

void TolkWrapper_Output(const std::wstring& text) {
    if (g_Tolk_Output) {
        g_Tolk_Output(text.c_str(), true);
    }
}
