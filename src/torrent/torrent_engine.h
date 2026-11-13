#pragma once
#include <windows.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ABM_TORRENT_TEXT_CAP 512
typedef struct ABM_TorrentTaskInfo {
    uint64_t task_id;
    double progress;
    int64_t total_bytes, downloaded_bytes, uploaded_bytes;
    int download_rate, upload_rate, peer_count, state, has_metadata, file_count;
    int known_peers, connect_candidates, connections, seeds;
    int tracker_count, tracker_verified, tracker_failures;
    int announcing_to_dht, announcing_to_trackers;
    int dht_running, is_private;
    wchar_t tracker_diagnostic[512];
    wchar_t name[ABM_TORRENT_TEXT_CAP];
    wchar_t error[ABM_TORRENT_TEXT_CAP];
} ABM_TorrentTaskInfo;
enum ABM_TorrentState { ABM_TORRENT_QUEUED=0, ABM_TORRENT_CHECKING=1, ABM_TORRENT_DOWNLOADING=2, ABM_TORRENT_FINISHED=3, ABM_TORRENT_SEEDING=4, ABM_TORRENT_PAUSED=5, ABM_TORRENT_ERROR=6, ABM_TORRENT_UNKNOWN=7 };
__declspec(dllexport) BOOL WINAPI ABM_Torrent_Initialize(const wchar_t *data_directory, const wchar_t *download_directory);
__declspec(dllexport) void WINAPI ABM_Torrent_Shutdown(void);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_AddMagnet(const wchar_t *magnet_uri, uint64_t *task_id, wchar_t *error, unsigned int error_capacity);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_AddFile(const wchar_t *torrent_path, uint64_t *task_id, wchar_t *error, unsigned int error_capacity);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_GetTask(uint64_t task_id, ABM_TorrentTaskInfo *info);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_GetFile(uint64_t task_id, int file_index, wchar_t *path, unsigned int path_capacity, int64_t *file_size);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_SelectFile(uint64_t task_id, int file_index);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_Pause(uint64_t task_id);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_Resume(uint64_t task_id);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_Remove(uint64_t task_id, BOOL delete_downloaded_data);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_SetRateLimits(int download_bytes_per_second, int upload_bytes_per_second);
/* Persistent on-disk cache management. Quota is in bytes; default is 20 GiB. */
__declspec(dllexport) BOOL WINAPI ABM_Torrent_GetCacheInfo(uint64_t *used_bytes, uint64_t *limit_bytes);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_SetCacheLimit(uint64_t limit_bytes, uint64_t *used_bytes);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_ClearCache(uint64_t *freed_bytes, uint64_t *remaining_bytes);
/* Verified-piece streaming. The loopback API binds only to 127.0.0.1; the separate
   LAN API binds to one private IPv4 interface and only accepts same-subnet clients.
   Both return per-stream random bearer URLs. */
__declspec(dllexport) BOOL WINAPI ABM_Torrent_SetReadWindow(uint64_t task_id, int file_index, int64_t offset, int64_t length, int ahead_bytes);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_WaitForRange(uint64_t task_id, int file_index, int64_t offset, int64_t length, unsigned int timeout_ms);
__declspec(dllexport) BOOL WINAPI ABM_Torrent_StartHttpStream(uint64_t task_id, int file_index, wchar_t *url, unsigned int url_capacity, wchar_t *error, unsigned int error_capacity);
/* Private-LAN variant for an authorized TV receiver on the same subnet. */
__declspec(dllexport) BOOL WINAPI ABM_Torrent_StartLanHttpStream(uint64_t task_id, int file_index, wchar_t *url, unsigned int url_capacity, wchar_t *error, unsigned int error_capacity);
__declspec(dllexport) void WINAPI ABM_Torrent_StopHttpStream(void);
#ifdef __cplusplus
}
#endif
