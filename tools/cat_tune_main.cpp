// cat_tune_main.cpp — host wrapper for cat_proc.h. Built by tools/cat_tune.py; not a PIO env
// (a non-test main() in the native env fights build_src_filter — spec §Tuning rig).
// stdout: "<GW> <GH> <P>\n" header, then GW*GH*3 raw RGB bytes, then plain-text diagnostics
// (incl. an ASCII index map for agents). The header exists so the page sizes its canvas from
// the binary — hardcoding the grid there meant every CAT_GW/GH change smeared the preview.
#define CAT_TUNE 1          // enables the body-yaw override seam in cat_proc.h (rig-only)
#include "../cat_proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  CatPreset p = CAT_PRESET[0];
  CatRenderState s; catInit(s);
  float advanceMs = 0;
  // Lamp controls. Azimuth/elevation are far easier to steer than three raw Q8 components,
  // and contrast spreads the band edges around the middle one (0 = flat, 1 = shipped).
  float lightDeg = 227.6f, lightEl = 35.3f, contrast = 1.0f;
  catTuneReset();                 // every pose slot to NaN = "the keyframe decides"
  // CatPose field order, and the names the page uses. Index IS the float offset into CatPose,
  // so this list must stay in declaration order — the static_assert on CAT_POSE_FLOATS catches a
  // field being added, this comment is what catches one being REORDERED.
  static const char* PF[CAT_POSE_FLOATS] = {
    "bodyX", "bodyY", "bodyRx", "bodyRy", "breathe",
    "headDx", "headDy", "headR", "bodyYaw", "tilt", "poseHeadYaw",
    "earL", "earR",
    "tailBase", "tailCurl", "tailSwish",
    "legLift0", "legLift1", "legLift2", "legLift3", "legFwd",
    "eyeOpen", "pupilDx", "pupilDy", "mouthOpen",
  };
  struct { const char* k; float* f; } F[] = {
    {"chub", &p.bodyChub}, {"head", &p.headSize}, {"ear", &p.earLen},
    {"earPoint", &p.earPoint}, {"tailLen", &p.tailLen}, {"fluff", &p.tailFluff}, {"eye", &p.eyeShape},
    {"breatheRate", &p.breatheRate}, {"tailRate", &p.tailRate},
    {"phase", &s.phase}, {"blend", &s.blend}, {"aTail", &s.aTail}, {"aBreathe", &s.aBreathe},
    {"headYaw", &s.headYaw}, {"headYawTarget", &s.headYawTarget}, {"blinkT", &s.blinkT}, {"twitchT", &s.twitchT},
    {"rearX", &catLegRearX}, {"frontX", &catLegFrontX}, {"hock", &catLegHock},
    {"rearSet", &catLegRearSet}, {"pawScale", &catLegPawScale}, {"ground", &catLegGround},
    {"earInner", &catEarInner}, {"eyeK", &catEyeK}, {"earYaw", &catEarYawGain},
    {"lightDeg", &lightDeg}, {"lightEl", &lightEl}, {"contrast", &contrast},
    {"advanceMs", &advanceMs},
  };
  bool sawHeadYaw = false, sawHeadYawTarget = false;
  for (int i = 1; i < argc; i++) {
    char* eq = strchr(argv[i], '='); if (!eq) continue;
    *eq = 0; const char* k = argv[i]; float v = (float)atof(eq + 1);
    if (!strcmp(k, "headYaw")) sawHeadYaw = true;
    if (!strcmp(k, "headYawTarget")) sawHeadYawTarget = true;
    if (!strcmp(k, "anim")) {          // page sends anim first; later headYaw args still override
      s.cur = (CatAnim)(int)v;
      s.headYaw = s.headYawTarget = CAT_POSE[s.cur].yaw;
    }
    else if (!strcmp(k, "marking")) p.marking = (uint8_t)v;
    else if (!strcmp(k, "flat")) p.flat = (uint8_t)v;
    else if (!strcmp(k, "outline")) p.outline = (uint8_t)v;
    else if (!strcmp(k, "mirror")) s.mirror = v > 0.5f;
    else if (!strncmp(k, "pal", 3)) { int pi = atoi(k + 3); if (pi >= 0 && pi < 16) p.pal[pi] = (uint16_t)(int)v; }
    else {
      bool hit = false;
      for (int j = 0; j < CAT_POSE_FLOATS && !hit; j++)         // pose overrides win the name
        if (!strcmp(k, PF[j])) { catTunePose[j] = v; hit = true; }
      for (unsigned j = 0; j < sizeof(F) / sizeof(F[0]) && !hit; j++)
        if (!strcmp(k, F[j].k)) { *F[j].f = v; hit = true; }
    }
  }
  // A headYaw the page set with no explicit target means "hold this facing". Without it
  // catAdvance slews straight back to CAT_POSE[cur].yaw the moment you press play, so the
  // slider silently stopped meaning anything during playback.
  if (sawHeadYaw && !sawHeadYawTarget) s.headYawTarget = s.headYaw;
  // The HTTP renderer is stateless, so it cannot inherit catSetPose's frozen transition source.
  // Seed blendFrom from the idle pose instead. In idle this makes blend a safe no-op; for another
  // selected pose it previews the intended idle -> pose transition rather than lerping from an
  // all-zero CatPose and shrinking the cat into the upper-left corner.
  if (s.blend < 1.0f) {
    CatRenderState from; catInit(from);
    from.aTail = s.aTail; from.aBreathe = s.aBreathe;
    s.blendFrom = catEvalCur(p, from);
  }
  {
    const float RAD = 3.14159265f / 180.0f;
    float az = lightDeg * RAD, el = lightEl * RAD;
    catLX = (int)(cosf(el) * cosf(az) * 256.0f);
    catLY = (int)(cosf(el) * sinf(az) * 256.0f);
    catLZ = (int)(sinf(el) * 256.0f);
    catBandHi  = 132 + (int)((225 - 132) * contrast);   // spread the outer edges around the
    catBandLo  = 132 - (int)((132 -  44) * contrast);   // middle one: 0 = flat, 1 = shipped
  }
  for (float t = 0; t < advanceMs; t += 16.0f) catAdvance(s, p, 16.0f);   // play path: real catAdvance, no JS port
  static uint8_t grid[CAT_GH][CAT_GW];
  static CatDiagnostics diag;
  catRender(grid, p, s, &diag);
  printf("%d %d %d\n", CAT_GW, CAT_GH, CAT_P);
  uint8_t row[CAT_GW * 3];
  for (int y = 0; y < CAT_GH; y++) {
    for (int x = 0; x < CAT_GW; x++) {
      uint16_t c = p.pal[grid[y][x]];
      if (grid[y][x] == CI_TRANS) { row[x*3] = 30; row[x*3+1] = 34; row[x*3+2] = 48; }   // visible bg
      else { row[x*3] = (uint8_t)((c >> 11) << 3); row[x*3+1] = (uint8_t)(((c >> 5) & 0x3F) << 2); row[x*3+2] = (uint8_t)((c & 0x1F) << 3); }
    }
    fwrite(row, 1, sizeof(row), stdout);
  }
  // ---- diagnostics (spec §Agent legibility): assertions, not pixels ----
  printf("clipped=%02X (T%d R%d B%d F%d E%d H%d M%d)\n", diag.clipped,
         !!(diag.clipped & (1u << CATP_TAIL)),
         !!(diag.clipped & (1u << CATP_REAR_LEGS)),
         !!(diag.clipped & (1u << CATP_BODY)),
         !!(diag.clipped & (1u << CATP_FRONT_LEGS)),
         !!(diag.clipped & (1u << CATP_EARS)),
         !!(diag.clipped & (1u << CATP_HEAD)),
         !!(diag.clipped & (1u << CATP_FACE)));
  printf("bbox=%u,%u..%u,%u  fill=%u/%d  headBodyOverlap=%u\n",
         diag.bboxX0, diag.bboxY0, diag.bboxX1, diag.bboxY1, diag.cellsFilled, CAT_GW * CAT_GH,
         diag.headBodyOverlap);
  static const char* PN[CAT_PART_COUNT] = {
    "tail", "rearLegs", "body", "frontLegs", "ears", "head", "face"
  };
  printf("occluded:");
  for (int i = 0; i < CAT_PART_COUNT; i++) printf(" %s=%u", PN[i], diag.occluded[i]);
  printf("\npose: anim=%d phase=%.3f blend=%.2f headYaw=%.2f bodyYaw=%.2f aTail=%.3f aBreathe=%.3f\n",
         (int)s.cur, s.phase, s.blend, s.headYaw, catEval(p, s).yaw, s.aTail, s.aBreathe);
  // Every CatPose float, by name, AFTER blending and the accumulators — i.e. the pose actually
  // rendered. The page mirrors these into any pose slider it did not send, so an untouched slider
  // shows what the keyframe chose rather than a JS default that exists nowhere.
  {
    CatPose q = catEval(p, s);
    const float* Q = (const float*)&q;
    printf("poseAll:");
    for (int i = 0; i < CAT_POSE_FLOATS; i++) printf(" %s=%.3f", PF[i], Q[i]);
    printf("\n\n");
  }
  printf("legRig: rearX=%.2f frontX=%.2f hock=%.2f rearSet=%.2f pawScale=%.2f ground=%.2f earInner=%.2f eyeK=%.3f earYaw=%.2f\n\n",
         catLegRearX, catLegFrontX, catLegHock, catLegRearSet, catLegPawScale, catLegGround,
         catEarInner, catEyeK, catEarYawGain);
  // CAT_PRESET[0] as the page's own struct syntax, so cat_tune.html can SEED from the preset the
  // binary was compiled with instead of hand-copying it into JS. That copy drifted twice in one
  // session: once silently serving the old cat, once with a slider range that could not even reach
  // the new value. Deliberately CAT_PRESET[0] and not `p` — `p` carries this frame's arg overrides,
  // and seeding from those would make every render redefine the baseline.
  // ONE line: the page captures it with /^preset: (.*)$/m.
  {
    const CatPreset& P0 = CAT_PRESET[0];
    printf("preset: { %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %d, %d, %d, %.2ff, %.2ff, { ",
           P0.bodyChub, P0.headSize, P0.earLen, P0.earPoint, P0.tailLen, P0.tailFluff, P0.eyeShape,
           (int)P0.marking, (int)P0.flat, (int)P0.outline, P0.breatheRate, P0.tailRate);
    for (int i = 0; i < 16; i++) printf("0x%04X%s", P0.pal[i], i < 15 ? ", " : "");
    printf(" } }\n\n");
  }
  for (int y = 0; y < CAT_GH; y++) {               // ASCII index map: '.'=transparent, hex digit=palette slot
    for (int x = 0; x < CAT_GW; x++) putchar(grid[y][x] ? "0123456789ABCDEF"[grid[y][x]] : '.');
    putchar('\n');
  }
  return 0;
}
