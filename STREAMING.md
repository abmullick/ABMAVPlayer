## GoCast YouTube sender

Streaming Centre's native YouTube Cast handoff now uses the vendored GoCast sender implementation (MIT-licensed upstream snapshot, pinned to `6acdd297db1c4b4b3aae13b8d6591488de65130a`). The Windows build compiles `tools/gocast/cmd/abm-gocast` into `abm-gocast.exe` and packages it beside the main application. The C++ Streaming Centre host passes the selected receiver host/port and validated YouTube video ID to this helper.

The GoCast path performs receiver TLS connection and Cast device-authentication challenge verification against the Cast trust anchors, queries YouTube app availability, launches the receiver app, waits for the app transport, requests the YouTube MDX screen ID, and performs the YouTube Lounge playlist handoff. It is separate from the main player's LibVLC renderer/local-casting path. The existing DIAL and DLNA paths remain available.

**Build requirement:** No manual Go installation is needed. `build.bat` uses an installed Go 1.25+ toolchain when available; otherwise `tools/ensure_go.ps1` downloads the official Go 1.27.3 Windows x64 archive into the local `build/toolchain` folder and verifies its published SHA-256 before extraction. The Go module dependencies are downloaded on first build. Internet access is required only for this first toolchain/dependency setup.

GoCast's YouTube Lounge control relies on YouTube's receiver-side MDX/Lounge behavior, which is not a stable public API. Receiver compatibility and physical TV playback must be validated on the target TV; a successful helper build does not guarantee that every Cast/Android TV firmware exposes the same YouTube app/session behavior.

# Streaming Centre — User Guide and Technical Notes

The ABM AV Player Streaming Centre is a separate Microsoft Edge WebView2-based browser window. It has its own navigation controls, service selector, TV discovery panel and streaming-cast playback path. Open it from the **Streaming & TVs** card in the main player's **Media & Streaming Hub**, from the Command Centre's **Streaming services** command, or directly with **Ctrl+Shift+S**. The Hub also has a separate **Torrent Streaming** card; that opens the Torrent Stream manager and does not open or replace the Streaming Centre.

## 1. Supported streaming services

The service cards open these providers' official websites:

- YouTube — https://www.youtube.com/
- Netflix — https://www.netflix.com/
- Prime Video — https://www.primevideo.com/
- JioHotstar — https://www.hotstar.com/in
- Sony LIV — https://www.sonyliv.com/
- ZEE5 — https://www.zee5.com/

Users authenticate directly with each provider using their own account and subscription. ABM AV Player does not proxy sign-in, collect provider passwords, host provider content, or provide subscriptions.

## 2. Browser controls

- **Back / Forward:** Navigate WebView2 browsing history.
- **Reload:** Refresh the current page.
- **Home:** Return to the Streaming Centre landing page.
- **Fullscreen:** Requests fullscreen from the active HTML video element or a provider fullscreen control. The window responds to WebView2 fullscreen events and switches to a monitor-sized surface. Press **Esc** to exit. Provider support varies.
- **Open in browser:** Opens the current HTTPS page in the user's default desktop browser.
- **Help / About:** Open the Streaming Centre's own help guide or product information.
- **Previous / Next:** Intended for supported YouTube playlist navigation; behaviour depends on the active page.

Keyboard controls include **F5** (reload), **F11** (request fullscreen), **Esc** (exit fullscreen), **Ctrl+R** (reload), **Ctrl+L** (open the active provider home page in the default browser), **Ctrl+W** (minimise), and browser Back/Forward keys (history navigation).

## 3. Session storage and privacy

WebView2 uses a persistent user-data directory at:

`%LOCALAPPDATA%\\ABMAVPlayer\\WebView2`

This allows provider cookies and supported login sessions to remain available between application launches for the current Windows user. Users can sign out through each provider's own website. To clear the local browser data, close ABM AV Player and remove this WebView2 profile folder; this signs out of sites using that profile.

The Streaming Centre validates navigation destinations and blocks non-HTTPS, local-network or otherwise unsafe destinations. Pop-up navigation is handled inside the trusted WebView when the target passes the same safety check. The app does not copy browser cookies or credentials to a TV.

## 4. Playback compatibility and DRM

A valid subscription does not guarantee embedded playback. Providers may restrict playback in embedded browsers, require browser/device certification, or depend on DRM capabilities unavailable in a particular WebView2 Runtime configuration. Compatibility can vary by service, content, account, region and runtime version.

If a provider blocks embedded playback or protected video fails, use **Open in browser** or the provider's official application. ABM AV Player does not extract protected streams, bypass DRM, or circumvent provider access controls.

## 5. Find TVs — integrated device-selection panel

The Streaming Centre has its own **Find TVs** button in the top toolbar. It opens a dedicated receiver-selection panel inside the Streaming Centre; it does not switch to the main player's window or reuse the main player's visible selection UI.

The panel provides:
1. A list of discovered receivers.
2. **Refresh devices** to restart discovery.
3. **Use selected TV** to apply the highlighted receiver.
4. **Close** to dismiss the panel.

Discovery checks four receiver paths:

- **Google Cast / Chromecast renderer:** Uses the existing LibVLC renderer-discovery session for direct media URL playback.
- **Google Cast V2:** Discovers `_googlecast._tcp.local` receivers through Windows DNS-SD, then uses TLS on TCP 8009 and Cast protobuf messages for YouTube app control.
- **DLNA / UPnP:** Discovers compatible UPnP MediaRenderer devices.
- **DIAL + YouTube Lounge:** Retained as a fallback for TVs that advertise a compatible DIAL app endpoint. The Lounge API is undocumented and may change.

The panel's **Check TV apps** action performs read-only capability checks. Over Google Cast V2 it asks the receiver whether the YouTube Cast app (`233637DE`) and an experimental Prime Video receiver app ID (`17608BC8`) are available, using the authenticated GoCast helper. The Prime Video ID is community-documented and is not an Amazon-published third-party sender API; availability must be confirmed on the actual TV. Over DIAL it queries the application resources `YouTube`, `Netflix`, `PrimeVideo`, `JioHotstar`, `SonyLIV` and `ZEE5` with HTTP GET only; it does not launch or stop applications. A DIAL `200` response means the endpoint recognises that application name and reports its state (running/stopped where supplied); `404` means that name is not exposed by that TV's DIAL service, not that the Android TV app is necessarily uninstalled. Provider-specific DIAL names vary, so these checks are evidence about receiver endpoints, not a complete Android TV installed-app inventory. The selector lists receiver-confirmed launch targets and clearly labelled manual DIAL launch attempts. For Netflix, SonyLIV and ZEE5, the manual entries are included when the TV has a DIAL endpoint even if the DIAL GET status endpoint returns 404. Launch sends a DIAL POST directly; the TV may accept or reject the application name. A manual entry is not proof that the app is installed. SonyLIV and ZEE5 use common case variants (`SonyLIV`/`SonyLiv` and `ZEE5`/`Zee5`) for automatic endpoint probes. A successful DIAL response means the TV accepted the launch request; it does not confirm playback. Use the TV remote to choose content and sign in. No browser session, title, account credentials or protected media are transferred.

The panel combines matching receiver names into one visible row when a device advertises more than one protocol. Where a matching device offers both Google Cast and DLNA, the Streaming Centre prefers the Google Cast route; DLNA is used when that is the available media-renderer route. A discovered DIAL endpoint is retained for app control. If the TV reports the experimental Cast receiver ID `17608BC8` as available, or recognises the `PrimeVideo` DIAL application resource, Prime Video can appear in the app selector and ABM AV Player can request an app launch. This is app launch only; it does not transfer a title or Amazon session, and the desktop browser still has no documented Amazon title-handoff interface. Cast V2 and DIAL are independent app-control paths.

The receiver list refreshes while the panel is open. If a device has disappeared or a selection is stale, use **Refresh devices**, select it again and press **Use selected TV**.

## 6. Cast video — what it does

After selecting a receiver, start a video in the browser and choose **Cast video**. The Streaming Centre examines the current page for either a recognised YouTube video or a directly addressable active HTML video source.

### YouTube handoff

For a recognised YouTube video URL, the app uses Google Cast V2 when the selected receiver advertises a Cast service: it opens a TLS connection to port 8009, launches the YouTube receiver application, requests the MDX screen ID and sends the video through YouTube Lounge remote control. If Cast V2 is unavailable, a discovered DIAL YouTube endpoint is used as a fallback. DLNA alone cannot launch the YouTube app. YouTube MDX/Lounge is an undocumented protocol and receiver firmware or service changes can break the handoff.

### ZEE5 and SonyLIV\n\nWhen the browser is on `zee5.com` or `sonyliv.com`, **Cast video** attempts to launch the matching TV app through a DIAL application endpoint recognised by the selected receiver. The app selector also probes common case variants of both DIAL resource names. A successful DIAL response confirms only that the TV accepted an app-launch request; it does not transfer the browser title, provider login or protected playback. If the TV returns `404` for every variant, open the app from the Android/Google TV home screen. Official ZEE5 and SonyLIV mobile apps document Chromecast casting; use their in-app Cast control to transfer a title.\n\n### Amazon Prime Video

When the current WebView2 page is on `primevideo.com`, **Cast video** requests the selected TV to open the experimental Prime Video Google Cast receiver app (`17608BC8`) when that endpoint was discovered. The result is reported as an app launch only; it does not transfer the desktop title, Amazon account session or playback state. The desktop Streaming Centre does not have a supported Amazon title-casting interface. If the selected TV has no Cast V2 endpoint, ABM explains that limitation instead of reporting a successful cast.

After ABM opens Prime Video on the TV, choose the title with the TV remote. To transfer a title from a phone, use the Cast button in Amazon's official Prime Video Android or iOS app and select the TV. The experimental Cast receiver check can tell whether the TV advertises app ID `17608BC8`; a successful launch is not a title handoff. ABM AV Player does not copy Amazon login state, transfer provider credentials, or bypass DRM. See [Amazon's Prime Video casting instructions](https://www.primevideo.com/help?nodeId=GDSGFYXXPCSAGVTH).

### Direct media URL handoff

For other pages, the Streaming Centre looks for a currently active HTML video element with a direct media URL and sends that URL to the selected Google Cast or DLNA renderer using the dedicated Streaming Centre playback path. The receiver must be able to reach and decode that URL.

This is **not browser-tab mirroring**, screen mirroring, a full WebRTC sender, or a universal Cast implementation. A direct URL handoff request being accepted does not guarantee that the TV can fetch or play the content.

### Content that generally cannot be handed off

Modern streaming sites may use Media Source Extensions (MSE), adaptive HLS/DASH manifests, short-lived signed URLs, cookies, authorisation headers, encrypted segments or DRM. Those sources may not be directly accessible to LibVLC or the receiver. Protected/segmented/encrypted streams, `blob:` URLs and `data:` URLs are not extracted or bypassed. Browser cookies and provider credentials are not copied to the TV.

If no direct video URL is available or playback fails:
- Try the provider's own Cast button.
- Use the provider's official TV app.
- Try **Open in browser** for embedded-browser playback issues.

## 7. Separation from local playback and casting

The Streaming Centre uses separate receiver-selection state and a dedicated LibVLC media-player instance for its direct-URL casting path. It does not stop, replace, seek or reconfigure the main player's local media player, current file, or local casting selection.

The main player's **Playback Output** selector is a different feature. It uses LibVLC renderer discovery for local media output, and its receiver selection remains independent of the Streaming Centre's Find TVs panel.

## 8. Runtime checks and recovery

Microsoft Edge WebView2 Evergreen Runtime is required to run the Streaming Centre. The app checks for the runtime before starting the embedded browser. If it is missing or cannot be found, the main player remains usable and the Streaming Centre displays an instruction to install the official runtime.

The Streaming Centre monitors WebView2 process failure. It makes a bounded number of automatic recovery attempts and restores the current safe HTTP(S) page when possible. If repeated recovery fails, close and reopen the Streaming Centre.

## 9. Build requirements

- Microsoft Edge WebView2 Evergreen Runtime is required at runtime.
- The Microsoft.Web.WebView2 NuGet package is required to compile the native WebView2 host.
- Install the SDK package from the repository root:

  `nuget install Microsoft.Web.WebView2 -OutputDirectory packages -ExcludeVersion`

- The build expects `WEBVIEW2_SDK` to point to the package root. The default is `%CD%\\packages\\Microsoft.Web.WebView2`.
- If the runtime is missing, the Streaming Centre reports the issue; local LibVLC playback remains independent.

## 10. Troubleshooting

### Streaming Centre does not open or remains blank
- Install or repair the Microsoft Edge WebView2 Evergreen Runtime.
- Check network connectivity and whether the provider website is available.
- If the embedded browser process fails, wait for the bounded recovery attempt. If it cannot recover, close and reopen the Streaming Centre.
- Use **Open in browser** to distinguish a provider/WebView2 restriction from a general network problem.

### No TV appears in Find TVs
- Confirm the PC and TV are on the same non-guest LAN.
- Click **Refresh devices**.
- Check multicast/mDNS and SSDP/UPnP discovery are not blocked by the router, firewall, VPN or client isolation.
- The TV may expose only one of Google Cast, DLNA or DIAL. Support for one protocol does not imply support for the others.

### Check TV apps and Launch app
- The app list contains only receiver-confirmed Google Cast YouTube availability or DIAL application resources.
- **Launch app** opens the selected receiver app only; it does not transfer a streaming title, browser session or account credentials. Use the TV remote to navigate and select content.
- A DIAL 404 means the TV's DIAL service does not recognise that application resource name. It does not prove that the app is absent from the TV launcher.

### Check TV apps reports an app as not exposed
- The check probes known DIAL application names and the Google Cast YouTube app ID; it is not a full Android TV installed-app inventory.
- A DIAL 404 means the TV's DIAL service does not recognise that application resource name. It does not prove that the app is absent from the TV launcher.
- A Cast `APP_UNAVAILABLE` response means the receiver did not advertise the requested Cast app ID. Use the TV's own app launcher for apps that are not exposed through these control protocols.

### A TV appears but YouTube does not launch
- YouTube app handoff requires a discovered and enabled DIAL YouTube app endpoint (or supported Cast app-control route).
- A DLNA-only receiver can receive compatible media URLs but cannot launch the YouTube app.
- Confirm YouTube is installed/enabled on the TV and that app launch from the local network is permitted.

### A TV appears but another service does not cast
- The current page must expose a directly addressable active video URL.
- Protected, encrypted, adaptive or segmented streams are not generally available for direct URL handoff.
- Try the provider's own Cast control or official TV app.

## Implementation boundaries

- WebView2 owns provider website rendering and sign-in.
- The Streaming Centre has independent Google Cast, DLNA/UPnP and DIAL discovery state for its receiver panel.
- A dedicated LibVLC media-player instance is used for supported direct-URL casting from the Streaming Centre.
- Local media playback and the main player's local receiver selection remain independent.
- Provider access, protected playback, subscriptions and content availability are controlled by each provider.


## Netflix and Prime Video receiver controls

Launching Netflix or Prime Video on a TV is a separate operation from transferring a browser title. The Streaming Centre does not copy provider credentials, DRM sessions or protected browser media to the receiver. Netflix and Prime Video desktop-browser title handoff is not implemented.

For a selected Google Cast app, **Refresh app status** requests receiver status and checks whether that app session is present. For a DIAL app, it reads the DIAL state (running/stopped/unknown). **Stop TV** sends the standard DIAL application DELETE request for DIAL-launched apps. For a Google Cast-launched app, **Stop TV** queries the receiver's active application session, sends the Cast receiver STOP command using that session ID, and waits for confirmation. This closes the app; it is not a playback pause command. Standard DIAL does not report whether a title is playing, paused or buffering and does not define provider playback seek/resume commands.

Pause, resume, seek and title metadata require a provider-compatible control session. Google Cast's standard media namespace defines these commands, but they are usable only when the running receiver application exposes/supports that media protocol and its session. A successful app launch alone does not establish such a media session. ABM therefore does not display false playback status or claim pause/resume support for Netflix or Prime Video. Use the TV remote or the provider's official mobile app for playback control when no supported sender session is available.

These app-control additions are confined to the Streaming Centre's DIAL app endpoint. They do not change local playback, local VLC renderer casting or the existing YouTube Cast handoff.
