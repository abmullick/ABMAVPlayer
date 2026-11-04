# Google Cast and network receiver guide

**Product:** ABM AV Player  
**Product Owner:** Abhisek Basu Mullick  
**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.

## Current implementation

ABM AV Player uses LibVLC's renderer-discovery API to enumerate receiver-discovery services available in the installed VLC build. It listens for receiver-added and receiver-removed events and presents discovered receivers in the player.

The player currently requires the receiver to be selected before opening media. This is a limitation of the current UI/player lifecycle: LibVLC requires the renderer to be set before the media player's first playback call for that route to take effect.

## How to cast

1. Install the full 64-bit VLC desktop application and the matching 64-bit LibVLC SDK.
2. Run ABM AV Player.
3. Click **Find TVs**.
4. Wait for Google TV / Chromecast devices to appear.
5. Select the desired receiver.
6. Click **Open** and choose the video.
7. Use the player's playback controls. The TV is the active renderer.

## Automatic compatibility conversion

For Chromecast rendering, conversion is handled by the VLC engine's Chromecast renderer/stream-output modules and their installed encoder plugins. VLC can pass through compatible audio/video streams and transcode incompatible streams to formats supported by the receiver. The exact fallback behavior depends on the VLC version, renderer, media streams, subtitles, and available encoders.

ABM AV Player does not currently ship or invoke a separate FFmpeg executable. Do not assume every unusual codec, subtitle format, or container can be converted successfully.

## Network troubleshooting

- Connect the laptop and Google TV to the same non-guest local network.
- Disable client/AP isolation on the router if enabled.
- Allow ABM AV Player and/or VLC through Windows Defender Firewall on **Private** networks.
- Ensure multicast DNS (mDNS) traffic is not blocked by the router, VPN, or endpoint security software.
- Confirm the installed VLC folder contains its normal `plugins` directory and that `VLC_PLUGIN_PATH` points to it.
- Some TVs expose Cast receivers only while awake and available on the network.

## Current limitations

- The receiver must be selected before opening the media file.
- Changing from local playback to a receiver after playback has begun requires restarting the application.
- No explicit Cast disconnect / return-to-local control yet.
- No independent transcoding settings or quality selector yet.
- Successful discovery and playback depend on the installed VLC build and receiver compatibility.

