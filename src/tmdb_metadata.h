#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

/* TMDB v3 read-only metadata client. All network calls are synchronous and
   MUST be invoked from a worker thread, never from a window procedure. */
typedef enum ABM_TMDB_MEDIA_TYPE {
    ABM_TMDB_MOVIE = 1,
    ABM_TMDB_TV = 2,
    ABM_TMDB_PERSON = 3
} ABM_TMDB_MEDIA_TYPE;

BOOL ABM_TMDB_SetReadToken(const wchar_t *token);
BOOL ABM_TMDB_GetReadToken(wchar_t *token, size_t tokenCount);
BOOL ABM_TMDB_IsConfigured(void);
BOOL ABM_TMDB_Search(const wchar_t *query, ABM_TMDB_MEDIA_TYPE type,
                     wchar_t *jsonUtf8, size_t jsonCapacity);
BOOL ABM_TMDB_GetDetails(ABM_TMDB_MEDIA_TYPE type, int tmdbId,
                         wchar_t *jsonUtf8, size_t jsonCapacity);
BOOL ABM_TMDB_DownloadImage(const wchar_t *imagePath, const wchar_t *destination,
                            DWORD timeoutMs);
const wchar_t *ABM_TMDB_AttributionNotice(void);

#ifdef __cplusplus
}
#endif
