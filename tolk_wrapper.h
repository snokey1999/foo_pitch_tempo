#pragma once

#include <string>
#include <windows.h>

void TolkWrapper_Init(HMODULE hComponent);
void TolkWrapper_Uninit();
void TolkWrapper_Output(const std::wstring& text);
