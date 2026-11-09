# ABM AV Player — User Guide

This guide describes the controls and behaviour currently implemented in ABM AV Player. Some capabilities depend on the media file and the installed 64-bit VLC runtime/plugins.

## 1. First launch and adding media

1. Start ABM AV Player.
2. Select Open media or press Ctrl+O.
3. Choose one or more audio/video files.
4. Alternatively, drag files from Windows File Explorer and drop them onto the player.
5. Select a playlist item and double-click it to start playback.

The playlist is a list of references to media files. Removing an item from the playlist does not delete the original file from disk.

## 2. Playback

| Control | What it does |
|---|---|
| Play / Pause | Starts the selected item, or pauses/resumes the current item |
| Stop | Stops the current playback |
| Previous / Next | Moves to the previous or next playlist item |
| Seek bar | Drag to a different position in the current media |
| Volume | Drag to adjust playback volume |
| Fullscreen | Toggles fullscreen video; F11 is the keyboard shortcut |

Use the video surface's right-click menu for additional media-specific actions. Double-clicking the video toggles fullscreen.

## 3. Playlist and recent media

- Add media: Add more files to the current playlist.
- Remove: Right-click a playlist entry to remove it from the list.
- Clear playlist: Removes all entries from the current list; it does not delete source files.
- Load playlist: Opens an M3U playlist.
- Save playlist: Saves the current list as an M3U playlist.
- Recent media: Opens the recent-items list, when entries are available.
- Shuffle: Randomises the next-item selection.
- Repeat current: Repeats the currently playing item.
- Capacity: The current playlist limit is 256 entries.

Recent items and playback preferences are stored in the current Windows user's application data folder.

## 4. Audio and video options

Right-click the video surface while media is playing to access context actions. The options available can depend on the current file:

- Audio track: Select an available audio stream.
- Subtitles: Select an embedded subtitle stream or disable subtitles.
- Load external subtitle: Load a subtitle file from disk. Supported extensions exposed by the file picker include SRT, ASS, SSA, SUB and VTT.
- Playback speed: Choose a supported speed preset (0.5x–2.0x).
- Equalizer presets: Apply an available equalizer preset.
- Media information: View basic metadata such as title, artist, album and duration when LibVLC has made it available.
- Shuffle / Repeat: Change playlist playback behaviour.
- ReplayGain: Toggle track-mode loudness normalisation for media opened after changing the setting. ReplayGain works only when usable tags are present.
- Dynamic-range compressor: Toggle the VLC compressor filter for media opened after changing the setting. This is not a guaranteed true-peak limiter.

The audio-track and subtitle menus show only streams exposed by the current media/VLC build.

## 5. Advanced equalizer

Open Advanced equalizer from the command menu or press Ctrl+E. The ABM graphic EQ provides 10 fixed frequency bands and profile curves:

- Flat
- Bass boost
- Vocal clarity
- Treble boost

The selected curve is applied through LibVLC's equalizer API. These are fixed graphic-EQ profiles; the player does not currently offer a fully parametric EQ with adjustable centre frequency and Q. Audio preferences are stored under %APPDATA%\ABMAVPlayer\audio.ini.

Use boosts conservatively. Strong bass/treble boosts can reduce headroom and may cause clipping depending on the source and output device.

## 6. Now-playing display and visualizer

During audio playback, the player can display now-playing information and a live audio spectrum/visualizer when the required playback data is available. The visualizer currently captures the Windows default render mix, so sounds from other applications may also contribute to the display. Process-specific audio capture is not implemented.

## 6. Playback output and casting

1. Open the Media / Settings drawer.
2. Find Playback Output.
3. Choose Find TVs / Find network devices and allow several seconds for asynchronous discovery.
4. Select a discovered receiver. You can select it before starting a file or while playback is active.
5. Choose Local playback to use the PC's normal output again.

### Requirements for Google Cast

- The TV/dongle must expose a compatible Google Cast / Chromecast receiver. Miracast-only devices are not Google Cast receivers.
- Install a full 64-bit VLC runtime with the renderer-discovery and Chromecast plugins and their dependencies. A minimal LibVLC SDK alone is insufficient.
- Connect the PC and TV to the same non-guest Wi-Fi/LAN.
- Allow ABM AV Player and VLC through Windows Defender Firewall on Private networks.
- Ensure router multicast/mDNS is not blocked. Guest Wi-Fi, client/AP isolation, VPNs and endpoint security can prevent discovery.

When output is switched during playback, ABM AV Player recreates the LibVLC media player and attempts to restore the previous position and play/pause state once the new output reports that playback has started. This is best-effort. If Cast playback fails, the app reports the failure; automatic local fallback is not guaranteed by the current implementation.

Android TV branding does not by itself guarantee Google Cast support. Some televisions expose only DLNA/UPnP or proprietary renderer services, and support depends on the VLC build.

For more detail see CASTING.md.

## 7. Supported formats

Common formats include:

- Audio: MP3, WAV, FLAC, AAC, M4A and OGG.
- Video: MP4, MKV, AVI, MOV, WMV and WebM.

These are container/format examples, not a guarantee that every codec profile will play. Actual decoding depends on the installed VLC build and its plugins.

## 7. Streaming Centre — online video services

ABM AV Player includes a separate, built-in browser window for popular streaming websites. Open **Streaming services** from the Command Centre or press **Ctrl+Shift+S**. Choose a provider from the service strip:

- YouTube
- Netflix
- Prime Video
- JioHotstar
- Sony LIV
- ZEE5

The Streaming Centre opens each provider's **official website** in Microsoft Edge WebView2. Sign in directly on that website using your own account and subscription. ABM AV Player does not collect, proxy or store provider passwords.

### Browser sessions and privacy

The WebView2 profile is stored locally at:

`%LOCALAPPDATA%\ABMAVPlayer\WebView2`

Supported cookies and sign-in sessions can persist between application launches for the current Windows user. Sign out through the provider's website. Removing this WebView2 profile after closing the player clears its locally stored browser data and signs out of sites using that profile.

### Playback compatibility

A subscription does not guarantee that protected video will play in an embedded browser. Provider policy, DRM requirements, browser allow-lists, device certification, region and runtime version can affect compatibility. If a provider blocks embedded playback or a video fails to start, use the Streaming Centre's **Open in browser** control or the provider's official supported browser/app. The player does not extract protected streams or bypass DRM.

### Requirements

The Microsoft Edge WebView2 Evergreen Runtime is required to use the Streaming Centre. It is normally present on current Windows 11 systems, but should be verified on managed or customized Windows installations. The WebView2 SDK is required only when building the application.

See [STREAMING.md](STREAMING.md) for additional setup and troubleshooting information.

## 8. Keyboard shortcuts


| Shortcut | Action |
|---|---|
| Ctrl+O | Open media |
| Ctrl+D | Find network devices |
| Ctrl+Shift+C | Start Cast / receiver discovery |
| Space | Play / Pause |
| Left / Right | Seek backward / forward 10 seconds |
| Home | Seek to the beginning |
| N / P | Next / Previous playlist item |
| + / - | Increase / decrease playback speed |
| F11 | Toggle fullscreen |
| Ctrl+L | Load playlist |
| Ctrl+S | Save playlist |
| Ctrl+J | Open recent media |
| Ctrl+E | Open Advanced equalizer |
| Ctrl+Shift+H | Toggle shuffle |
| Ctrl+R | Toggle repeat-current-track |
| F1 | Open Help |
| Ctrl+I | Open About |
| Esc | Close the menu or leave fullscreen |
| Alt+F4 | Exit the application |

## 9. Preferences and local data

ABM AV Player stores preferences and recent/playlist state under:

%APPDATA%\ABMAVPlayer

Audio preferences are stored in audio.ini. The application does not currently maintain a database-backed, automatically indexed media library.

## 10. Troubleshooting

### A file does not play
- Confirm that the source file still exists and can be opened by VLC.
- Check that the installed VLC build includes the required codec/plugin.
- Try a common MP4/H.264 video or MP3 audio file to distinguish a codec issue from a general playback issue.

### The TV is not discovered
- Confirm the PC and TV are on the same non-guest LAN.
- Wake the TV and ensure its Cast receiver is enabled.
- Allow ABM AV Player and VLC through Windows Defender Firewall on Private networks.
- Temporarily test without a VPN; check router client isolation and multicast/mDNS filtering.
- Confirm that the VLC installation includes the renderer-discovery and Chromecast plugins.

### The TV appears, but playback fails
- Try a common H.264/AAC MP4 file.
- Check receiver compatibility and the VLC plugin/encoder set.
- Some containers, codecs and subtitle combinations cannot be transcoded for every receiver. Output switching and recovery are best-effort.

### The equalizer or ReplayGain has no audible effect
- Confirm media is playing and reopen the media after changing ReplayGain/compressor settings.
- ReplayGain requires usable track tags. EQ and filter availability can vary with the installed VLC build.

## 11. Product boundaries

ABM AV Player uses LibVLC for decoding and device output. It is a desktop media player, not an audio/video driver. It currently has no exclusive/bit-perfect audio path, no dedicated audio-device selector, no parametric EQ, and no process-specific audio loopback visualizer.


