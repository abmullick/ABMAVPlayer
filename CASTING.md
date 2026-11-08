# Google Cast and network receiver guide

**Product:** ABM AV Player  
**Product Owner:** Abhisek Basu Mullick  
**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.

## Current implementation

ABM AV Player uses LibVLC's renderer-discovery API to enumerate receiver-discovery services available in the installed VLC build. It listens for receiver-added and receiver-removed events and presents discovered receivers in the player.

The player can select a receiver before playback or switch output during playback. When the output changes, ABM AV Player recreates the LibVLC media player, reapplies the selected renderer, and attempts to resume the current item at its previous position and play/pause state.

## How to cast

1. Install the full 64-bit VLC desktop application and the matching 64-bit LibVLC SDK.
2. Run ABM AV Player.
3. Open **Playback Output** in the Media / Settings drawer.
4. Click **Find TVs** and wait for discovery events.
5. Select the desired receiver. You can do this before playback or while a file is playing.
6. Open media and use the player's playback controls. The TV becomes the active renderer.

## Automatic compatibility conversion

For Chromecast rendering, conversion is handled by the VLC engine's Chromecast renderer/stream-output modules and their installed encoder plugins. VLC can pass through compatible audio/video streams and transcode incompatible streams to formats supported by the receiver. The exact fallback behavior depends on the VLC version, renderer, media streams, subtitles, and available encoders.

ABM AV Player does not currently ship or invoke a separate FFmpeg executable. Do not assume every unusual codec, subtitle format, or container can be converted successfully.

## Network troubleshooting

- Connect the laptop and Google TV to the same non-guest local network.
- Disable client/AP isolation on the router if enabled.
- Allow ABM AV Player and/or VLC through Windows Defender Firewall on **Private** networks.
- Ensure multicast DNS (mDNS) traffic is not blocked by the router, VPN, or endpoint security software.
- Confirm the installed VLC folder contains its normal `plugins` directory. The player sets `VLC_PLUGIN_PATH` and LibVLC's explicit `--plugin-path` to the bundled `plugins` folder, and disables the plugin cache so newly copied receiver-discovery modules are scanned.
- Some TVs expose Cast receivers only while awake and available on the network.

## Current limitations

- Discovery requires a Google Cast / Chromecast-capable receiver and working multicast DNS on the local network; Miracast-only devices are not Google Cast receivers.
- Some codecs, subtitle combinations, and media containers may not be supported by the receiver or VLC's available transcoding modules.
- Select **Local playback** in Playback Output to return audio/video to the PC.
- No independent transcoding settings or quality selector yet.
- Successful discovery and playback depend on the installed VLC build and receiver compatibility.

