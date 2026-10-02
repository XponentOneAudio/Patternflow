// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Mandelbrot Lace
// Author:  Martyn Quickenden
// Lineage: original
//
// An endless, colour-cycling zoom into the Mandelbrot set. One of four
// (mandelbrot_zoom, _star, _antenna, _lace) that share this code and differ
// only in the constants under "The self-similar point".
//
// How it loops: the view is centred on the Misiurewicz point
// c0 = -0.60703102 + 0.60525138i, a lacy edge beside the period-3 bulb. The
// orbit of 0 under z^2 + c0 lands on a repelling period-3 cycle after 3
// steps, and near c0 the set is self-similar under multiplication by that
// cycle's multiplier lambda (|lambda| = 4.5425, arg = -69.92 deg). One loop
// is lambda itself:
// x4.542, turning -69.92 deg. That lands back on the picture it started
// from, and every pixel takes exactly 3 more iterations to escape. So the
// camera zooms through one loop, jumps back, and subtracts the extra
// iterations from the colour index. The seam differs from the previous frame
// by about as much as any two frames do.
//
// The similarity holds better the deeper the view, and float32 runs out of
// precision near 1e-5. SCALE0 sits between those two limits.
//
// Knobs:  1 zoom    — speed and direction (turn left to zoom out forever)
//         2 spin    — rotation, independent of the loop
//         3 cycle   — colour cycling speed
//         4 palette — electric blue, rainbow, neon, sunset fire
// Buttons reset their knob.
#include "pf_module.h"

namespace MandelbrotLace {
  const char* NAME = "Mandelbrot Lace";
  const char* KNOB_LABELS[4] = {"zoom", "spin", "cycle", "palette"};
  constexpr bool ABSOLUTE_READY = true;

  // --- The self-similar point and its loop step ---
  const float C0_RE = -0.607031022616088f;
  const float C0_IM = 0.605251381278934f;
  const float LOOP_LOG2_SCALE = 2.18348261f;  // log2 of the zoom per loop
  const float LOOP_ANGLE      = -1.22039965f;  // turn per loop, radians
  const float LOOP_ITERS      = 3.0f;  // extra escape iterations per loop
  const float SCALE0          = 1.9e-03f;  // half-width of the view at s = 0

  const int   MAX_ITER       = 160;
  const float BAILOUT2       = 256.0f;
  const float BAND_DENSITY   = 0.06f;  // palette turns per iteration
  const int   PALETTE_OFFSET = 3;  // palette the knob starts on

  // Zoom is in doublings per second, not loops, so all four feel equally fast.
  const float ZOOM_MIN  = -0.6f,  ZOOM_MAX  = 0.6f;   // zoom doublings per second
  const float SPIN_MIN  = -1.0f,  SPIN_MAX  = 1.0f;   // radians per second
  const float CYCLE_MIN = -1.0f,  CYCLE_MAX = 1.0f;   // palette turns per second
  const float ZOOM_DEF = 0.148f, SPIN_DEF = 0.0f, CYCLE_DEF = 0.25f;
  const int   PALETTE_COUNT = 4;

  // --- State ---
  float zoomRate = ZOOM_DEF;
  float spinRate = SPIN_DEF;
  float cycleRate = CYCLE_DEF;
  int   palette = 0;          // knob position; PALETTE_OFFSET picks the colours
  int   builtPalette = -1;
  float loopPhase = 0.0f;   // s in [0, 1): position inside one loop
  float spin = 0.0f;
  float hueShift = 0.0f;

  uint8_t lutR[256], lutG[256], lutB[256];

  // Cosine palettes, col(t) = a + b * cos(2pi (c t + d)), tuned bright and
  // saturated. c = 1 everywhere so each palette wraps seamlessly.
  struct CosPalette { float a[3], b[3], d[3]; };
  const CosPalette PALETTES[PALETTE_COUNT] = {
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.00f, 0.33f, 0.67f}},  // rainbow
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.80f, 0.90f, 0.30f}},  // neon magenta / cyan
    {{0.6f, 0.4f, 0.4f}, {0.4f, 0.5f, 0.5f}, {0.00f, 0.10f, 0.25f}},  // sunset fire
    {{0.4f, 0.5f, 0.6f}, {0.5f, 0.5f, 0.4f}, {0.50f, 0.30f, 0.10f}},  // electric blue
  };

  void buildPalette(int p) {
    const CosPalette& cp = PALETTES[(p + PALETTE_OFFSET) % PALETTE_COUNT];
    for (int i = 0; i < 256; i++) {
      float t = (float)i / 256.0f;
      float rgb[3];
      for (int k = 0; k < 3; k++) {
        float v = cp.a[k] + cp.b[k] * cosf(TWO_PI * (t + cp.d[k]));
        rgb[k] = constrain(v, 0.0f, 1.0f);
      }
      // Stretch so the brightest channel always reaches full: no dull bands.
      float m = max(rgb[0], max(rgb[1], rgb[2]));
      float gain = m > 0.05f ? 1.0f / m : 1.0f;
      lutR[i] = (uint8_t)(constrain(rgb[0] * gain, 0.0f, 1.0f) * 255.0f);
      lutG[i] = (uint8_t)(constrain(rgb[1] * gain, 0.0f, 1.0f) * 255.0f);
      lutB[i] = (uint8_t)(constrain(rgb[2] * gain, 0.0f, 1.0f) * 255.0f);
    }
    builtPalette = p;
  }

  void setup() {
    zoomRate = ZOOM_DEF;
    spinRate = SPIN_DEF;
    cycleRate = CYCLE_DEF;
    palette = 0;
    loopPhase = 0.0f;
    spin = 0.0f;
    hueShift = 0.0f;
    buildPalette(palette);
  }

  void update(float dt, const InputFrame& input) {
    PFParams::apply(input, 0, &zoomRate, ZOOM_MIN, ZOOM_MAX, (ZOOM_MAX - ZOOM_MIN) / 48.0f);
    if (input.btnPressed[0] && !input.paramAbsoluteActive[0] && !input.knobAudioActive[0]) zoomRate = ZOOM_DEF;

    PFParams::apply(input, 1, &spinRate, SPIN_MIN, SPIN_MAX, (SPIN_MAX - SPIN_MIN) / 48.0f);
    if (input.btnPressed[1] && !input.paramAbsoluteActive[1] && !input.knobAudioActive[1]) spinRate = SPIN_DEF;

    PFParams::apply(input, 2, &cycleRate, CYCLE_MIN, CYCLE_MAX, (CYCLE_MAX - CYCLE_MIN) / 48.0f);
    if (input.btnPressed[2] && !input.paramAbsoluteActive[2] && !input.knobAudioActive[2]) cycleRate = CYCLE_DEF;

    PFParams::applyIndex(input, 3, &palette, PALETTE_COUNT);
    if (input.btnPressed[3] && !input.paramAbsoluteActive[3] && !input.knobAudioActive[3]) palette = 0;

    loopPhase += dt * zoomRate / LOOP_LOG2_SCALE;
    loopPhase -= floorf(loopPhase);   // wraps both ways, so zooming out loops too

    spin += dt * spinRate;
    if (spin > TWO_PI) spin -= TWO_PI;
    if (spin < 0.0f)   spin += TWO_PI;

    hueShift += dt * cycleRate;
    hueShift -= floorf(hueShift);
  }

  void draw() {
    if (builtPalette != palette) buildPalette(palette);

    // Complex step per pixel: SCALE0 shrunk by s loops, rotated by spin.
    const float s = loopPhase;
    const float mag = SCALE0 * exp2f(-s * LOOP_LOG2_SCALE) / (PANEL_RES_W * 0.5f);
    const float ang = -s * LOOP_ANGLE + spin;
    const float kr = mag * cosf(ang);
    const float ki = mag * sinf(ang);

    // Colour index offset: deeper views escape later; remove that so the
    // palette lines up when s wraps from 1 back to 0.
    const float hueBase = hueShift - s * LOOP_ITERS * BAND_DENSITY;
    const float cx = PANEL_RES_W * 0.5f - 0.5f;
    const float cy = PANEL_RES_H * 0.5f - 0.5f;

    for (int y = 0; y < PANEL_RES_H; y++) {
      const float v = (float)y - cy;
      // Screen y points down; flip it so the spiral turns the usual way.
      // Each pixel's c is computed from the row start rather than accumulated:
      // 128 float adds would drift by a visible fraction of a pixel this deep.
      const float rowR = C0_RE + v * ki;
      const float rowI = C0_IM - v * kr;
      for (int x = 0; x < PANEL_RES_W; x++) {
        const float u = (float)x - cx;
        const float cr = rowR + u * kr;
        const float ci = rowI + u * ki;
        float zr = 0.0f, zi = 0.0f, zr2 = 0.0f, zi2 = 0.0f;
        int n = 0;
        while (n < MAX_ITER && zr2 + zi2 < BAILOUT2) {
          zi = 2.0f * zr * zi + ci;
          zr = zr2 - zi2 + cr;
          zr2 = zr * zr;
          zi2 = zi * zi;
          n++;
        }
        if (n >= MAX_ITER) {
          PFCanvas::setPixel(x, y, 0, 0, 0);
          continue;
        }
        // Smooth (continuous) escape count.
        float mu = (float)n + 1.0f - PFMath::fastLog2(0.5f * PFMath::fastLog2(zr2 + zi2));
        float h = mu * BAND_DENSITY + hueBase;
        int idx = (int)((h - floorf(h)) * 256.0f) & 255;
        PFCanvas::setPixel(x, y, lutR[idx], lutG[idx], lutB[idx]);
      }
    }

    PFCanvas::present();
  }
}  // namespace MandelbrotLace

PF_REGISTER_PATTERN(MandelbrotLace)
