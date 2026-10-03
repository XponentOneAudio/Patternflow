// ═══════════════════════════════════════════════════════════
// Patternflow Video — what this composition calls itself, and what it sets
//
// Included from config.h before any default, so anything `#ifndef`-guarded
// anywhere in the tree can be set here. Nothing in this file is a core
// file, and the build script puts it back the way it found it.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#define PF_VARIANT          "video"
#define PF_VARIANT_VERSION  "v0.1.0"

// Nothing else is changed. The radio stays at the conformance-tested setting
// and a panel switching to this build keeps its Wi-Fi, brightness and
// patterns.
