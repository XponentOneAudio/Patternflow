// ═══════════════════════════════════════════════════════════
// Patternflow Video — which features this composition carries
//
// A panel that shows what a computer sends it: TouchDesigner, Resolume,
// MadMapper or a script streaming pixels over Wi-Fi as Art-Net or DDP. While
// a stream arrives it replaces the running pattern; when it stops, the
// pattern is back. OSC rides along because the same TouchDesigner patch that
// sends the picture usually wants the knobs too.
//
//   video_in  Art-Net :6454 and DDP :4048 into the frame on its way to the
//             panel - docs/video-in-spec.md
//   osc       knobs and pattern changes to and from the host
//
// Not on the shelf: a composition the tree keeps buildable.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "osc/feature_osc.h"
#include "video_in/feature_video_in.h"

// video_in composes the frame last-but-only; OSC only asks for patterns.
#define PF_FEATURE_LIST          \
  &PFFeatureOsc::descriptor,     \
      &PFFeatureVideoIn::descriptor
