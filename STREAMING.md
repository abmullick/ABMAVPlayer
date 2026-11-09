# Streaming Centre

ABM AV Player includes a separate Microsoft Edge WebView2-based Streaming Centre. It is launched from the Command Centre using **Streaming services** or **Ctrl+Shift+S**.

## Supported launch targets

- YouTube — https://www.youtube.com/
- Netflix — https://www.netflix.com/
- Prime Video — https://www.primevideo.com/
- JioHotstar — https://www.hotstar.com/in
- Sony LIV — https://www.sonyliv.com/

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
