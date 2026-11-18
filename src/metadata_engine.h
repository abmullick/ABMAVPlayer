#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum ABM_METADATA_MEDIA_TYPE {
    ABM_METADATA_MOVIE = 1,
    ABM_METADATA_TV = 2
} ABM_METADATA_MEDIA_TYPE;

/* Keyless metadata adapters. Network calls are synchronous and must run on
   Home Theatre worker threads, never from a window procedure. */
BOOL ABM_Metadata_Search(const wchar_t *query, ABM_METADATA_MEDIA_TYPE type,
                        wchar_t *json, size_t capacity);
BOOL ABM_Metadata_GetDetails(ABM_METADATA_MEDIA_TYPE type, int providerId,
                             wchar_t *json, size_t capacity);
BOOL ABM_Metadata_GetRecommendations(ABM_METADATA_MEDIA_TYPE type, int providerId,
                                     wchar_t *json, size_t capacity);
BOOL ABM_Metadata_GetSeasonDetails(int tvmazeId, int seasonNumber,
                                    wchar_t *json, size_t capacity);
BOOL ABM_Metadata_DownloadImage(const wchar_t *url, const wchar_t *destination,
                                 DWORD timeoutMs);
BOOL ABM_Metadata_FindAlbumArtwork(const wchar_t *artist, const wchar_t *album,
                                   wchar_t *imageUrl, size_t capacity);
const wchar_t *ABM_Metadata_ProviderName(ABM_METADATA_MEDIA_TYPE type);

#ifdef __cplusplus
}
#endif
