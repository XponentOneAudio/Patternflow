# TouchDesigner → Patternflow video

## What it does

Streams a TOP from TouchDesigner to a Patternflow panel over Wi-Fi, at up to
about 30 fps. While frames arrive the panel shows them in place of its pattern;
when they stop, the pattern is back after about 1.5 s. `patternflow_stream.py`
is a short Python sender that runs inside TouchDesigner, and also runs on its
own from a terminal to test a panel with no TouchDesigner at all.

## Contract it uses

[`docs/video-in-spec.md`](../../docs/video-in-spec.md): DDP on UDP 4048
(the default here) or Art-Net on UDP 6454, and `/api/video-in`.

## Requirements

- A panel running firmware that carries the `video_in` feature: the `video`
  composition, `./firmware/bundles/build.sh video` (experimental, not on the
  shelf yet). `http://<panel>/api/status` lists `"video_in"` in `caps` when it is
  there.
- TouchDesigner 2022 or newer (it bundles Python 3 and numpy).
- The computer and the panel on the same network, and not a guest network that
  keeps clients apart.

## Install

1. Create a **Text DAT** named `patternflow_stream`, and either paste in
   [`patternflow_stream.py`](patternflow_stream.py) or set the DAT's *File*
   parameter to it.
2. At the end of your chain, put a **Fit TOP** set to 128 × 64 (or any TOP with
   *Output Resolution* → *Custom*, 128 × 64). Name it `out_panel`. The panel
   does not scale, so a TOP of any other size is refused.

## Connect

1. Find the panel's IP address: the NETWORK screen on the device shows it, or
   read `ip` from `http://patternflow.local/api/status`.
2. Create an **Execute DAT**, turn on *Frame End*, and replace its contents with:

   ```python
   sender = mod('patternflow_stream').Sender('192.168.1.50')  # the panel's IP

   def onFrameEnd(frame):
       sender.send_top(op('out_panel'))
       return
   ```

That is all. The **VID** row on the device's NETWORK screen switches video in
off and on at the panel. More than 30 fps rarely helps: lower the project's cook
rate, or send every other frame (`if frame % 2 == 0: ...`).

### Art-Net instead of DDP

```python
sender = mod('patternflow_stream').Sender('192.168.1.50', protocol='artnet')
```

Sends 49 universes of 170 pixels from universe 0, then `ArtSync`. Lighting
software that already speaks Art-Net, TouchDesigner's own **DMX Out CHOP**
included, can drive the panel without this script. Send RGB bytes row-major
from the top-left, unicast to the panel's IP. If the sender packs a full 512
channels into each universe, or starts at a universe other than 0, tell the
panel:

```bash
curl -X POST "http://patternflow.local/api/video-in" -d "universe=0&channels=512"
```

DDP is the recommendation: 18 packets a frame against 50, and a frame boundary
the protocol states.

### Testing without TouchDesigner

```bash
pip install numpy
python integrations/touchdesigner/patternflow_stream.py 192.168.1.50
python integrations/touchdesigner/patternflow_stream.py patternflow.local --protocol artnet
```

This streams a moving colour field with a white bar along the top edge, so you
can see that the picture is the right way up.

## Troubleshooting

- **Nothing shows.** `GET /api/video-in`: is `on` true, and is `live` true?
  If `live` is false, no frames are arriving. Check the IP, the network, and
  that the TOP is exactly 128 × 64 (the Textport shows the error otherwise).
- **Upside down.** Use `send_top`, which flips TouchDesigner's bottom-up rows,
  rather than passing `numpyArray()` to `send_array` yourself.
- **Scrambled stripes (Art-Net).** The panel's `channels`/`universe` don't
  match the sender. See above.
- **Choppy.** Wi-Fi airtime. Send less often, move the panel nearer the access
  point, or switch to DDP.
- **Colours look different from the TouchDesigner viewer.** The panel's own
  gamma, white balance and brightness apply to video too, on purpose: they are
  set to make the LEDs look right.

## Tested with

The packet formats were checked against the firmware's own parser on a host
(DDP; Art-Net at 510 and 512 channels per universe), pixel for pixel. **Not
yet run against a panel or inside TouchDesigner.** Please say which firmware
and TouchDesigner build you used when you try it.

## License

MIT ([`LICENSE-MIT`](../../LICENSE-MIT)).
