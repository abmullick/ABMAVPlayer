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
- Drag-and-drop media files into the player
- Persistent playlist library, recent-media history and saved volume/shuffle/repeat/speed preferences under `%APPDATA%\\ABMAVPlayer`
- Shuffle playback and repeat-current-track mode
- Playback speed presets from 0.5x to 2.0x (right-click the video)
- Audio-track selection, subtitle-track selection and external subtitle loading (right-click the video)
- LibVLC equalizer presets plus ABM 10-band graphic EQ curves (Flat, Bass boost, Vocal clarity, Treble boost)
- Optional ReplayGain track normalisation and dynamic-range compressor for newly opened media
- Basic media metadata dialog (title, artist, album and duration)
- Keyboard shortcuts: Space, Left/Right, Home, N/P, +/- and F11
- Dark custom Windows interface rendered with Direct2D and DirectWrite
- DirectWrite typography and antialiased Direct2D controls, panels and interactive surfaces
- Network receiver discovery through LibVLC (Google Cast / Chromecast and other renderer services exposed by the installed VLC build)
- Select a receiver before playback, or switch the current item to a discovered TV from the Playback Output selector
- Output switching queues a seek-position restore after the new output reaches Playing; paused playback is restored as paused
- If Cast setup or playback fails, the player attempts local playback; a Cast session that fails to resume is also returned to the PC
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
5. Run `build.bat` from the repository root. It compiles `src\\player.c`, `src\\ui_d2d.cpp` and `src\\audio_visualizer.cpp`, then links the Windows SDK and LibVLC libraries.

The executable is written to build\ABMAVPlayer.exe.

## Run
Run run.bat. It defaults to the standard VLC installation directory:
C:\Program Files\VideoLAN\VLC

If VLC is installed elsewhere, set VLC_DIR before running.

## Known limitations
- Playlist capacity is currently limited to 256 entries; no persistent media-library database yet.
- No persistent media-library database or recent-history view yet.
- No exclusive-mode / bit-perfect audio path yet.
- Hardware decoding now uses LibVLC automatic selection by default. Set `ABM_VLC_HW=none` to force software decoding, or `ABM_VLC_HW=d3d11va` to request D3D11VA when supported by the installed VLC build and GPU driver.
- ABM EQ profiles use LibVLC's fixed 10-band graphic equalizer; a fully parametric EQ with adjustable centre frequency/Q and a true-peak brickwall limiter are not implemented yet.
- ReplayGain requires suitable track metadata/tags. The compressor is a dynamic-range effect, not a guaranteed true-peak limiter; availability depends on the installed VLC compressor plugin.
- The visualizer still captures the Windows default render mix, so audio from other applications may appear in it. Process-specific loopback capture is not implemented yet.
- Codec availability and network-casting capabilities depend on the installed VLC build and its plugins.
- For casting, allow ABM AV Player/VLC through Windows Defender Firewall on private networks and ensure multicast/mDNS discovery is not blocked.
- Switching the output recreates the LibVLC media player. The player now defers seek restoration until LibVLC reports Playing and preserves paused/playing state. If a Cast output cannot start or resume, it attempts local playback. This recovery is best-effort and still requires testing against each receiver and VLC build.
- Source has not yet been compiled or tested on the user's HP Envy.

## Google Cast / Android TV casting
1. Install a full 64-bit VLC distribution alongside ABM AV Player. The VLC renderer-discovery and Chromecast plugins must be present; the minimal LibVLC SDK alone is not a complete runtime.
2. On the TV, enable its built-in Google Cast / Chromecast receiver. Android TV devices without Google Cast support may expose another renderer protocol (for example UPnP/DLNA), but that is device- and VLC-plugin-dependent and is not guaranteed.
3. Connect the Windows PC and TV to the same non-guest Wi-Fi/LAN. Disable client/AP isolation and temporarily test without a VPN if discovery fails. Allow ABM AV Player and VLC through Windows Defender Firewall on Private networks.
4. Click **Find TVs** and wait for receivers to appear. Discovery is asynchronous; allow several seconds.
5. Select the TV in **Playback Output**. Start a local file, or switch the output while a file is playing. The player attempts to restore the previous position and play/pause state after the TV starts.
6. If the TV rejects the stream, the player attempts local playback rather than leaving the app without an output. This is a best-effort fallback, not a guarantee that every codec, subtitle, or container can be cast.

### Cast compatibility notes
- Google Cast support means casting to a TV or dongle exposing a compatible Chromecast receiver. Merely running Android TV does not guarantee that a device exposes the Cast receiver service.
- Some Android TV brands expose only DLNA/UPnP or proprietary protocols. The app can only use renderer services discovered and supported by the installed VLC build; it does not implement every vendor protocol.
- Unsupported media may require transcoding. Transcoding depends on the VLC build, available encoders, stream type, subtitle format, and receiver capabilities.
- Discovery problems are commonly caused by guest Wi-Fi, multicast/mDNS filtering, firewall rules, VPNs, or client isolation.

The application uses LibVLC's renderer-discovery and renderer-selection APIs. It does not bundle a separate FFmpeg executable; codec conversion is delegated to the VLC engine and its installed plugins. Transcoding is not guaranteed for every codec, subtitle format, or receiver.

## Remaining roadmap
1. Database-backed media library with folder indexing, search and richer recent history
2. Embedded and online album artwork with caching
3. Audio-reactive visualizer (requires a safe audio tap/mixer implementation)
4. Custom equalizer bands, audio-device selection and DSP beyond the current LibVLC presets
5. Hardware-acceleration and aspect-ratio controls
6. Stronger Chromecast session health, receiver-loss recovery, and reconnect UX
7. Parametric EQ controls, true-peak limiter, per-process visualizer capture and audio-device selection
