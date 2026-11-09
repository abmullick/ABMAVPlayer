# Streaming Centre

ABM AV Player includes a separate Microsoft Edge WebView2-based Streaming Centre. It is launched from the Command Centre using **Streaming services** or **Ctrl+Shift+S**.

## Supported launch targets

- YouTube — https://www.youtube.com/
- Netflix — https://www.netflix.com/
- Prime Video — https://www.primevideo.com/
- JioHotstar — https://www.hotstar.com/in
- Sony LIV — https://www.sonyliv.com/
- ZEE5 — https://www.zee5.com/

The application opens the providers' official websites. Users authenticate directly with each provider using their own account and subscription. ABM AV Player does not proxy sign-in, store provider passwords, extract media URLs or bypass access controls.

## Session storage and privacy

WebView2 uses a persistent user-data directory at:

`%LOCALAPPDATA%\ABMAVPlayer\WebView2`

This allows provider cookies and supported login sessions to remain available between application launches. The profile is local to the current Windows user. Users can sign out through each provider's own website or remove the local WebView2 profile after closing ABM AV Player (this signs them out of sites stored in that profile).

## Playback compatibility

A valid subscription does not guarantee embedded playback. Providers may restrict playback in embedded browsers, require browser/device certification, or depend on DRM capabilities not available in a particular WebView2 Runtime configuration. Compatibility can vary by service, content, account, region and runtime version.

If a provider blocks embedded playback or protected video fails, use the **Open in browser** control in the Streaming Centre or that provider's official application. ABM AV Player must not attempt to extract protected streams or circumvent DRM.

## Runtime and build requirements

- Microsoft Edge WebView2 Evergreen Runtime is required to run the Streaming Centre. Current Windows 11 installations commonly include it, but deployment environments should verify it.
- The Microsoft.Web.WebView2 NuGet package is required to compile the native WebView2 host.
- Install the SDK package from the repository root:

  `nuget install Microsoft.Web.WebView2 -OutputDirectory packages -ExcludeVersion`

- The build expects `WEBVIEW2_SDK` to point to the package root. The default is `%CD%\packages\Microsoft.Web.WebView2`.
- If the runtime is missing, the Streaming Centre displays an explanatory status message; local LibVLC playback remains independent.

## Implementation boundaries

- LibVLC continues to own local file playback and network renderer/casting.
- WebView2 owns streaming website rendering and provider sign-in.
- The streaming browser runs in a separate top-level window so its native browser surface does not interfere with the Direct2D video child window.
- Browser profile data is isolated from the existing playlist/preferences directory.

## Streaming window controls

- **Back / Forward:** Navigate WebView2 browsing history for the current provider.
- **Reload / Home:** Refresh the current page or return to the Streaming Centre landing page.
- **Fullscreen:** Focuses the embedded provider player and sends its standard fullscreen shortcut. When the provider enters fullscreen, WebView2 reports its fullscreen element and ABM AV Player switches the Streaming Centre window to a borderless monitor-sized surface. Press **Esc** to exit. Provider support and keyboard shortcuts can vary.
- **Open in browser:** Opens the current provider URL in the user's default desktop browser.
- **Help / About:** Available in the Streaming Centre header.

Fullscreen is intended for the actual provider video player, not merely enlarging the normal application window. Some providers may restrict fullscreen or require using their own on-player fullscreen control.



## Cast to TV integration

The Streaming Centre has its own **Find TVs** device-selection panel. It opens inside the Streaming Centre and does not bring the main player forward. The panel starts or refreshes the application's LibVLC mDNS / Google Cast renderer discovery, lists compatible receivers, and stores a streaming-specific receiver selection. The existing local-player receiver selection is not changed.

The Streaming Centre uses a dedicated LibVLC media-player instance for direct-URL casting. It does not stop, replace, or reconfigure the local media player, its current file, or its local casting selection.

## Casting a browser video to a TV (experimental)

1. Choose **Find TVs** in the Streaming Centre and select a receiver in that panel.
2. Start the video in the provider page.
3. Choose **Cast video**. The browser checks the active HTML5 video element and passes its current source URL to the dedicated streaming LibVLC player.
4. LibVLC asks the selected renderer to play that URL. A failed request is not silently redirected to local playback, and the local player's playback state is left unchanged.

This is a best-effort direct-URL handoff, not browser-tab mirroring or a full WebRTC/Google Cast sender implementation. It works only when the page exposes a directly addressable HTTP(S) media URL and the TV/renderer can fetch and decode it. Many modern sites use Media Source Extensions (MSE), short-lived signed URLs, cookies/authorization headers, adaptive HLS/DASH manifests, or DRM. Those sources may be unavailable to LibVLC or the receiver. blob: URLs, data: URLs, protected streams and DRM are intentionally not extracted or bypassed. The feature does not copy browser cookies or credentials to the TV.

If **Cast video** reports that no direct video URL is available, use the streaming provider's own Cast button or its official TV app. A successful handoff request does not guarantee that the receiver can fetch or play the media.

This is a best-effort direct-URL handoff, not browser-tab mirroring or a full WebRTC/Google Cast sender implementation. It works only when the page exposes a directly addressable HTTP(S) media URL and the TV/renderer can fetch and decode it. Many modern sites use Media Source Extensions (MSE), short-lived signed URLs, cookies/authorization headers, adaptive HLS/DASH manifests, or DRM. Those sources may be unavailable to LibVLC or the receiver. blob: URLs, data: URLs, protected streams and DRM are intentionally not extracted or bypassed. The feature does not copy browser cookies or credentials to the TV.

If **Cast video** reports that no direct video URL is available, use the streaming provider's own Cast button or its official TV app. A successful handoff request does not guarantee that the receiver can fetch or play the media.

## Runtime checks and recovery

Before starting the embedded browser, ABM AV Player checks whether a WebView2 runtime is available. If it is missing or cannot be found, the main player remains usable and the Streaming Centre displays an instruction to install the official Microsoft Edge WebView2 Evergreen Runtime. If the WebView2 browser process fails unexpectedly, the Streaming Centre makes a bounded number of automatic recovery attempts and restores the current HTTP(S) page when possible. If repeated recovery fails, close and reopen the Streaming Centre. Provider playback and DRM remain subject to the provider's policies.
