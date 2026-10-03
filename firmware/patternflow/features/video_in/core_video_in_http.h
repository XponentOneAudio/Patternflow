// ═══════════════════════════════════════════════════════════
// PatternFlow - video in: /api/video-in
//
//   GET  /api/video-in                    the same object /api/status carries
//                                         as "videoIn"
//   POST /api/video-in  universe=0&channels=510
//                                         the Art-Net layout, kept in NVS
//
// No console page: the two numbers are set once to match a sender, and the
// sender's own UI is where somebody is looking while they do it. curl or the
// TouchDesigner script sets them.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include "video_in_config.h"

#include "../../net_config.h"
#include "../../src/core_patterns_http.h"
#include "../../src/core_loop_sync.h"
#include "core_video_in.h"

#if PF_VIDEO_IN_ENABLED && PF_PATTERNS_HTTP_ENABLED
#include "../../src/webserver/WebServer.h"  // vendored: see src/webserver/VENDORED.md
#endif

namespace PatternflowVideoInHttp {

#if PF_VIDEO_IN_ENABLED && PF_PATTERNS_HTTP_ENABLED

inline WebServer& server() { return PatternflowPatternsHttp::server(); }
inline bool initialized = false;

inline void sendState(int code, const char* error = nullptr) {
  String json = error ? "{\"ok\":false,\"error\":\"" : "{\"ok\":true";
  if (error) {
    json += error;
    json += "\"";
  }
  PatternflowVideoIn::appendStatus(json);  // leading comma, as /api/status wants it
  json += "}";
  server().send(code, "application/json", json);
}

inline void handleGet() {
  PatternflowPatternsHttp::noteConsoleApiCall();
  sendState(200);
}

inline void configOnLoop() {
  long uni = PatternflowVideoIn::artnetUniverse;
  long ch = PatternflowVideoIn::artnetChannels;
  if (server().hasArg("universe")) uni = server().arg("universe").toInt();
  if (server().hasArg("channels")) ch = server().arg("channels").toInt();
  if (server().hasArg("on")) {
    const String& v = server().arg("on");
    PatternflowVideoIn::setRuntimeEnabled(v == "1" || v == "true" || v == "on");
  }
  if (!PatternflowVideoIn::setLayout(uni, ch)) {
    sendState(400, "universe 0-32767; channels a multiple of 3 up to 510, or 512");
    return;
  }
  sendState(200);
}

inline void handleConfig() {
  if (!PFLoopSync::run([] { configOnLoop(); })) PatternflowHttp::sendLoopStalled();
}

inline void begin() {
  if (initialized) return;
  if (!PatternflowWifi::linkUp()) return;
  server().on("/api/video-in", HTTP_GET, handleGet);
  server().on("/api/video-in", HTTP_POST, handleConfig);
  initialized = true;
}

#else

inline void begin() {}

#endif

}  // namespace PatternflowVideoInHttp
