# ABM AV Player\n\n**Product Owner:** Abhisek Basu Mullick  \n**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.\n\nThis repository is publicly viewable for reference only. Public availability does not grant permission to copy, modify, redistribute, sublicense, or commercially exploit this software. See [LICENSE](LICENSE) and [COPYRIGHT.md](COPYRIGHT.md).

A native Windows desktop audio/video player written in C.

## Architecture
- Application and interface: C + Win32 API
- Media playback, demuxing and codecs: LibVLC
- Video output: LibVLC embedded in a Win32 child window
- Audio output: LibVLC's Windows audio backend

LibVLC handles media decoding and device output. This project owns the desktop interface and playback controls; it is not a kernel audio/video driver.

## Current features
- Open local audio and video files
- Audio: MP3, WAV, FLAC, AAC, M4A and OGG (codec support depends on installed VLC)
- Video: MP4, MKV, AVI, MOV, WMV and WebM (codec support depends on installed VLC)
- Play/pause, stop, seek and volume
- Dark native Windows interface
- Unicode file paths converted to UTF-8 for LibVLC

## Requirements
- Windows 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- Windows SDK
- 64-bit VLC media player
- Matching 64-bit LibVLC SDK (headers and import library)

Download VLC: https://www.videolan.org/vlc/
LibVLC SDK: https://www.videolan.org/vlc/libvlc.html

All components must use the same architecture (x64).

## Build
1. Install 64-bit VLC.
2. Extract the matching 64-bit LibVLC SDK, for example to C:\vlc-sdk.
3. Open **x64 Native Tools Command Prompt for VS 2022**.
4. Set the SDK path: set VLC_SDK=C:\vlc-sdk
5. Run build.bat from the repository root.

The executable is written to build\ABMAVPlayer.exe.

## Run
Run run.bat. It defaults to the standard VLC installation directory:
C:\Program Files\VideoLAN\VLC

If VLC is installed elsewhere, set VLC_DIR before running.

## Known limitations of this first version
- Single-file playback; no playlist or library database yet.
- No subtitle/audio-track selection or playback-speed control yet.
- No exclusive-mode / bit-perfect audio path yet.
- No hardware-acceleration controls yet.
- Codec availability depends on the installed VLC build.
- Source has not yet been compiled or tested on the user's HP Envy.

## Roadmap
1. Robust playback-state handling and metadata
2. Playlist and drag-and-drop
3. Subtitle and audio-track selection
4. Fullscreen, aspect ratio and playback speed
5. Audio-device selection and advanced output
6. Equalizer and DSP
7. Library scanning, album art and persistent playlists
