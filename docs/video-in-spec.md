# Video in — wire contract

> **Status: experimental.** Carried by the `video` composition
> (`./firmware/bundles/build.sh video`), which is not on the shelf. Nothing
> here is promised to stay until it is.

A panel built with the `video_in` feature shows pixels a computer streams to it
over Wi-Fi. While a stream is arriving it **replaces** the running pattern on
its way to the panel; the pattern keeps running underneath, and about 1.5 s
after the last frame (`PF_VIDEO_IN_TIMEOUT_MS`) the pattern is back. The panel's
own brightness, gamma and white balance still apply to what you send.

Two standard UDP protocols are accepted, so anything that already drives LED
walls can drive the panel: TouchDesigner, Resolume, MadMapper, xLights, LedFx,
or a few lines of Python. A ready-made sender is
[`integrations/touchdesigner/patternflow_stream.py`](../integrations/touchdesigner/README.md).

## The frame

- **128 × 64 pixels** (the panel's `PANEL_RES_W` × `PANEL_RES_H`), 8-bit RGB,
  **row-major from the top-left**: byte `(y * 128 + x) * 3` is the red of
  pixel (x, y). One frame is 24,576 bytes.
- Send the frame at the panel's resolution. The panel does not scale.
- Use one protocol at a time. Both write into the same buffer.

## DDP — UDP port 4048

[Distributed Display Protocol](http://www.3waylabs.com/ddp/), version 1.

| byte | field | value |
| --- | --- | --- |
| 0 | flags | `0x40` (version 1), `\| 0x01` on the **last** packet of a frame (push), `\| 0x10` if a 4-byte timecode follows the header |
| 1 | sequence | anything; not checked |
| 2 | data type | `0x0B` (RGB, 8 bit) by convention; not checked |
| 3 | destination id | `1` by convention; not checked |
| 4–7 | data offset | big-endian, **in bytes** into the 24,576-byte frame |
| 8–9 | data length | big-endian, bytes of pixel data that follow |
| 10… | data | RGB bytes (from byte 14 with the timecode flag) |

A frame is shown when a packet with the push flag arrives. 1,440 bytes per
packet (18 packets a frame) stays under a 1,500-byte MTU. Query packets
(`0x02`) are ignored; the panel sends nothing back.

## Art-Net — UDP port 6454

`ArtDmx` (OpCode `0x5000`) and `ArtSync` (`0x5200`). No `ArtPoll` reply: send
to the panel's IP address (unicast), not broadcast.

- The universe holding the **top-left pixel** is the *start universe*
  (default `0`); every following universe carries the next run of bytes.
  Universe here is the full 15-bit port-address (`Net << 8 | SubUni`).
- **Channels per universe** is `510` by default — 170 whole pixels, the LED
  mapping convention — so a frame is 49 universes (0–48). A sender that fills
  all 512 channels of each universe, a pixel straddling the seam, works too
  with the panel set to `512` (48 universes).
- **When a frame is shown:** if the sender has ever sent `ArtSync`, on each
  `ArtSync`. Otherwise, when the universe holding the last pixel arrives — so
  send universes in ascending order, which every sender does.

Both settings are on the panel, kept across reboots:

```bash
curl -X POST "http://patternflow.local/api/video-in" -d "universe=0&channels=510"
```

## `/api/video-in`

`GET` returns, and `/api/status` carries as `"videoIn"`:

```json
{"on":true,"live":true,"source":"ddp","fps":29.8,
 "artnetPort":6454,"artnetUniverse":0,"artnetChannels":510,"universes":49,
 "ddpPort":4048,"width":128,"height":64}
```

`POST` takes `universe` (0–32767), `channels` (a multiple of 3 up to 510, or
512) and `on` (`1`/`0`). A value out of range is refused with `400`, not
clamped. `on` is the same switch as the **VID** row on the device's NETWORK
screen; off, the panel ignores every packet.

`/api/status` lists `"video_in"` in `caps` when the feature is compiled in.

## Throughput

Expect 25–30 fps on a decent 2.4 GHz network. The panel draws its own loop at
its own rate and shows the newest complete frame each time, so sending faster
than the panel draws only costs airtime. Frames are lost, not queued: a late
frame is replaced by the next one rather than played out late.

## Implementation

`firmware/patternflow/features/video_in/`. Packets are taken off the network on
lwIP's task (AsyncUDP) into one of three buffers; a complete frame is handed to
the render loop by pointer swap, and the frame on the panel is never one the
network can write into. The core is unchanged: the feature uses the existing
`composeFrame` hook.
