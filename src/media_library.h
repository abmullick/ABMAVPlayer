#ifndef ABM_MEDIA_LIBRARY_H
#define ABM_MEDIA_LIBRARY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ABM_MediaLibraryPlayFn)(const wchar_t *path);

/* Opens the local Media Library manager. Scanning is performed on a worker
   thread and never blocks the main playback/UI thread. */
void ABM_MediaLibrary_Show(HWND owner, ABM_MediaLibraryPlayFn playCallback);

#ifdef __cplusplus
}
#endif
#endif
