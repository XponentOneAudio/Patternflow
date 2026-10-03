// ═══════════════════════════════════════════════════════════
// PatternFlow - video in: frames from TouchDesigner (or anything) over Wi-Fi
//
// Two UDP listeners, both standard pixel-streaming protocols so the sender
// needs nothing Patternflow-specific:
//
//   Art-Net  :6454  ArtDmx, 170 RGB pixels per universe, row-major from the
//                   top-left. TouchDesigner's DMX Out CHOP sends it natively.
//   DDP      :4048  RGB bytes at a byte offset; the push flag ends a frame.
//                   Fewer packets, and a frame boundary the protocol states.
//
// What arrives replaces the pattern on its way to the panel (composeFrame),
// so the panel's own gamma, white balance and brightness still apply, and
// the pattern underneath keeps running - stop the sender and it is back
// within PF_VIDEO_IN_TIMEOUT_MS.
//
// ── Why AsyncUDP and three buffers ──────────────────────────────────────
//
// A frame is 24,576 bytes: 49 Art-Net packets or 18 DDP ones, in a burst.
// Polling a WiFiUDP socket once per frame from the loop drops most of that
// burst - lwIP's per-socket receive queue holds a handful of datagrams, and
// the loop is away drawing for ~10 ms at a time. AsyncUDP hands each packet
// over on lwIP's own task the moment it lands, so nothing queues.
//
// That puts the writer on another task, so the buffers are three, each with
// one owner at a time: the network task fills `rx`; a complete frame swaps
// `rx` with `ready`; the loop task, in compose(), swaps `ready` with `shown`
// and blits `shown`. Only pointer swaps happen under the lock, never a copy,
// and the buffer being blitted is never one the network task can reach.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include "video_in_config.h"

#include <Arduino.h>
#include "config.h"
#include "core_mem.h"

#if PF_VIDEO_IN_ENABLED
#include <AsyncUDP.h>
#include <Preferences.h>
#include <WiFi.h>
#endif

namespace PatternflowVideoIn {

#if PF_VIDEO_IN_ENABLED

constexpr int W = PANEL_RES_W;
constexpr int H = PANEL_RES_H;
constexpr size_t FRAME_BYTES = (size_t)W * H * 3;

// The Art-Net layout: which port-address holds the top-left pixel, and how
// many channels of each universe carry pixels. 510 (170 whole pixels) is the
// LED-mapping convention; 512 is what a sender that just chops one long
// channel list into universes produces, a pixel straddling the seam. Set on
// /api/video-in and kept in NVS, because a prebuilt image cannot be rebuilt
// to match somebody's patch. Written on the loop task, read on the network
// task; word-sized, so a read is never torn.
inline volatile int artnetUniverse = PF_VIDEO_IN_ARTNET_UNIVERSE;
inline volatile int artnetChannels = PF_VIDEO_IN_ARTNET_PIXELS_PER_UNIVERSE * 3;

inline int universesFor(int ch) { return (int)((FRAME_BYTES + ch - 1) / ch); }
inline int universes() { return universesFor(artnetChannels); }

inline AsyncUDP artnet;
inline AsyncUDP ddp;
inline bool listening = false;
inline bool runtimeEnabled = true;

inline uint8_t* rx = nullptr;     // network task writes here
inline uint8_t* ready = nullptr;  // last complete frame, waiting to be shown
inline uint8_t* shown = nullptr;  // loop task blits this; nobody else touches it
inline bool readyFresh = false;
// Network task only: pixels have landed in rx since the last commit. Without
// it a frame boundary with nothing new behind it - ArtSync after the last
// universe already committed, a DDP push with no data - swaps a stale buffer
// in front of the fresh one.
inline bool rxDirty = false;
inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Written on the network task, read on the loop task. Word-sized, so a torn
// read cannot happen; a stale one is a frame late, which is harmless.
inline volatile uint32_t lastFrameMs = 0;
inline volatile uint32_t framesIn = 0;
inline volatile uint8_t lastSource = 0;  // 0 none, 1 Art-Net, 2 DDP
// Art-Net: set once a sender has used ArtSync, after which only ArtSync
// commits a frame (the sender is telling us where frames end). Cleared when
// the stream times out, so a different sender starts from scratch.
inline volatile bool artSyncSeen = false;

// Fps is measured on the loop task from framesIn, once a second.
inline uint32_t fpsWindowMs = 0;
inline uint32_t fpsWindowFrames = 0;
inline float fps = 0;

// Network task. The frame in rx is complete: hand it to the loop.
inline void commit(uint8_t source) {
  if (!rxDirty) return;
  rxDirty = false;
  portENTER_CRITICAL(&lock);
  uint8_t* t = ready;
  ready = rx;
  rx = t;
  readyFresh = true;
  portEXIT_CRITICAL(&lock);
  lastFrameMs = millis();
  lastSource = source;
  framesIn = framesIn + 1;
}

inline bool live() {
  const uint32_t last = lastFrameMs;
  return last != 0 && (millis() - last) < PF_VIDEO_IN_TIMEOUT_MS;
}

// ── Art-Net ─────────────────────────────────────────────────────────────
//
// Header: "Art-Net\0", OpCode (little-endian), ProtVer (big-endian), then
// for ArtDmx: Sequence, Physical, SubUni, Net, Length (big-endian), data.
constexpr uint16_t OP_DMX = 0x5000;
constexpr uint16_t OP_SYNC = 0x5200;

inline void onArtnet(AsyncUDPPacket& packet) {
  if (!runtimeEnabled || !rx) return;
  const uint8_t* p = packet.data();
  const size_t len = packet.length();
  if (len < 10 || memcmp(p, "Art-Net\0", 8) != 0) return;
  const uint16_t op = (uint16_t)p[8] | ((uint16_t)p[9] << 8);

  if (op == OP_SYNC) {
    artSyncSeen = true;
    commit(1);
    return;
  }
  if (op != OP_DMX || len < 18) return;

  const int portAddress = ((int)(p[15] & 0x7f) << 8) | p[14];
  const int perUniverse = artnetChannels;  // read once: the layout may change mid-packet
  const int count = universesFor(perUniverse);
  const int slot = portAddress - artnetUniverse;
  if (slot < 0 || slot >= count) return;

  size_t dmxLen = ((size_t)p[16] << 8) | p[17];
  if (dmxLen > len - 18) dmxLen = len - 18;
  if (dmxLen > (size_t)perUniverse) dmxLen = (size_t)perUniverse;

  const size_t off = (size_t)slot * perUniverse;
  if (off >= FRAME_BYTES) return;
  if (dmxLen > FRAME_BYTES - off) dmxLen = FRAME_BYTES - off;
  memcpy(rx + off, p + 18, dmxLen);
  rxDirty = true;

  // No ArtSync from this sender: the universe holding the last pixel ends
  // the frame. Senders go through their universes in ascending order, so
  // this is the last packet of each frame.
  if (!artSyncSeen && slot == count - 1) commit(1);
}

// ── DDP ─────────────────────────────────────────────────────────────────
//
// Header (10 bytes, 14 with the timecode flag): flags, sequence, data type,
// destination id, data offset (32-bit BE, in bytes), data length (16-bit
// BE), [timecode]. Flags: version in the top two bits (01), 0x10 timecode,
// 0x02 query, 0x01 push - "this packet completes the frame".
constexpr uint8_t DDP_PUSH = 0x01;
constexpr uint8_t DDP_QUERY = 0x02;
constexpr uint8_t DDP_TIMECODE = 0x10;

inline void onDdp(AsyncUDPPacket& packet) {
  if (!runtimeEnabled || !rx) return;
  const uint8_t* p = packet.data();
  const size_t len = packet.length();
  if (len < 10) return;
  const uint8_t flags = p[0];
  if ((flags & 0xc0) != 0x40) return;   // DDP v1 only
  if (flags & DDP_QUERY) return;        // discovery: not answered, ignored
  const size_t header = (flags & DDP_TIMECODE) ? 14 : 10;
  if (len < header) return;

  const size_t off = ((size_t)p[4] << 24) | ((size_t)p[5] << 16) |
                     ((size_t)p[6] << 8) | p[7];
  size_t n = ((size_t)p[8] << 8) | p[9];
  if (n > len - header) n = len - header;
  if (off < FRAME_BYTES) {
    if (n > FRAME_BYTES - off) n = FRAME_BYTES - off;
    memcpy(rx + off, p + header, n);
    rxDirty = true;
  }
  if (flags & DDP_PUSH) commit(2);
}

// ── Lifecycle ───────────────────────────────────────────────────────────

inline bool allocate() {
  if (rx) return true;
  rx = (uint8_t*)PFMem::alloc(FRAME_BYTES);
  ready = (uint8_t*)PFMem::alloc(FRAME_BYTES);
  shown = (uint8_t*)PFMem::alloc(FRAME_BYTES);
  if (rx && ready && shown) return true;
  free(rx);
  free(ready);
  free(shown);
  rx = ready = shown = nullptr;
  Serial.println("[VIDEO] no memory for frame buffers");
  return false;
}

// Whole pixels per universe (a multiple of 3 up to 510), or the full 512.
inline bool validChannels(long ch) {
  return ch == 512 || (ch >= 3 && ch <= 510 && ch % 3 == 0);
}

inline void loadConfig() {
  Preferences prefs;
  if (prefs.begin("patternflow", true)) {
    runtimeEnabled = prefs.getBool("video_in_rt", true);
    prefs.end();
  }
  if (prefs.begin("pfvideo", true)) {
    const int uni = prefs.getInt("uni", PF_VIDEO_IN_ARTNET_UNIVERSE);
    const int ch = prefs.getInt("ch", PF_VIDEO_IN_ARTNET_PIXELS_PER_UNIVERSE * 3);
    if (uni >= 0 && uni <= 0x7fff) artnetUniverse = uni;
    if (validChannels(ch)) artnetChannels = ch;
    prefs.end();
  }
}

// Loop task (the HTTP handler runs there). Out-of-range values are refused
// rather than clamped: a layout that silently differs from the one asked for
// is a scrambled picture with no explanation.
inline bool setLayout(long uni, long ch) {
  if (uni < 0 || uni > 0x7fff || !validChannels(ch)) return false;
  artnetUniverse = (int)uni;
  artnetChannels = (int)ch;
  artSyncSeen = false;
  Preferences prefs;
  if (prefs.begin("pfvideo", false)) {
    prefs.putInt("uni", (int)uni);
    prefs.putInt("ch", (int)ch);
    prefs.end();
  }
  return true;
}

inline void setRuntimeEnabled(bool on) {
  runtimeEnabled = on;
  if (!on) lastFrameMs = 0;  // hand the panel back at once
  Preferences prefs;
  if (prefs.begin("patternflow", false)) {
    prefs.putBool("video_in_rt", on);
    prefs.end();
  }
}

inline bool isRuntimeEnabled() { return runtimeEnabled; }

// Every Wi-Fi connect edge. AsyncUDP binds to any address, so the sockets
// survive a reconnect; this only has to open them once.
inline void begin() {
  if (listening || !allocate()) return;
  bool a = artnet.listen(PF_VIDEO_IN_ARTNET_PORT);
  bool d = ddp.listen(PF_VIDEO_IN_DDP_PORT);
  if (a) artnet.onPacket(onArtnet);
  if (d) ddp.onPacket(onDdp);
  listening = a || d;
  Serial.printf("[VIDEO] listening art-net:%d ddp:%d at %s\n",
                a ? PF_VIDEO_IN_ARTNET_PORT : 0, d ? PF_VIDEO_IN_DDP_PORT : 0,
                WiFi.localIP().toString().c_str());
}

// Loop task, once a frame.
inline void tick() {
  const uint32_t now = millis();
  if (now - fpsWindowMs >= 1000) {
    const uint32_t f = framesIn;
    fps = (f - fpsWindowFrames) * 1000.0f / (float)(now - fpsWindowMs);
    fpsWindowFrames = f;
    fpsWindowMs = now;
  }
  if (!live()) artSyncSeen = false;
}

// Loop task, inside present(). Show the newest complete frame while the
// stream is live; null otherwise, which leaves the pattern on the panel.
inline const uint8_t* compose(const uint8_t*, int w, int h) {
  if (!runtimeEnabled || !shown || w != W || h != H || !live()) return nullptr;
  portENTER_CRITICAL(&lock);
  if (readyFresh) {
    uint8_t* t = shown;
    shown = ready;
    ready = t;
    readyFresh = false;
  }
  portEXIT_CRITICAL(&lock);
  return shown;
}

inline void appendStatus(String& json) {
  json += ",\"videoIn\":{\"on\":";
  json += runtimeEnabled ? "true" : "false";
  json += ",\"live\":";
  json += live() ? "true" : "false";
  json += ",\"source\":\"";
  json += lastSource == 1 ? "artnet" : lastSource == 2 ? "ddp" : "";
  json += "\",\"fps\":";
  json += String(live() ? fps : 0.0f, 1);
  json += ",\"artnetPort\":";
  json += PF_VIDEO_IN_ARTNET_PORT;
  json += ",\"artnetUniverse\":";
  json += (int)artnetUniverse;
  json += ",\"artnetChannels\":";
  json += (int)artnetChannels;
  json += ",\"universes\":";
  json += universes();
  json += ",\"ddpPort\":";
  json += PF_VIDEO_IN_DDP_PORT;
  json += ",\"width\":";
  json += W;
  json += ",\"height\":";
  json += H;
  json += "}";
}

#else  // !PF_VIDEO_IN_ENABLED

inline void loadConfig() {}
inline bool setLayout(long, long) { return false; }
inline void begin() {}
inline void tick() {}
inline bool isRuntimeEnabled() { return false; }
inline void setRuntimeEnabled(bool) {}
inline const uint8_t* compose(const uint8_t*, int, int) { return nullptr; }
inline void appendStatus(String&) {}

#endif

}  // namespace PatternflowVideoIn
