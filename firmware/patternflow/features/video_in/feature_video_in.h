// ═══════════════════════════════════════════════════════════
// PatternFlow - video in, as a feature
//
// Pixels from TouchDesigner (or Resolume, MadMapper, xLights, a Python
// script - anything that speaks Art-Net or DDP) shown on the panel over
// Wi-Fi. While a stream is arriving it replaces the pattern; when it stops,
// the pattern is back. The wire contract is docs/video-in-spec.md.
//
// Hooks: setup, onNetwork (the UDP listeners and /api/video-in), loop,
// appendStatus, composeFrame, and the runtime-toggle trio that puts it on the
// device's NETWORK screen as "VID".
// Core edits: none - composeFrame already hands a feature the whole frame.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "../pf_feature.h"
#include "core_video_in.h"
#include "core_video_in_http.h"

namespace PFFeatureVideoIn {

inline void setup() { PatternflowVideoIn::loadConfig(); }

inline void onNetwork() {
  PatternflowVideoIn::begin();
  PatternflowVideoInHttp::begin();
}

inline void loop(const PFFeatureFrame&) { PatternflowVideoIn::tick(); }

inline void appendStatus(String& json) { PatternflowVideoIn::appendStatus(json); }

inline const uint8_t* composeFrame(const uint8_t* frame, int w, int h) {
  return PatternflowVideoIn::compose(frame, w, h);
}

inline bool isRuntimeEnabled() { return PatternflowVideoIn::isRuntimeEnabled(); }
inline void setRuntimeEnabled(bool on) { PatternflowVideoIn::setRuntimeEnabled(on); }

inline const PFFeature descriptor = {
    "video_in",
    "video_in",    // cap - the site and the lab probe for this
    setup,
    onNetwork,
    loop,
    nullptr,       // observeFrame
    nullptr,       // fillInput
    nullptr,       // onUserInput
    nullptr,       // claimsPattern - the pattern keeps running underneath
    nullptr,       // takePattern
    nullptr,       // onSleep
    nullptr,       // requestSleep
    "VID",         // shortName - the device NETWORK screen row
    isRuntimeEnabled,
    setRuntimeEnabled,
    appendStatus,
    nullptr,       // drawOverlay - this feature composes instead
    nullptr,       // navPath - no console page
    nullptr,       // navLabel
    nullptr,       // navDesc
    composeFrame,
};

}  // namespace PFFeatureVideoIn
