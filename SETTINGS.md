# ABM AV Player — Application Settings Reference

This reference describes the controls currently exposed by the Windows application. Main Settings and Advanced Settings are separate surfaces.

## Open the settings windows

- **Main Settings:** Command Centre → Settings. Use it for general defaults, audio processing, torrent storage, bandwidth and colour theme.
- **Advanced Settings:** choose **Advanced…** in the main Settings window. Pages: Overview, Video & Decoding, Audio Engine, Video Output, Subtitles, Streaming & Network, Performance, Controls & Behaviour and Diagnostics.

A status line is not necessarily an editable control. Overview and diagnostic metrics are read-only.

## Main Settings

### General

| Option | Meaning |
|---|---|
| Volume | Default player volume, 0–100. |
| Playback speed | Default rate, 0.5x–2.0x. |
| Shuffle by default | Saves the preferred shuffle state. |
| Repeat current item | Saves the preferred repeat state. |

### Audio processing

| Option | Meaning |
|---|---|
| ReplayGain normalisation | Uses track-mode ReplayGain where supported and usable ReplayGain metadata exists. It is not a general volume leveller for untagged files. |
| Dynamic range compressor | Enables/disables the VLC compressor processing option. Effect availability depends on the VLC runtime/plugins. |

Audio processing preferences are also stored in `audio.ini`.

### Torrent & Storage

| Option | Meaning |
|---|---|
| Cache parent folder | Parent directory in which the torrent engine creates its `torrent-cache` working directory. |
| Permanent downloads folder | Destination for selected/completed torrent media; separate from the working cache. |
| Cache quota | Persistent cache quota, 1–4096 GiB; default 20 GiB. |
| Browse / Open folder | Select or open the configured directory in File Explorer. |

A new installation uses the Windows Downloads known folder as the basis for `<Downloads>\\ABM AV Player\\torrent-cache`. Existing installations with a legacy cache retain that location unless changed. Changing the path does not move existing data. If the torrent engine has started, restart ABM AV Player to initialise it with the new cache path.

### Network

Download and upload limits constrain torrent transfer rates in KiB/s. Enter `0` for unlimited. A limit does not guarantee throughput; peers, trackers, routing and remote upload capacity remain external factors.

### Updates

**Check for updates** checks the latest published GitHub Release. The updater expects `ABM-AV-Player-Setup.exe`, downloads over HTTPS and verifies SHA-256 before launching. See [RELEASES.md](RELEASES.md).

### Color Theme

Seven presets: Midnight Teal, Arctic Blue, Royal Violet, Ruby Rose, Sunset Amber, Emerald and Golden Yellow. The palette applies immediately to ABM-owned interface surfaces and persists in `%APPDATA%\\ABMAVPlayer\\theme.ini`. Provider websites keep their own styling.

## Advanced Settings

### Overview

Read-only live summary of playback and video dimensions, volume/speed/shuffle, playlist/recent counts and position, audio output-engine preference, caching targets, decoder/frame options, subtitle size/bold/delay, CPU count, installed RAM and process working set.

### Video & Decoding

| Choice | Meaning | Activation |
|---|---|---|
| Automatic | LibVLC selects an available decoder. | Save and restart. |
| Software decoding | Requests CPU decoding. | Save and restart. |
| D3D11VA | Requests Direct3D 11 Video Acceleration. | Save and restart. |

Saved under `[Video] HardwareAcceleration`. `ABM_VLC_HW` environment override takes precedence. Hardware support depends on codec/profile, VLC build, GPU and driver.

### Audio Engine

| Choice | Meaning | Activation |
|---|---|---|
| Automatic | LibVLC chooses the output module. | Save and restart. |
| WASAPI | Requests Windows Audio Session API output. | Save and restart. |
| DirectSound | Requests DirectSound output. | Save and restart. |

Saved under `[Audio] OutputEngine`. This is not an exclusive-mode/bit-perfect option or a new device picker.

### Video Output

Aspect ratio: Original, 16:9, 4:3 or 21:9. Saved under `[Video] AspectRatio` and applied through LibVLC. It affects display geometry, not source media. The status area reports video window availability, decoded dimensions and client area. Per-monitor calibration, full colour management and every renderer-specific scaling option are not exposed.

### Subtitles

| Control | Values | Meaning |
|---|---|---|
| Timing | −50 ms / +50 ms / Reset | Adjusts subtitle delay in 50 ms steps, within ±10 seconds. |
| Font size | 10 / 13 / 16 / 19 | LibVLC relative subtitle font-size values. |
| Text colour | White / Yellow / Cyan / Green | Supported preset colours. |
| Bold | On / Off | Requests bold text. |
| Outline | Off / Thin / Normal / Thick | Outline thickness. |
| Vertical position | Default / 10 / 25 / 50 px | Subtitle bottom margin. |

Appearance options are attached to newly opened media; embedded subtitle styling may override them. Timing also updates the current player when possible.

### Streaming & Network

Network, file and live caching each cycle through 300, 1,000, 2,000 and 5,000 ms. These LibVLC buffering targets are passed to newly opened media. More buffering can absorb jitter but increase startup delay/latency; less buffering can reduce delay but increase interruptions. Torrent quota and torrent bandwidth limits are configured in the main Settings window.

### Performance

| Control | Values | Effect |
|---|---|---|
| Decoder threads | Automatic / 2 / 4 / 8 | LibVLC decoder worker-thread selection/cap for newly opened media. |
| Drop late frames | Enabled / Disabled | Permits late frames to be dropped for responsiveness. |
| Skip frames | Enabled / Disabled | Permits frame skipping when decoding falls behind; visual quality may be reduced. |

These are not global CPU caps and do not guarantee better performance. Reopen media after changing them.

### Controls & Behaviour

- Confirm before exiting ABM AV Player.
- Confirm before clearing the playlist; source files are not deleted.
- Double-click video to toggle fullscreen.

Each preference is saved immediately under `[Controls]`.

### Diagnostics

Live metrics include application version/PID, logical processors, installed/available RAM, system memory load, process working set/page-file usage, playback state, video dimensions, playlist count and recent count. **Generate report** writes a timestamped local TXT report and loads it into the read-only, scrollable preview. **Open data folder** opens the directory. No automatic upload occurs; review the report before sharing.

## Configuration files

| File | Purpose |
|---|---|
| `%APPDATA%\\ABMAVPlayer\\settings.ini` | General, torrent, video, audio-engine, subtitle, streaming, performance and controls preferences. |
| `%APPDATA%\\ABMAVPlayer\\audio.ini` | Audio processing preferences. |
| `%APPDATA%\\ABMAVPlayer\\theme.ini` | Selected ABM interface theme preset. |

Prefer the UI over manual INI editing. If editing manually, close the application and make a backup. Invalid values may be normalised to defaults.

## Troubleshooting

- Hardware choice not active: restart and check whether `ABM_VLC_HW` overrides it.
- Audio output change not active: restart ABM AV Player.
- New torrent cache path not active: restart the application; the engine initialises its cache path at startup.
- Excessive buffering: lower the relevant cache target, noting that unstable networks may interrupt more often.
- Playback stutter: compare decoder modes and change one Performance option at a time; reopen media.
- Empty diagnostic preview: generate again; otherwise use Open data folder to inspect the saved TXT report.
