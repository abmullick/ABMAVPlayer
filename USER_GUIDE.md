# ABM AV Player — User Guide

This guide covers the Windows desktop player and its separate Streaming Centre. Some playback, discovery and casting features depend on the installed 64-bit VLC runtime/plugins, receiver capabilities, WebView2 and provider policies.

## 1. First launch and adding media

1. Start ABM AV Player.
2. Select **Open media** or press **Ctrl+O**.
3. Choose one or more audio/video files.
4. Alternatively, drag files from Windows File Explorer into the player.
5. Double-click a playlist row to start playback.

The playlist stores references to media files. Removing an item from the playlist does not delete the original file from disk.

## 2. Media & Streaming Hub

Open **Media & Streaming Hub** from the Command Centre. It presents two large navigation cards that match the main player's dark navy/blue design:

- **Streaming & TVs:** opens the separate Streaming Centre for provider websites, Google Cast/DLNA/DIAL TV discovery, TV app launching and supported casting workflows. You can also open it directly with **Ctrl+Shift+S**.
- **Torrent Streaming:** opens the separate Torrent Stream manager to add a magnet URI or `.torrent` file, select a video, inspect peers and transfer rates, and start progressive playback. You can also open it directly with **Ctrl+Shift+T**. The torrent manager is a separate window; the actual video plays in the main LibVLC player.

The two windows are separate from local playback and from each other's receiver/session state.

## 3. Application settings

Open **Settings** from the Command Centre. The dedicated dark settings window follows the player's navy/blue visual theme and groups the preferences that are currently supported by the application.

- **General:** default volume, playback speed, shuffle and repeat.
- **Audio:** ReplayGain normalisation and dynamic-range compressor preferences.
- **Torrent & Storage:** torrent cache parent folder and cache quota. The engine creates a `torrent-cache` subfolder inside the selected location.
- **Network:** torrent download and upload limits in KiB/s. A value of `0` means unlimited.
- **Updates:** manually check the latest published GitHub release.

Choose **Browse** to select a different cache folder, then **Save settings** to persist changes. The first-run default is the current Windows user's Downloads known folder, followed by an `ABM AV Player` folder. Windows Known Folder resolution respects redirected Downloads locations. Existing installations with a legacy torrent cache retain their existing location rather than silently abandoning cached data.

If the torrent engine is already running, changing its cache location takes effect after restarting ABM AV Player. Other supported settings are applied immediately where possible. The settings file is stored at `%APPDATA%\\ABMAVPlayer\\settings.ini`; audio processing preferences remain in `audio.ini`.

## 2. Application settings

Open **Settings** from the Command Centre. The dedicated window uses the player's dark navy/blue visual theme and groups the currently supported preferences.

- **General:** volume (0–100), playback speed (0.5x–2.0x), shuffle and repeat.
- **Audio:** ReplayGain normalisation and dynamic-range compressor.
- **Torrent & Storage:** cache parent folder and quota (1–4096 GiB; default 20 GiB). The engine creates a `torrent-cache` subfolder inside the selected parent.
- **Network:** torrent download and upload limits in KiB/s. A value of `0` means unlimited.
- **Updates:** manually check the latest public GitHub release.

Choose **Browse** to select a cache parent folder, then **Save settings**. **Open folder** opens the selected parent in File Explorer.

On a new installation, the cache is created under the Windows user's Downloads known folder:

```text
<Windows Downloads>\\ABM AV Player\\torrent-cache
```

Windows Known Folder resolution respects redirected Downloads locations. Existing installations with a legacy cache under `%APPDATA%\\ABMAVPlayer\\torrent-cache` retain that location unless the user chooses another.

Changing the cache parent does not automatically move existing cache data. If Torrent Stream is already running, the active session continues using its original path; restart ABM AV Player to activate the new path. Keep the old folder if you want to preserve its cached pieces.

Settings are saved in `%APPDATA%\\ABMAVPlayer\\settings.ini`; audio processing preferences are also stored in `audio.ini`. For the full option reference and troubleshooting, see [Application Settings](SETTINGS.md).

## 3. Playback and fullscreen

| Control | What it does |
|---|---|
| Play / Pause | Starts the selected item, or pauses/resumes current playback |
| Stop | Stops current playback |
| Previous / Next | Moves through the playlist |
| Seek bar | Drag to a different position in the current media |
| Volume | Drag to adjust playback volume |
| Fullscreen | Toggles fullscreen video; use F11 or double-click the video |

In fullscreen, move the pointer to reveal the playback overlay. Drag the timeline to seek and use the overlay controls. Press **Esc** or **F11** to return to the windowed player.

## 4. Playlist and recent media

- **Add media:** Add more files to the current playlist.
- **Remove:** Right-click a playlist entry to remove it from the list.
- **Clear playlist:** Removes all entries from the current list; it does not delete source files.
- **Load playlist / Save playlist:** Opens or saves M3U playlists.
- **Recent media:** Opens the recent-items list, when entries are available.
- **Shuffle:** Changes the next-item selection order.
- **Repeat current:** Repeats the currently playing item.
- **Capacity:** The playlist currently supports up to 256 entries.

Recent items and playback preferences are stored in the current Windows user's application data folder.

## 5. Audio, video and subtitles

Right-click the video surface while media is playing to access available media actions:

- **Audio track:** Select an available audio stream.
- **Subtitles:** Select an embedded subtitle stream or disable subtitles.
- **Load external subtitle:** Load a subtitle file from disk. The file picker exposes SRT, ASS, SSA, SUB and VTT extensions.
- **Playback speed:** Choose a preset from 0.5x to 2.0x.
- **Equalizer presets:** Apply an available LibVLC equalizer preset.
- **Media information:** View title, artist, album and duration when LibVLC provides them.
- **ReplayGain:** Toggle track-mode loudness normalisation for media opened after changing the setting. Usable ReplayGain tags are required.
- **Dynamic-range compressor:** Toggle the VLC compressor filter for media opened after changing the setting. This is a dynamic-range effect, not a guaranteed true-peak limiter.

The audio-track and subtitle menus show only streams exposed by the current media and VLC build.

## 6. Advanced equalizer, now playing and visualizer

Open **Advanced equalizer** from the Command Centre or press **Ctrl+E**. The ABM 10-band graphic EQ provides these fixed profiles:

- Flat
- Bass boost
- Vocal clarity
- Treble boost

The selected curve is applied through LibVLC's equalizer API. This is a fixed-band graphic equalizer, not a parametric EQ with adjustable centre frequency and Q. Audio preferences are stored in:

`%APPDATA%\\ABMAVPlayer\\audio.ini`

During audio playback, the player can show now-playing metadata and a live spectrum/visualizer when the required playback data is available. The visualizer currently captures the Windows default render mix, so sounds from other applications may also contribute. Process-specific audio capture is not implemented.

## 6. Local playback output — Find network devices / Cast to TV

The main player's local casting controls are in the **Media / Settings** drawer under **Playback Output**.

1. Open the drawer and expand Playback Output.
2. Choose **Find TVs / Find network devices** or press **Ctrl+D** (Ctrl+Shift+C also starts discovery).
3. Wait several seconds for asynchronous discovery.
4. Select a discovered receiver. You can select it before opening a file or while playback is active.
5. Choose **Local playback** to return output to the PC.

The main player uses LibVLC renderer discovery. Compatible Google Cast / Chromecast receivers and other renderer services exposed by the installed VLC build may appear. Duplicate receiver names from multiple discovery services are consolidated in the visible list.

### Google Cast requirements and troubleshooting

- The TV/dongle must expose a compatible Google Cast / Chromecast receiver. Miracast-only devices are not Google Cast receivers; Android TV branding alone does not guarantee Google Cast support.
- Install a full 64-bit VLC runtime with renderer-discovery and Chromecast plugins and their dependencies. A minimal LibVLC SDK alone is insufficient.
- Connect the PC and TV to the same non-guest Wi-Fi/LAN.
- Allow ABM AV Player and VLC through Windows Defender Firewall on Private networks.
- Ensure router multicast/mDNS is not blocked. Guest Wi-Fi, client/AP isolation, VPNs and endpoint security can prevent discovery.

When output is switched during playback, ABM AV Player recreates the LibVLC media player and attempts to restore the previous position and play/pause state after the new output reports that playback has started. This is best-effort. If Cast playback fails, the app reports the failure; automatic local fallback is not guaranteed.

See [CASTING.md](CASTING.md) for additional network and receiver troubleshooting.

## 7. Streaming Centre — online video services

Open **Streaming services** from the Command Centre or press **Ctrl+Shift+S**. The Streaming Centre is a separate Microsoft Edge WebView2 browser window with six service cards:

- YouTube
- Netflix
- Prime Video
- JioHotstar
- Sony LIV
- ZEE5

Each card opens the provider's official website. Sign in directly with your own provider account and subscription. ABM AV Player does not collect, proxy or store provider passwords.

### Browser navigation and controls

- **Back / Forward:** Navigate browser history.
- **Reload:** Refresh the current provider page.
- **Home:** Return to the Streaming Centre landing page.
- **Fullscreen:** Request fullscreen for the provider's active video element or its fullscreen control. Use **F11** or **Esc** to enter/leave as supported by the provider and WebView2.
- **Open in browser:** Open the current page in the default desktop browser.
- **Help / About:** Open Streaming Centre help or product information.
- **Previous / Next:** Intended for supported YouTube playlist navigation; behaviour depends on the page/provider.

The Streaming Centre's browser profile is stored locally at:

`%LOCALAPPDATA%\\ABMAVPlayer\\WebView2`

Supported cookies and sign-in sessions can persist between launches for the current Windows user. Sign out through the provider's website. To clear the local browser profile, close ABM AV Player first and remove that WebView2 profile folder; this signs out of sites stored in the profile.

### Streaming compatibility and privacy

A subscription does not guarantee that protected video will play in an embedded browser. Provider policy, DRM requirements, browser allow-lists, device certification, region and WebView2 runtime version can affect compatibility. If a provider blocks embedded playback or a video fails to start, use **Open in browser** or the provider's official supported browser/app. The player does not extract protected streams or bypass DRM.

## 8. Streaming Centre — Find TVs and Cast video

The Streaming Centre has its own **Find TVs** button and integrated receiver-selection panel. This is separate from the main player's Playback Output selector.

1. Open **Find TVs** in the Streaming Centre.
2. Wait for discovery, or choose **Refresh devices** to search again.
3. Select a receiver from the list.
4. Click **Use selected TV**. The panel displays the selected receiver.
5. Click **Check TV apps** to inspect receiver-confirmed app endpoints.
6. If an app is listed, select it in the app selector and click **Launch app**. The TV opens that app; use the TV remote to choose content and sign in.
7. Start a supported video in the provider page and click **Cast video**.

The Streaming Centre discovers receivers through:
- **Google Cast / Chromecast** renderer discovery.
- **DLNA / UPnP** MediaRenderer discovery.
- **Google Cast V2** discovery for Cast receiver app control over TLS/TCP 8009.
- **DIAL** app discovery for YouTube app handoff.

The **Check TV apps** action queries the selected receiver without launching anything. Google Cast V2 checks whether the YouTube Cast app (`233637DE`) and an experimental Prime Video receiver ID (`17608BC8`) are available. The Prime Video ID is community-documented and is not an Amazon-published third-party sender API; verify it against the actual TV. DIAL checks the known application resource names YouTube, Netflix, PrimeVideo, JioHotstar, SonyLIV and ZEE5. A DIAL result of **recognized** means the receiver exposes that app endpoint; **not exposed by DIAL** does not prove the Android TV app is not installed. DIAL is not a complete installed-app inventory. Apps confirmed by these checks appear in the app selector. **Launch app** opens the selected app through Google Cast V2 (YouTube or the experimental Prime Video receiver, if advertised) or a DIAL POST after a successful DIAL capability check. This launches the TV app only; it does not send the current browser title, provider login or playback session.

If the same receiver is found through multiple protocols, matching names are consolidated in the list. The implementation prefers Google Cast when a matching receiver exposes both Google Cast and DLNA; DLNA is used when it is the available media-renderer route. Google Cast V2 is preferred for YouTube app control when available; DIAL remains a fallback. Prime Video title casting is not supported from the desktop browser.

### What can be cast

- **YouTube:** The player recognises a YouTube video URL and uses Google Cast V2 to launch the YouTube receiver app, obtain its MDX screen ID and send the selected video through YouTube Lounge remote control. A compatible DIAL app endpoint is used as a fallback. These app-control protocols are receiver-dependent and YouTube MDX/Lounge is undocumented; DLNA alone cannot launch the YouTube app.
- **ZEE5 and SonyLIV:** The TV app selector checks common DIAL name variants and lists an app only when the TV recognises its endpoint. On the matching provider website, **Cast video** requests the TV app to open through DIAL. This does not transfer the desktop title or login session. If the TV does not expose the app through DIAL, open it from the TV home screen. Use the provider's official mobile app Cast control for title casting.\n- **Prime Video:** Desktop browser-to-TV title casting is not implemented. **Check TV apps** probes the experimental Google Cast receiver ID `17608BC8` and the `PrimeVideo` DIAL endpoint. When the selected TV exposes the Cast V2 endpoint, **Cast video** on a Prime Video page now requests that the Prime Video TV app open and reports the result. This is app launch only; it does not send a title, Amazon account session or protected playback. Choose content with the TV remote, or use Amazon's official Android/iOS Cast button for phone-to-TV title playback.
- **Direct video URL:** For other pages, the Streaming Centre looks for a currently active HTML video element with a directly addressable media URL and hands that URL to the selected compatible Google Cast or DLNA renderer.
- **Not browser mirroring:** This feature does not mirror the browser tab or desktop and is not a general-purpose Google Cast sender.

Many streaming services use Media Source Extensions, adaptive HLS/DASH manifests, short-lived signed URLs, authentication headers, cookies, DRM or encrypted segments. Those sources may not be directly accessible to LibVLC or the TV. Protected, segmented, encrypted, blob: and data: streams are not extracted or bypassed. The player does not copy browser cookies or provider credentials to the TV.

A successful launch/request does not guarantee that the TV can fetch or decode the content. If handoff fails, use the provider's own Cast control or its official TV app. Streaming Centre receiver selection and casting do not stop, replace or reconfigure local file playback or the main player's local casting selection.

See [STREAMING.md](STREAMING.md) for implementation details and limitations.

## 9. In-app updates

Choose **Check for updates** in the Command Centre.

1. The player checks the latest published release on the public ABM AV Player GitHub repository.
2. If a newer version exists, it displays the release version and release notes.
3. If you accept, it downloads the official installer over HTTPS.
4. The updater verifies the downloaded installer against the SHA-256 digest supplied by GitHub for that release asset.
5. If the digest is missing or does not match, the installer is not launched.
6. If verification succeeds, the installer starts and Windows asks for the normal elevation approval.

No GitHub account, login, access token or code-signing certificate is required for this SHA-256 integrity check. SHA-256 confirms the downloaded bytes match the digest GitHub reports; it does not independently authenticate the publisher like Authenticode signing. Windows SmartScreen or antivirus software may still show warnings for an unsigned installer.

Users can also download the installer manually from [GitHub Releases](https://github.com/abmullick/ABMAVPlayer/releases). Older installed versions that do not include the SHA-256-only updater may need one manual update before using this certificate-free update flow.

## 10. Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+O | Open media |
| Ctrl+D | Find network devices |
| Ctrl+Shift+C | Start local-player receiver discovery |
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
| Ctrl+Shift+S | Open Streaming Centre |\n| Ctrl+Shift+T | Open Torrent Stream manager |
| F1 | Open main-player Help |
| Ctrl+I | Open About |
| Esc | Close the menu or leave fullscreen |
| Alt+F4 | Exit the application |

Streaming Centre keyboard controls: **F5** reloads the page; **F11** requests provider fullscreen; **Esc** exits fullscreen; **Ctrl+R** reloads; **Ctrl+L** opens the active provider page in the default browser; **Ctrl+W** minimises the Streaming Centre. Browser Back/Forward keys navigate history.

## 11. Preferences and local data

General preferences, recent items and playlist-related state are stored under:

`%APPDATA%\\ABMAVPlayer`

Audio preferences are stored in `audio.ini`. The WebView2 browser profile is separate under `%LOCALAPPDATA%\\ABMAVPlayer\\WebView2`. The application does not currently maintain a database-backed, automatically indexed media library.

## 12. Troubleshooting

### A file does not play
- Confirm that the source file exists and can be opened by VLC.
- Check that the installed VLC build includes the required codec/plugin.
- Try a common MP4/H.264 video or MP3 audio file to distinguish a codec issue from a general playback issue.

### The TV is not discovered by the main player
- Confirm PC and TV are on the same non-guest network.
- Check Windows Defender Firewall permissions for ABM AV Player and VLC on Private networks.
- Disable VPN temporarily and check router guest-network/client isolation and multicast/mDNS settings.
- Confirm that the TV exposes a compatible renderer service and that the installed VLC runtime includes the corresponding discovery plugin.

### The Streaming Centre does not show a TV
- Open its own **Find TVs** panel and click **Refresh devices**.
- Check that the TV is on the same LAN and that multicast/SSDP discovery is not blocked.
- A TV may expose DLNA or DIAL but not Google Cast; discovery protocols are independent.

### Launch app
- Only receiver-confirmed app endpoints are offered.
- A successful DIAL response means the TV accepted the app-launch request; it does not confirm that a title is playing. Use the TV remote to select a show or movie.
- Launching Prime Video on the TV is separate from desktop Prime Video title casting, which remains unsupported.

### Check TV apps results
- **Check TV apps** is a read-only endpoint check. It does not open, stop or change an app on the TV.
- A DIAL 404 means the requested app name is not exposed through DIAL on that receiver; it does not mean the app is absent from Android TV.
- Google Cast `APP_AVAILABLE` confirms that the receiver advertises the queried app ID, not that a title can be handed off. The Prime Video receiver ID check is experimental; a successful app launch is not confirmation of Amazon title playback.

### A TV is listed but Cast video does not start
- Confirm the correct TV is selected in the Streaming Centre panel, then try **Cast video** again.
- YouTube handoff starts the TV app through Google Cast V2 or DIAL, then uses YouTube Lounge remote control to send the selected video. The Lounge API is undocumented and may change.
- On a Prime Video page, **Cast video** explains that desktop browser-to-TV title casting is not supported. If the TV exposes `PrimeVideo` through DIAL, use **Check TV apps** and **Launch app** to open the TV app, then select content with the remote. Otherwise use Amazon's official Android/iOS Cast button or open Prime Video directly on the TV.
- For other pages, the video must expose a direct media URL and the receiver must be able to fetch/decode it.
- Protected, adaptive or segmented streaming content may not be handoff-compatible. Use the provider's own Cast control or TV app.

### The Streaming Centre is blank or cannot start
- Install or repair the Microsoft Edge WebView2 Evergreen Runtime.
- Check network connectivity and provider availability.
- If the embedded browser process fails, the app attempts bounded automatic recovery. If it cannot recover, close and reopen the Streaming Centre.
- Try **Open in browser** for provider compatibility issues.

### Automatic update is unavailable
- Check internet access to GitHub Releases.
- Confirm that the latest release includes an asset named exactly `ABM-AV-Player-Setup.exe`.
- The release asset must have a GitHub SHA-256 digest; if GitHub does not provide one, the updater blocks automatic installation.
- Download the installer manually from the official Releases page if necessary.

## 13. Application architecture

ABM AV Player is a native Windows desktop application. Win32 owns the application window and controls; Direct2D/DirectWrite render the main interface; LibVLC handles local media decoding and renderer output; Microsoft Edge WebView2 renders the separate Streaming Centre. The Streaming Centre's receiver discovery and cast handoff are isolated from the main player's local playback and casting state.

The application is a media player, not a separate kernel audio/video driver.


## Streaming Centre TV app controls

After selecting a TV, click **Check TV apps**. The list includes receiver-confirmed apps and, when a DIAL endpoint is present, clearly labelled manual launch attempts for Netflix, SonyLIV and ZEE5 if the TV does not expose a readable DIAL GET status endpoint. Select an entry and click **Launch app**. For a manual attempt, the TV may accept or reject the DIAL POST request; the entry does not prove that the app is installed.

After a TV is selected, **Use selected TV** pulses orange. After a TV confirms an app launch, **Launch app** pulses orange. The **Close** button is last in the device panel. **Refresh app status** checks whether the selected Google Cast application session is running, or queries the DIAL application state. It reports app-level state only, not whether a film is playing, paused or buffering. **Stop TV** closes a Cast-launched app using the receiver session STOP command, or sends DIAL DELETE for a DIAL-launched app. It is not a pause command. Standard DIAL does not provide playback seek/resume controls. If the provider does not expose a compatible sender session, use the TV remote or official mobile app. The existing YouTube handoff and main-player local playback/casting paths are unchanged.


**Refresh app status** queries the TV's current receiver application state and reports whether the selected Cast app is running or not running, or reads the selected DIAL app's running/stopped state. It does not report movie playback state (playing, paused or buffering).
