# ABM AV Player — Application Settings

The Command Centre's **Settings** item opens a dedicated Windows settings window. Its dark navy background, teal/blue accents and Segoe UI typography are designed to match the main ABM AV Player interface.

## Open Settings

1. Open the Command Centre from the main player.
2. Select **Settings**.
3. Change the required options.
4. Select **Save settings**. Use **Close** to dismiss the window.

## General

| Option | Behaviour |
|---|---|
| Volume | Sets the player's volume from 0 to 100. |
| Playback speed | Sets the playback rate from 0.5x to 2.0x. |
| Shuffle by default | Saves the preferred shuffle state. |
| Repeat current item | Saves the preferred repeat state. |

## Audio

| Option | Behaviour |
|---|---|
| ReplayGain normalisation | Enables/disables ReplayGain processing where supported by media metadata and the VLC runtime. |
| Dynamic range compressor | Enables/disables the audio dynamic-range compressor. |

Audio processing preferences are stored in `audio.ini` under the application data directory.

## Torrent & Storage

| Option | Behaviour |
|---|---|
| Cache parent folder | Selects the parent directory for torrent data. The engine creates and uses a `torrent-cache` subfolder within it. |
| Browse | Opens the Windows folder picker. |
| Open folder | Opens the currently selected cache parent in File Explorer. |
| Cache quota | Sets the persistent cache quota in GiB, from 1 to 4096. The default is 20 GiB. |

### Default location and existing installations

On a new installation, the cache parent is created under the current Windows user's **Downloads known folder**:

```text
<Windows Downloads>\ABM AV Player\torrent-cache
```

The Windows Known Folder API is used, so redirected Downloads folders (including folders redirected to another drive or a supported cloud location) are respected.

If an existing installation already has the legacy cache under `%APPDATA%\ABMAVPlayer\torrent-cache`, ABM AV Player retains that cache location rather than silently switching to an empty folder.

### Changing the cache location

1. Open **Settings** → **Torrent & Storage**.
2. Select **Browse** and choose the new cache parent folder.
3. Select **Save settings**.
4. If Torrent Stream is already running, close and restart ABM AV Player before using the new location.

Changing the configured location does **not** automatically move or copy existing cache data. Existing files remain in the previous folder. If you need to preserve and reuse those pieces, keep the old cache folder or arrange a safe migration while the torrent engine is stopped.

The currently running torrent engine continues using the cache path with which it was initialized. The new path is used on the next application launch.

## Network

| Option | Behaviour |
|---|---|
| Download limit | Maximum torrent download rate in KiB/s. |
| Upload limit | Maximum torrent upload rate in KiB/s. |

Enter `0` for unlimited transfer rate. Limits are saved and applied when the torrent engine starts; if the engine is already running, Save settings applies the new limits to that session.

## Updates

**Check for updates** checks the latest public GitHub release. The normal update workflow asks before downloading and verifies the installer SHA-256 digest before launching it.

## Configuration files

Application settings are persisted under:

```text
%APPDATA%\ABMAVPlayer\settings.ini
%APPDATA%\ABMAVPlayer\audio.ini
```

The first file stores player and torrent preferences, including the cache parent folder, cache quota and bandwidth limits. The second stores audio processing preferences.

## Troubleshooting

- **The new cache path is not active yet:** restart ABM AV Player if Torrent Stream was already initialized when you saved the setting.
- **The cache folder cannot be created:** choose a writable folder and verify that the target drive is available.
- **The cache appears empty after changing location:** the setting changes the storage root; it does not move existing files automatically. Check the previous cache folder.
- **Transfers are slower than expected:** verify that the download/upload limits are not set too low. A value of 0 removes the configured rate limit; it does not guarantee peer or network throughput.
