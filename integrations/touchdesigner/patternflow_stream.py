"""Stream video to a Patternflow panel over Wi-Fi: DDP or Art-Net.

Two ways to use this file.

Inside TouchDesigner
--------------------
1. Put a 128x64 TOP somewhere (a Fit TOP or Resolution TOP set to 128 x 64
   in front of whatever you are making). Name it e.g. `out_panel`.
2. Drop this file into a Text DAT named `patternflow_stream` (or point a Text
   DAT's File parameter at it).
3. Add an Execute DAT, turn on "Frame End", and paste:

       sender = mod('patternflow_stream').Sender('192.168.1.50')

       def onFrameEnd(frame):
           sender.send_top(op('out_panel'))
           return

   with the panel's IP address (the device shows it on its NETWORK screen,
   and http://patternflow.local/api/status reports it).

TouchDesigner's TOP origin is bottom-left; `send_top` flips it so the top of
your TOP is the top of the panel.

From a terminal (no TouchDesigner) - a test pattern
---------------------------------------------------
    python patternflow_stream.py 192.168.1.50
    python patternflow_stream.py patternflow.local --protocol artnet --fps 30

Needs numpy for `send_top` and the test pattern; `send_rgb` takes raw bytes
and needs nothing.

The wire contract is docs/video-in-spec.md. License: MIT
"""

import socket
import struct
import time

WIDTH = 128
HEIGHT = 64
FRAME_BYTES = WIDTH * HEIGHT * 3

DDP_PORT = 4048
ARTNET_PORT = 6454

# 1440 bytes of pixels per DDP packet (480 pixels) keeps every datagram under
# a 1500-byte Ethernet/Wi-Fi MTU with headers to spare - 18 packets a frame.
DDP_CHUNK = 1440


class Sender:
    """Sends 128x64 RGB frames to one panel.

    protocol  "ddp" (default: fewer packets, an explicit end-of-frame) or
              "artnet" (what lighting software already speaks).
    universe  Art-Net only: the universe holding the top-left pixel. Must match
              the panel (POST /api/video-in universe=N; default 0).
    channels  Art-Net only: channels per universe. 510 (170 whole pixels) is the
              panel's default; 512 also works if the panel is set to match.
    """

    def __init__(self, host, protocol="ddp", universe=0, channels=510):
        self.host = socket.gethostbyname(host)
        self.protocol = protocol.lower()
        if self.protocol not in ("ddp", "artnet"):
            raise ValueError("protocol must be 'ddp' or 'artnet'")
        self.universe = int(universe)
        self.channels = int(channels)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.seq = 0

    # -- frames ------------------------------------------------------------

    def send_rgb(self, data):
        """Send one frame: WIDTH*HEIGHT*3 bytes, RGB, row-major from top-left."""
        data = bytes(data)
        if len(data) != FRAME_BYTES:
            raise ValueError("frame must be %d bytes, got %d" % (FRAME_BYTES, len(data)))
        if self.protocol == "ddp":
            self._send_ddp(data)
        else:
            self._send_artnet(data)

    def send_array(self, arr):
        """Send a numpy array shaped (HEIGHT, WIDTH, 3 or 4), uint8 or 0..1 float."""
        import numpy as np

        a = np.asarray(arr)
        if a.shape[0] != HEIGHT or a.shape[1] != WIDTH:
            raise ValueError("expected %dx%d, got %dx%d" % (WIDTH, HEIGHT, a.shape[1], a.shape[0]))
        a = a[:, :, :3]
        if a.dtype != np.uint8:
            a = (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        self.send_rgb(np.ascontiguousarray(a).tobytes())

    def send_top(self, top):
        """Send a TouchDesigner TOP. Must be 128x64; its bottom row is flipped
        to the panel's bottom (TOPs are stored bottom-up)."""
        arr = top.numpyArray(delayed=True)
        if arr is None:
            return
        self.send_array(arr[::-1])

    # -- protocols ---------------------------------------------------------

    def _send_ddp(self, data):
        # flags: version 1 (0x40), push (0x01) on the last packet only.
        # data type 0x0B = RGB, 8 bits per channel. destination id 1 = display.
        self.seq = (self.seq % 15) + 1
        for off in range(0, FRAME_BYTES, DDP_CHUNK):
            chunk = data[off:off + DDP_CHUNK]
            last = off + len(chunk) >= FRAME_BYTES
            header = struct.pack(">BBBBIH", 0x41 if last else 0x40, self.seq, 0x0B, 1, off, len(chunk))
            self.sock.sendto(header + chunk, (self.host, DDP_PORT))

    def _send_artnet(self, data):
        self.seq = (self.seq % 255) + 1
        uni = self.universe
        for off in range(0, FRAME_BYTES, self.channels):
            chunk = data[off:off + self.channels]
            if len(chunk) % 2:
                chunk += b"\x00"  # ArtDmx length must be even
            header = (
                b"Art-Net\x00"
                + struct.pack("<H", 0x5000)       # OpDmx
                + struct.pack(">H", 14)           # protocol version
                + bytes([self.seq, 0])            # sequence, physical
                + struct.pack("<H", uni & 0x7FFF) # SubUni, Net
                + struct.pack(">H", len(chunk))
            )
            self.sock.sendto(header + chunk, (self.host, ARTNET_PORT))
            uni += 1
        # ArtSync: tells the panel the frame is complete.
        sync = b"Art-Net\x00" + struct.pack("<H", 0x5200) + struct.pack(">H", 14) + b"\x00\x00"
        self.sock.sendto(sync, (self.host, ARTNET_PORT))


def _test_pattern(t):
    import numpy as np

    y, x = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    r = 0.5 + 0.5 * np.sin(x / 9.0 + t * 2.0)
    g = 0.5 + 0.5 * np.sin(y / 7.0 - t * 1.3)
    b = 0.5 + 0.5 * np.sin((x + y) / 13.0 + t * 0.7)
    img = np.stack([r, g, b], axis=-1)
    img[:2, :, :] = 1.0  # a white bar along the top: tells you which way is up
    return img


def main():
    import argparse

    ap = argparse.ArgumentParser(description="Stream a test pattern to a Patternflow panel.")
    ap.add_argument("host", help="the panel's IP address or patternflow.local")
    ap.add_argument("--protocol", default="ddp", choices=["ddp", "artnet"])
    ap.add_argument("--universe", type=int, default=0)
    ap.add_argument("--channels", type=int, default=510)
    ap.add_argument("--fps", type=float, default=30.0)
    args = ap.parse_args()

    sender = Sender(args.host, args.protocol, args.universe, args.channels)
    print("streaming %s to %s at %.0f fps - Ctrl-C to stop" % (args.protocol, sender.host, args.fps))
    start = time.time()
    period = 1.0 / args.fps
    try:
        while True:
            t0 = time.time()
            sender.send_array(_test_pattern(t0 - start))
            time.sleep(max(0.0, period - (time.time() - t0)))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
