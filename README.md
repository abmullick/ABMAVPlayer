# ABM AV Player\n\n**Product Owner:** Abhisek Basu Mullick  \n**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.\n\nThis repository is publicly viewable for reference only. Public availability does not grant permission to copy, modify, redistribute, sublicense, or commercially exploit this software. See [LICENSE](LICENSE) and [COPYRIGHT.md](COPYRIGHT.md).

A native Windows desktop audio/video player written in C.

## Architecture
- Application and windowing: C + Win32 API
- Interface rendering: Direct2D (antialiased shapes, surfaces and controls) + DirectWrite (Segoe UI text layout and ClearType-quality glyph rendering)
- Media playback, demuxing and codecs: LibVLC
- Video output: LibVLC embedded in a Win32 child window
- Audio output: LibVLC's Windows audio backend

LibVLC handles media decoding and device output. This project owns the desktop interface and playback controls; it is not a kernel audio/video driver.

## Current features
- Open local audio and video files
- Editable playlist: add, remove, reorder, clear, load and save M3U playlists
- Audio: MP3, WAV, FLAC, AAC, M4A and OGG (codec support depends on installed VLC)
- Video: MP4, MKV, AVI, MOV, WMV and WebM (codec support depends on installed VLC)
- Play/pause, stop, seek and volume
- Dark custom Windows interface rendered with Direct2D and DirectWrite
- DirectWrite typography and antialiased Direct2D controls, panels and interactive surfaces
- Network receiver discovery through LibVLC (Google Cast / Chromecast where the installed VLC build provides the renderer module)
- Select a receiver before playback, or switch the current item to a discovered TV from the Playback Output selector
- Chromecast renderer can automatically transcode unsupported streams when supported by the installed VLC build
- Unicode file paths converted to UTF-8 for LibVLC

## Requirements
- Windows 11 x64
- Visual Studio Build Tools / Visual Studio with **Desktop development with C++** and Windows 10/11 SDK
- Windows SDK
- 64-bit VLC media player
- Matching 64-bit LibVLC SDK (headers and import library), version 3.0 or later for renderer discovery

Download VLC: https://www.videolan.org/vlc/
LibVLC SDK: https://www.videolan.org/vlc/libvlc.html

All components must use the same architecture (x64). For casting, use a full VLC installation with its renderer-discovery and Chromecast plugins; minimal VLC runtime bundles may omit them.

## Build
1. Install 64-bit VLC.
2. Extract the matching 64-bit LibVLC SDK, for example to C:\vlc-sdk.
3. Open **x64 Native Tools Command Prompt for VS 2022**.
4. Set the SDK path: set VLC_SDK=C:\vlc-sdk
5. Run build.bat from the repository root. The build compiles `src\\player.c` and `src\\ui_d2d.c`, linking `d2d1.lib` and `dwrite.lib` from the Windows SDK.

The executable is written to build\ABMAVPlayer.exe.

## Run
Run run.bat. It defaults to the standard VLC installation directory:
C:\Program Files\VideoLAN\VLC

If VLC is installed elsewhere, set VLC_DIR before running.

## Known limitations of this first version
- Playlist capacity is currently limited to 256 entries; no persistent media-library database yet.
- No subtitle/audio-track selection or playback-speed control yet.
- No exclusive-mode / bit-perfect audio path yet.
- No hardware-acceleration controls yet.
- Codec availability and network-casting capabilities depend on the installed VLC build and its plugins.
- For casting, allow ABM AV Player/VLC through Windows Defender Firewall on private networks and ensure multicast/mDNS discovery is not blocked.
- Switching the output recreates the LibVLC media player and restarts the current playlist item; seek-position restoration and receiver compatibility should be tested with the target TV.
- Source has not yet been compiled or tested on the user's HP Envy.

## Google Cast / network casting
1. Click **Find TVs** and wait for network receivers to appear.
2. Select your Google TV / Chromecast receiver from the list.
3. Open the media file. LibVLC's renderer module handles the Cast session and can transcode streams the receiver cannot decode directly, depending on the VLC build and available encoder plugins.
4. Keep the laptop and TV on the same non-guest Wi-Fi/LAN. Router client isolation, VPNs, multicast filtering, or Windows Firewall rules can prevent discovery or streaming.

The application uses LibVLC's renderer-discovery and renderer-selection APIs. It does not bundle a separate FFmpeg executable; codec conversion is delegated to the VLC engine and its installed plugins. Transcoding is not guaranteed for every codec, subtitle format, or receiver.

## Roadmap
1. Robust playback-state handling and media metadata
2. Drag-and-drop playlist editing and persistent media-library database
3. Subtitle and audio-track selection
4. Aspect ratio and playback speed
5. Audio-device selection and advanced output
6. Equalizer and DSP
7. Library scanning and album art
