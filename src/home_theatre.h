#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*ABM_HomeTheatrePlayFn)(int playlistIndex);
void ABM_HomeTheatre_Show(HWND owner, const wchar_t *const *mediaPaths,
                          int mediaCount, ABM_HomeTheatrePlayFn playCallback);
#ifdef __cplusplus
}
#endif
