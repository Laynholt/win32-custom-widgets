#pragma once

#include <wcw/Style.h>

#include <windows.h>

namespace wcw {

bool Initialize(HINSTANCE instance);
void Shutdown();

void SetTheme(const Theme& theme);
Theme GetTheme();

bool SetStyleOverride(HWND window, const StyleOverride& style);
bool ClearStyleOverride(HWND window);

} // namespace wcw
