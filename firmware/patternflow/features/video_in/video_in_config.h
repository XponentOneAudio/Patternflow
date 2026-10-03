// ═══════════════════════════════════════════════════════════
// PatternFlow - video in feature: compile-time defaults
//
// Read only when a composition carries features/video_in/. Every value is
// #ifndef-guarded: patternflow_secrets.h (per device) and a composition's
// overrides.h (per edition) are included before this through config.h, so
// whatever they define wins and the lines below fill in the rest.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include "../../config.h"

#ifndef PF_VIDEO_IN_ENABLED
#define PF_VIDEO_IN_ENABLED 1
#endif

// Art-Net (ArtDmx). TouchDesigner's DMX Out CHOP speaks it with no code at
// all. 6454 is the port the Art-Net specification fixes; senders do not let
// you change it, so neither does this in practice.
#ifndef PF_VIDEO_IN_ARTNET_PORT
#define PF_VIDEO_IN_ARTNET_PORT 6454
#endif
// The Art-Net port-address (net << 8 | subnet << 4 | universe) that carries
// the top-left pixel, and the pixels each universe carries. Both are only
// where a fresh panel starts: POST /api/video-in changes them and NVS keeps
// them (namespace "pfvideo").
#ifndef PF_VIDEO_IN_ARTNET_UNIVERSE
#define PF_VIDEO_IN_ARTNET_UNIVERSE 0
#endif
// 170 is the convention (510 of 512 channels, so a pixel never straddles two
// universes) and what every LED-mapping tool assumes.
#ifndef PF_VIDEO_IN_ARTNET_PIXELS_PER_UNIVERSE
#define PF_VIDEO_IN_ARTNET_PIXELS_PER_UNIVERSE 170
#endif

// DDP (Distributed Display Protocol). Fewer, larger packets than Art-Net -
// 18 per frame against 49 - and a push flag that says where a frame ends.
// TouchDesigner sends it from a few lines of Python
// (integrations/touchdesigner/patternflow_stream.py); 4048 is DDP's port.
#ifndef PF_VIDEO_IN_DDP_PORT
#define PF_VIDEO_IN_DDP_PORT 4048
#endif

// How long after the last complete frame the stream is considered gone and
// the panel goes back to the running pattern. Long enough to ride out a
// Wi-Fi hiccup, short enough that stopping the sender feels like stopping.
#ifndef PF_VIDEO_IN_TIMEOUT_MS
#define PF_VIDEO_IN_TIMEOUT_MS 1500
#endif
