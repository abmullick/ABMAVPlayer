#pragma once
#define IDI_APP_ICON 101

#ifdef __cplusplus
extern "C" {
#endif
HICON ABM_GetTitleBarIcon(void);
void ABM_ApplyTitleBarIcon(HWND window);
#ifdef __cplusplus
}
#endif
