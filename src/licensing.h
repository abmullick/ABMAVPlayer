#ifndef ABM_LICENSING_H
#define ABM_LICENSING_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* TRUE means a signed perpetual licence is valid or the 10-day trial is active. */
BOOL ABM_LicenseCheckAndPrompt(HINSTANCE instance, HWND owner);
#ifdef __cplusplus
}
#endif
#endif
