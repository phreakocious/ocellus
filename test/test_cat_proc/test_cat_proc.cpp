#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "../../cat_proc.h"

// guard-banded grid: the fills must never write outside the CAT_GW x CAT_GH interior
static struct { uint8_t pre[64]; uint8_t g[CAT_GH][CAT_GW]; uint8_t post[64]; } G;
static CatDiagnostics D;

static CatRaster fresh() {
  memset(&G, 0xA5, sizeof(G));
  memset(&G.g, 0, sizeof(G.g));
  memset(&D, 0, sizeof(D));
  D.bboxX0 = CAT_GW; D.bboxY0 = CAT_GH;
  CatRaster r; r.g = G.g; r.d = &D; r.part = CATP_BODY; r.clipTo = 0; r.clipR = 0; r.remap = false; r.far = false; r.contact = false; r.darken = false; r.furOnly = false; r.rim = false; r.mirror = false; r.ramp = nullptr;
  return r;
}
static void checkGuards() {
  for (int i = 0; i < 64; i++) { TEST_ASSERT_EQUAL_UINT8(0xA5, G.pre[i]); TEST_ASSERT_EQUAL_UINT8(0xA5, G.post[i]); }
}

void test_ellipse_fills_center_and_stays_in_bbox() {
  CatRaster r = fresh();
  catEllipse(r, 34, 23, 10, 6, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[23][34]);   // center
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[23][44]);   // +rx on the axis
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[17][44]);   // corner outside the ellipse
  TEST_ASSERT_EQUAL_UINT16(24, D.bboxX0);
  TEST_ASSERT_EQUAL_UINT16(44, D.bboxX1);
  TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
  checkGuards();
}

void test_offgrid_ellipse_sets_clip_flag_only() {
  CatRaster r = fresh(); r.part = CATP_EARS;
  catEllipse(r, 2, 2, 6, 6, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(1u << CATP_EARS, D.clipped);
  checkGuards();
}

void test_capsule_r0_is_continuous_line() {
  CatRaster r = fresh();
  catCapsule(r, 5, 5, 20, 12, 0, CI_OUTLINE);
  for (int x = 5; x <= 20; x++) {                   // x-major Bresenham: one cell per column
    int hits = 0;
    for (int y = 0; y < CAT_GH; y++) if (G.g[y][x] == CI_OUTLINE) hits++;
    TEST_ASSERT_EQUAL_INT(1, hits);
  }
  checkGuards();
}

void test_capsule_thick_has_round_caps() {
  CatRaster r = fresh();
  catCapsule(r, 20, 20, 30, 20, 3, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[20][17]);   // cap extends r beyond the endpoint
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[23][25]);   // r below the shaft
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[23][17]);   // corner outside the round cap
  checkGuards();
}

void test_tri_fills_and_ignores_winding() {
  CatRaster r = fresh();
  catTri(r, 10, 30, 30, 30, 20, 10, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[28][20]);   // interior
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[12][10]);   // outside the slanted edge
  CatRaster r2 = fresh();                            // reversed winding fills the same cells
  catTri(r2, 10, 30, 20, 10, 30, 30, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[28][20]);
  checkGuards();
}

// Hull of three discs = Minkowski(triangle, disc): the triangle's interior PLUS a rad-wide band
// around every edge. Rounded corners come free, which is what the artist asked for, and the sides
// are straight tangents -- so no radius STEP exists anywhere, which is what the nipple was.
void test_rtri_fills_interior_and_rounds_corners() {
  CatRaster r = fresh();
  catRTri(r, 40, 60, 60, 60, 50, 30, 4, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[55][50]);   // deep interior
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[60][38]);   // grown past the base-left corner by rad
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[27][50]);   // grown past the tip by rad
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[26][38]);   // outside the hull: corner of the bbox
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[64][50]);   // ON the base band: perp dist exactly rad,
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[65][50]);   // and the test is <=, so 64 in / 65 out
  // Straight tangent side, not a bulge -- the whole reason this primitive exists (it replaces
  // two coaxial capsules whose radius step read as a visible artefact). Left edge runs (40,60)-
  // (50,30); at y=44 the perpendicular distance is 3162/1000=3.16 at x=42 (inside rad 4) and
  // 4111/1000=4.11 at x=41 (outside) -- a future refactor that rounds or bulges the side fails here.
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[44][42]);
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[44][41]);
  checkGuards();
}

// A degenerate hull (all three points coincident) is a disc, not a crash or an empty fill --
// catTri bails on zero area, so the edge-distance pass has to carry it alone.
void test_rtri_degenerate_is_a_disc() {
  CatRaster r = fresh();
  catRTri(r, 50, 50, 50, 50, 50, 50, 3, CI_FUR_2);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[50][50]);
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[50][53]);
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[50][54]);
  checkGuards();
}

// One shape means ONE shading frame, so no seam can exist inside it -- the failure that forced the
// coaxial-capsule ear was two overlapping primitives disagreeing about the normal.
void test_rtri_shades_as_one_gradient() {
  CatRaster r = fresh();
  shRTri(r, 40, 60, 60, 60, 50, 30, 4, CAT_FUR);
  int bands = 0;
  bool seen[CAT_BANDS] = { false, false, false, false };
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
    int b = catRampBand(CAT_FUR, G.g[y][x]);
    if (b >= 0 && !seen[b]) { seen[b] = true; bands++; }
  }
  TEST_ASSERT_GREATER_THAN_INT(1, bands);           // it is a gradient, not a flat fill
  checkGuards();
}

// The rim pass under-draws a grown CI_OUTLINE silhouette, same contract as shEllipse/shCapsule.
void test_rtri_rim_grows_the_silhouette() {
  CatRaster r = fresh(); r.rim = true;
  shRTri(r, 40, 60, 60, 60, 50, 30, 4, CAT_FUR);
  TEST_ASSERT_EQUAL_UINT8(CI_OUTLINE, G.g[60][37]);   // still inside the rad-4 hull: pins the colour
  TEST_ASSERT_EQUAL_UINT8(CI_OUTLINE, G.g[60][35]);   // pins the +1: 35 is OUTSIDE the rad-4 hull,
                                                      // so this fails if rim mode stops growing
  checkGuards();
}

void test_masked_remap_stays_inside_fur() {
  CatRaster r = fresh();
  catEllipse(r, 34, 23, 8, 6, CI_FUR_1);            // lit-fur island
  r.remap = true;
  catCapsule(r, 20, 23, 48, 23, 1, CI_ACC_1);       // stripe wider than the island
  r.remap = false;
  TEST_ASSERT_EQUAL_UINT8(CI_ACC_1, G.g[23][34]);   // recolored inside
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[23][20]);   // nothing outside
  checkGuards();
}

void test_clipto_writes_only_over_target_index() {
  CatRaster r = fresh();
  catEllipse(r, 30, 23, 6, 6, CI_FUR_2);
  r.clipTo = CI_FUR_2;
  catEllipse(r, 29, 22, 6, 6, CI_FUR_1);            // second fill, shifted
  r.clipTo = 0;
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_1, G.g[22][29]);   // inside both -> overwritten
  TEST_ASSERT_EQUAL_UINT8(CI_FUR_2, G.g[23][36]);   // first fill survives where the second missed
  TEST_ASSERT_EQUAL_UINT8(0,        G.g[16][29]);   // shifted-only cell: clipped, not painted
  checkGuards();
}

void test_occlusion_and_overlap_counters() {
  CatRaster r = fresh(); r.part = CATP_BODY;
  catEllipse(r, 30, 25, 8, 8, CI_FUR_2);
  r.part = CATP_HEAD;
  catEllipse(r, 36, 25, 8, 8, CI_FUR_2);
  TEST_ASSERT_TRUE(D.headBodyOverlap > 0);
  TEST_ASSERT_TRUE(D.occluded[CATP_BODY] > 0);
  TEST_ASSERT_EQUAL_UINT16(D.headBodyOverlap, D.occluded[CATP_BODY]);
}

void test_idle_render_in_bounds_and_nonempty() {
  CatRenderState s; catInit(s);
  memset(&G, 0xA5, sizeof(G));
  memset(&D, 0, sizeof(D));
  catRender(G.g, CAT_PRESET[0], s, &D);
  TEST_ASSERT_EQUAL_UINT8(0, D.clipped);      // nothing off-grid in the shipped idle
  TEST_ASSERT_TRUE(D.cellsFilled > 300);      // and it isn't (near-)empty
  TEST_ASSERT_TRUE(D.headBodyOverlap > 0);    // head sits on the body
  TEST_ASSERT_TRUE(D.occluded[CATP_REAR_LEGS] > 0); // body genuinely covers both rear upper limbs
  TEST_ASSERT_TRUE(D.occluded[CATP_BODY] > D.headBodyOverlap); // forelegs also genuinely cover body
  checkGuards();
}

void test_meow_cycle_has_readable_vocal_beats() {
  const CatPose start = catKeyEval(CA_MEOW, 0.00f);
  const CatPose prep  = catKeyEval(CA_MEOW, 0.14f);
  const CatPose voice = catKeyEval(CA_MEOW, 0.30f);
  const CatPose vowel = catKeyEval(CA_MEOW, 0.52f);
  const CatPose end   = catKeyEval(CA_MEOW, 1.00f);

  // The loop joins on the exact idle pose, then anticipates closed-mouth before it calls.
  TEST_ASSERT_EQUAL_MEMORY(&P_IDLE, &start, sizeof(CatPose));
  TEST_ASSERT_EQUAL_MEMORY(&P_IDLE, &end, sizeof(CatPose));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, prep.mouthOpen);
  TEST_ASSERT_TRUE(prep.headDy > P_IDLE.headDy);             // tiny chin tuck

  // The vocal beat must be more than an idle pose with a dark dot pasted onto it.
  TEST_ASSERT_TRUE(voice.mouthOpen >= 0.35f && voice.mouthOpen <= 0.50f);
  TEST_ASSERT_TRUE(voice.eyeOpen < 0.80f);                   // soft effort/squint
  TEST_ASSERT_TRUE(voice.headDy < prep.headDy - 1.5f);       // visible head lift
  TEST_ASSERT_TRUE(voice.tilt < -0.05f);
  TEST_ASSERT_TRUE(voice.earL > 0.0f && voice.earR < 0.0f); // asymmetric living-ear response

  // The held vowel changes shape instead of freezing on one cel.
  TEST_ASSERT_TRUE(vowel.mouthOpen > 0.25f);
  TEST_ASSERT_TRUE(fabsf(vowel.mouthOpen - voice.mouthOpen) > 0.05f);
  TEST_ASSERT_TRUE(fabsf(vowel.eyeOpen - voice.eyeOpen) > 0.03f);
}

void test_meow_cycle_stays_in_bounds_with_a_complete_face() {
  CatPreset p = CAT_PRESET[0];
  memset(&G, 0xA5, sizeof(G));
  for (int outline = 0; outline <= 1; outline++) {
    p.outline = (uint8_t)outline;
    for (int i = 0; i <= 40; i++) {
      CatRenderState s; catInit(s);
      s.cur = CA_MEOW; s.phase = (float)i / 40.0f; s.blend = 1.0f;
      s.headYaw = s.headYawTarget = CAT_POSE[CA_MEOW].yaw;
      memset(&D, 0, sizeof(D));
      catRender(G.g, p, s, &D);
      TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
      TEST_ASSERT_TRUE(D.cellsFilled > 300);
      CatPose q = catEval(p, s);
      TEST_ASSERT_TRUE(q.mouthOpen >= 0.0f && q.mouthOpen <= 0.50f);
      int nose = 0, mouth = 0;
      for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
        nose += G.g[y][x] == CI_NOSE;
        mouth += G.g[y][x] == CI_MOUTH;
      }
      TEST_ASSERT_TRUE(nose > 0);
      TEST_ASSERT_TRUE(mouth > 0);
    }
  }
  checkGuards();
}

void test_sleep_cycle_is_a_quiet_loaf_with_one_dream_flick() {
  const CatPose start = catKeyEval(CA_SLEEPING, 0.00f);
  const CatPose hold  = catKeyEval(CA_SLEEPING, 0.55f);
  const CatPose dream = catKeyEval(CA_SLEEPING, 0.62f);
  const CatPose end   = catKeyEval(CA_SLEEPING, 1.00f);

  TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &start, sizeof(CatPose));
  TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &hold, sizeof(CatPose));
  TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &end, sizeof(CatPose)); // exact loop join
  TEST_ASSERT_TRUE(start.yaw > 0.80f);                       // horizontal/profile body
  TEST_ASSERT_TRUE(start.bodyRy < P_IDLE.bodyRy);
  TEST_ASSERT_TRUE(start.headR < P_IDLE.headR);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, start.eyeOpen);     // the caret, not the 0.1 black bar
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, start.mouthOpen);

  // The three constraints that make this a loaf instead of a potato. Each has a wrong-looking
  // neighbour one small edit away, and none of them is visible in a clip flag or a cell count.
  TEST_ASSERT_TRUE(start.headDx > 0.0f);                     // front is +x; at -x the face is on the rump
  TEST_ASSERT_TRUE(start.headDy < -start.bodyRy);            // head centre clears the body's top edge
  TEST_ASSERT_TRUE(start.tailBase > 3.0f && start.tailBase < 3.45f);  // root leaves flat along the ground
  TEST_ASSERT_TRUE(start.tailCurl >= 0.8f);                  // distal joints complete a visible coil
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(start.legLift[i] > 0.4f);   // paws tucked, not standing

  // A single restrained dream beat; the rest of the five seconds is deliberately still.
  TEST_ASSERT_TRUE(dream.headDy > start.headDy);
  TEST_ASSERT_TRUE(dream.earL > 0.0f && start.earL < 0.0f);
  TEST_ASSERT_TRUE(dream.earR < 0.0f && start.earR > 0.0f);
  // The dream reaches a paw too: one tucked forepaw flexes with the ear flick, but stays a
  // tucked far-plane paw -- above the loaf's 0.4 floor, below the near gate.
  TEST_ASSERT_TRUE(dream.legLift[0] < start.legLift[0] - 0.05f);
  TEST_ASSERT_TRUE(dream.legLift[0] > 0.4f);

  CatRenderState awake; catInit(awake); awake.aTail = 1.5707963f;
  CatRenderState asleep; catInit(asleep);
  asleep.cur = CA_SLEEPING; asleep.aTail = 1.5707963f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.00f, catEval(CAT_PRESET[0], awake).tailSwish);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.12f, catEval(CAT_PRESET[0], asleep).tailSwish);
}

// EVERY sleep entry must damp the tail, not just the two that shipped first. tailSwish is written
// from the accumulator AFTER the keyframe lerp, so the exact-base hand-off check above cannot see
// it: CA_CURL_UP passed that test byte-for-byte while still swishing at full awake gain, and the
// 1.00 -> 0.12 drop at the hand-off snapped the tail by up to 119 cells depending on where in the
// cycle the entry happened to land. Assert the gain itself, at the layer that applies it.
void test_every_sleep_entry_damps_the_tail_before_the_loaf() {
  const CatAnim entries[] = { CA_YAWNING, CA_NODDING, CA_CURL_UP, CA_WAKING };
  for (unsigned k = 0; k < sizeof entries / sizeof entries[0]; k++) {
    CatRenderState s; catInit(s);
    s.cur = entries[k];
    s.phase = 1.00f; s.aTail = 1.5707963f;   // sin(aTail) = 1, so tailSwish IS the gain
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.35f, catEval(CAT_PRESET[0], s).tailSwish);
  }
  // The pounce damps harder still: "tail straight" is the pose, and the idle wag dissolves it.
  CatRenderState st; catInit(st);
  st.cur = CA_POUNCE; st.phase = 0.32f; st.aTail = 1.5707963f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, catEval(CAT_PRESET[0], st).tailSwish);
}

// The stretch's hold IS the pose. It shipped passing through full extension in ~350 ms; the beat
// only reads as a held stretch with room around it, so assert the span and the clip together --
// widening one without the other silently trades the hold against the approach.
void test_stretch_holds_full_extension_long_enough() {
  TEST_ASSERT_EQUAL_UINT16(2600, CAT_POSE[CA_STRETCHING].durMs);
  const CatKey* K; int n; catKeysGet(CA_STRETCHING, K, n);
  // The hold is the span at FULL extension -- keyed by the deepest bodY, not by identical keys:
  // the two hold keys deliberately differ in mouthOpen, so the stretch keeps opening its mouth
  // while the body stays put.
  float deepest = P_IDLE.bodyY;
  for (int i = 0; i < n; i++) if (K[i].p.bodyY > deepest) deepest = K[i].p.bodyY;
  float holdFrom = -1.0f, holdTo = -1.0f;
  for (int i = 0; i < n; i++)
    if (K[i].p.bodyY >= deepest - 0.001f) { if (holdFrom < 0.0f) holdFrom = K[i].t; holdTo = K[i].t; }
  TEST_ASSERT_TRUE(holdTo > holdFrom);
  const float holdMs = (holdTo - holdFrom) * (float)CAT_POSE[CA_STRETCHING].durMs;
  TEST_ASSERT_TRUE(holdMs > 500.0f);                // ~570; the old 0.46-0.62 of 2200 was ~350
  // ...and the reach into it stays as unhurried as it was, rather than paying for the hold.
  TEST_ASSERT_TRUE(holdFrom * (float)CAT_POSE[CA_STRETCHING].durMs > 1000.0f);
}

// The pounce is four beats in one clip, and each one is load-bearing: without the crouch it is a
// hop, without the wiggle it is a jump, without the tuck the leap reads as a cat on stilts.
void test_pounce_crouches_wiggles_then_leaves_the_ground() {
  const CatPose crouch = catKeyEval(CA_POUNCE, 0.32f);
  const CatPose jump   = catKeyEval(CA_POUNCE, 0.74f);

  // Down low, eyes narrow -- but not shut, which would read as a blink rather than a stalk.
  TEST_ASSERT_TRUE(crouch.bodyY > P_IDLE.bodyY + 5.0f);
  TEST_ASSERT_TRUE(crouch.bodyRy < P_IDLE.bodyRy - 4.0f);
  // Squinted as far as the rasterizer allows: 0.60 is the floor of the "properly open" band that
  // test_authored_faces_avoid_the_bar_band_and_the_muzzle_hole enforces, and the leap's snap back
  // to 1.00 is where the narrowing actually reads from.
  TEST_ASSERT_TRUE(crouch.eyeOpen <= 0.62f && crouch.eyeOpen >= 0.60f);
  // Haunches gathered under a body turned enough to show them at all.
  TEST_ASSERT_TRUE(crouch.legLift[2] > 0.25f && crouch.legLift[3] > 0.25f);
  TEST_ASSERT_TRUE(crouch.yaw > P_IDLE.yaw + 0.20f);
  // Tail straight: no coil, and laid back rather than idle's raised curl.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, crouch.tailCurl);
  TEST_ASSERT_TRUE(crouch.tailBase > P_IDLE.tailBase + 0.30f);

  // The wiggle sways BOTH ways around the crouch and trades the haunches, three beats.
  const CatPose w1 = catKeyEval(CA_POUNCE, 0.40f);
  const CatPose w2 = catKeyEval(CA_POUNCE, 0.48f);
  const CatPose w3 = catKeyEval(CA_POUNCE, 0.56f);
  TEST_ASSERT_TRUE(w1.bodyX < crouch.bodyX && w3.bodyX < crouch.bodyX);
  TEST_ASSERT_TRUE(w2.bodyX > crouch.bodyX);
  TEST_ASSERT_TRUE((w1.legLift[2] - w1.legLift[3]) * (w2.legLift[2] - w2.legLift[3]) < 0.0f);
  TEST_ASSERT_TRUE(w1.bodyX > crouch.bodyX - 4.0f);   // a tell, not a stagger

  // The leap: the torso goes UP and the eyes snap open.
  TEST_ASSERT_TRUE(jump.bodyY < P_IDLE.bodyY - 8.0f);
  TEST_ASSERT_TRUE(jump.bodyY < crouch.bodyY - 15.0f);
  TEST_ASSERT_TRUE(jump.eyeOpen > 0.95f);
  TEST_ASSERT_TRUE(jump.legLift[0] > 0.30f);         // paws tucked, not trailing on stilts

  // Every lift in every key stays under the gate. Past it a foreleg switches to the
  // reach-for-the-muzzle geometry and the airborne frames render as a cat covering its face.
  const CatKey* K; int n; catKeysGet(CA_POUNCE, K, n);
  for (int i = 0; i < n; i++)
    for (int e = 0; e < 4; e++)
      TEST_ASSERT_TRUE(K[i].p.legLift[e] < CAT_LEG_NEAR_GATE);
}

void test_sleep_cycle_stays_in_bounds_through_breathing_extremes() {
  const float breath[] = { 0.0f, 1.5707963f, 4.7123890f };   // neutral, fullest, shallowest
  CatPreset p = CAT_PRESET[0];
  memset(&G, 0xA5, sizeof(G));
  for (int outline = 0; outline <= 1; outline++) {
    p.outline = (uint8_t)outline;
    for (int bi = 0; bi < 3; bi++)
      for (int i = 0; i <= 20; i++) {
        CatRenderState s; catInit(s);
        s.cur = CA_SLEEPING; s.phase = (float)i / 20.0f; s.blend = 1.0f;
        s.aBreathe = breath[bi]; s.aTail = 1.5707963f;         // widest body / max tucked-tail swish
        s.headYaw = s.headYawTarget = CAT_POSE[CA_SLEEPING].yaw;
        memset(&D, 0, sizeof(D));
        catRender(G.g, p, s, &D);
        TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
        TEST_ASSERT_TRUE(D.cellsFilled > 300);
        int eyes = 0, nose = 0, mouth = 0;
        for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
          eyes  += G.g[y][x] == CI_EYE;
          nose  += G.g[y][x] == CI_NOSE;
          mouth += G.g[y][x] == CI_MOUTH;
        }
        TEST_ASSERT_TRUE(eyes > 0);
        TEST_ASSERT_TRUE(nose > 0);
        TEST_ASSERT_TRUE(mouth > 0);
      }
  }
  checkGuards();
}

// The "concentric potato" has now been authored twice -- once in the first sleeping pose and
// again in the first stretch -- because dropping bodyY without tracking headDy down with it slides
// the head's centre below the body's top edge, at which point the two ellipses rasterize as one
// blob with a face on its rim and nothing in the diagnostics complains. Every keyframe of every
// animation owes this clearance, so assert it across the whole table rather than per pose.
void test_no_keyframe_buries_the_head_in_the_body() {
  for (int a = 0; a < CA_COUNT; a++) {
    const CatKey* K; int n; catKeysGet((CatAnim)a, K, n);
    for (int i = 0; i < n; i++)
      TEST_ASSERT_TRUE_MESSAGE(K[i].p.headDy < -K[i].p.bodyRy,
                               "head centre must sit above the body's top edge");
  }
}

// Once poses blend back to their mood base when they end, so a last key that is merely close to
// that base shows up as a hitch at the hand-off. Sleep-entry gestures deliberately end on
// P_SLEEP; every other once pose returns to P_IDLE.
void test_every_once_pose_returns_exactly_to_its_mood_base() {
  const CatAnim idleReturn[] = {
    CA_MEOW, CA_LICKING, CA_STRETCHING, CA_ITCH, CA_KNEADING, CA_BEGGING,
    CA_PLEASE, CA_TAIL_HUG, CA_SLOWBLINK, CA_SNIFF, CA_WAKING,
    CA_GROOM_FACE, CA_GROOM_FORELEG, CA_GROOM_BELLY, CA_ADORE,
    CA_HEAD_BUNT, CA_PROTEST, CA_POUNCE
  };
  for (unsigned k = 0; k < sizeof idleReturn / sizeof idleReturn[0]; k++) {
    const CatPose end = catKeyEval(idleReturn[k], 1.00f);
    TEST_ASSERT_EQUAL_MEMORY(&P_IDLE, &end, sizeof(CatPose));
  }
  const CatAnim sleepReturn[] = { CA_YAWNING, CA_NODDING, CA_CURL_UP };
  for (unsigned k = 0; k < sizeof sleepReturn / sizeof sleepReturn[0]; k++) {
    const CatPose end = catKeyEval(sleepReturn[k], 1.00f);
    TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &end, sizeof(CatPose));
  }
}

void test_mood_gestures_have_distinct_body_beats() {
  const CatPose kneadL = catKeyEval(CA_KNEADING, 0.30f);
  const CatPose kneadR = catKeyEval(CA_KNEADING, 0.58f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, kneadL.eyeOpen);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, kneadR.eyeOpen);
  TEST_ASSERT_TRUE(kneadL.legLift[0] > kneadL.legLift[1] + 0.20f);
  TEST_ASSERT_TRUE(kneadR.legLift[1] > kneadR.legLift[0] + 0.20f);
  TEST_ASSERT_TRUE(kneadL.legLift[0] < CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(kneadR.legLift[1] < CAT_LEG_NEAR_GATE);

  const CatPose beg1 = catKeyEval(CA_BEGGING, 0.30f);
  const CatPose beg2 = catKeyEval(CA_BEGGING, 0.58f);
  TEST_ASSERT_TRUE(beg1.legLift[0] > CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(beg2.legLift[0] > beg1.legLift[0]);
  TEST_ASSERT_TRUE(beg1.mouthOpen > 0.35f && beg2.mouthOpen > 0.35f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, beg1.legLift[1]);  // one asking paw, not both

  const CatPose yawn = catKeyEval(CA_YAWNING, 0.34f);
  const CatPose settle = catKeyEval(CA_YAWNING, 0.74f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, yawn.eyeOpen);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, yawn.mouthOpen);
  TEST_ASSERT_TRUE(settle.bodyY > P_IDLE.bodyY);
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(settle.legLift[i] > 0.30f);

  const CatPose please = catKeyEval(CA_PLEASE, 0.48f);
  TEST_ASSERT_TRUE(please.legLift[0] > CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(please.legLift[1] > CAT_LEG_NEAR_GATE);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, please.legLift[0], please.legLift[1]);
  TEST_ASSERT_TRUE(please.mouthOpen > 0.15f);             // joined paws plus a quiet plea

  const CatPose hug = catKeyEval(CA_TAIL_HUG, 0.52f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, hug.eyeOpen);
  TEST_ASSERT_TRUE(hug.tailCurl < P_IDLE.tailCurl - 0.35f);
  TEST_ASSERT_TRUE(hug.bodyRy > P_IDLE.bodyRy + 1.0f);    // visible purr-squish, not only a tail

  const CatPose nod1 = catKeyEval(CA_NODDING, 0.24f);
  const CatPose nod2 = catKeyEval(CA_NODDING, 0.50f);
  const CatPose nod3 = catKeyEval(CA_NODDING, 0.76f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, nod1.eyeOpen);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, nod2.eyeOpen);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, nod3.eyeOpen);
  TEST_ASSERT_TRUE(nod2.bodyY > nod1.bodyY && nod3.bodyY > nod2.bodyY);
  TEST_ASSERT_TRUE(nod2.headDx > nod1.headDx && nod3.headDx > nod2.headDx);
}

void test_grooming_variants_have_their_own_repeated_beats() {
  const float lickAt[]  = { 0.12f, 0.38f, 0.64f };
  const float dipAt[]   = { 0.18f, 0.44f, 0.70f };
  const float swipeAt[] = { 0.24f, 0.50f, 0.76f };
  const float pauseAt[] = { 0.30f, 0.56f, 0.82f };
  for (unsigned i = 0; i < sizeof lickAt / sizeof lickAt[0]; i++) {
    const CatPose lick = catKeyEval(CA_GROOM_FACE, lickAt[i]);
    const CatPose dip = catKeyEval(CA_GROOM_FACE, dipAt[i]);
    const CatPose swipe = catKeyEval(CA_GROOM_FACE, swipeAt[i]);
    const CatPose pause = catKeyEval(CA_GROOM_FACE, pauseAt[i]);
    TEST_ASSERT_TRUE(lick.mouthOpen >= 0.20f);
    TEST_ASSERT_TRUE(dip.headDy > lick.headDy + 2.0f);
    TEST_ASSERT_TRUE(dip.legLift[0] < lick.legLift[0] - 0.20f);
    TEST_ASSERT_TRUE(swipe.legLift[0] > dip.legLift[0] + 0.25f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, swipe.eyeOpen);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, swipe.mouthOpen);
    TEST_ASSERT_EQUAL_MEMORY(&swipe, &pause, sizeof(CatPose));
  }

  int armLicks = 0;
  for (unsigned i = 1; i + 1 < sizeof K_GROOM_FORELEG / sizeof K_GROOM_FORELEG[0]; i++) {
    const CatPose& a = K_GROOM_FORELEG[i - 1].p;
    const CatPose& b = K_GROOM_FORELEG[i].p;
    const CatPose& c = K_GROOM_FORELEG[i + 1].p;
    TEST_ASSERT_TRUE(b.legLift[0] > CAT_LEG_NEAR_GATE);
    if (b.mouthOpen > a.mouthOpen && b.mouthOpen > c.mouthOpen && b.mouthOpen >= 0.20f)
      armLicks++;
  }
  TEST_ASSERT_EQUAL_INT(4, armLicks);

  const CatPose belly = catKeyEval(CA_GROOM_BELLY, 0.50f);
  TEST_ASSERT_TRUE(belly.bodyY > P_IDLE.bodyY + 8.0f);
  TEST_ASSERT_TRUE(belly.bodyRx > P_IDLE.bodyRx + 4.0f);
  TEST_ASSERT_TRUE(belly.bodyRy < P_IDLE.bodyRy - 4.0f);
  TEST_ASSERT_TRUE(belly.yaw > 0.70f);
  TEST_ASSERT_TRUE(belly.tilt > 0.60f);
  TEST_ASSERT_TRUE(belly.legLift[2] > CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(belly.mouthOpen >= 0.20f);
}

// Marking 3 = stripes + blaze, the watercolor reference's coat: tabby ticks and tail rings
// PLUS a light chest. The blaze is a remap (recolors fur bands toward the ramp's bright end),
// so it must never change the silhouette; the tabby accents must survive alongside it.
void test_blaze_marking_lightens_the_chest_and_keeps_the_tabby() {
  CatRenderState s; catInit(s);
  static uint8_t a[CAT_GH][CAT_GW], b[CAT_GH][CAT_GW];
  CatPreset p = CAT_PRESET[0];
  p.marking = 1; catRender(a, p, s, nullptr);
  p.marking = 3; catRender(b, p, s, nullptr);
  int occ = 0, accA = 0, accB = 0, whiteA = 0, whiteB = 0;
  for (int y = 0; y < CAT_GH; y++)
    for (int x = 0; x < CAT_GW; x++) {
      occ += (a[y][x] != CI_TRANS) != (b[y][x] != CI_TRANS);
      accA += a[y][x] >= CI_ACC_0 && a[y][x] <= CI_ACC_3;
      accB += b[y][x] >= CI_ACC_0 && b[y][x] <= CI_ACC_3;
      whiteA += a[y][x] == CI_HILITE;
      whiteB += b[y][x] == CI_HILITE;
    }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, occ, "blaze must not change the silhouette");
  TEST_ASSERT_TRUE_MESSAGE(accB > 0, "the tabby must survive the blaze");
  TEST_ASSERT_TRUE_MESSAGE(accB >= accA - 8, "blaze should not eat the tabby accents");
  // white beyond the two catchlights: the bib itself, not just a pale fringe
  TEST_ASSERT_TRUE_MESSAGE(whiteB > whiteA + 10, "the white bib must actually paint");
}

// The adoring gaze is the reference art's signature beat: catchlight riding HIGH in the solid
// chibi eye (pupilDy negative = looking up at the human), the head cocked into a tilt and
// lifted, held wide-eyed, then a soft half-blink that KEEPS the gaze instead of dropping it.
// Face-led like the slow blink: the body must not move.
void test_adore_gazes_up_with_a_head_tilt() {
  TEST_ASSERT_TRUE(CAT_POSE[CA_ADORE].durMs >= 2000);
  const CatPose hold = catKeyEval(CA_ADORE, 0.40f);
  TEST_ASSERT_TRUE(hold.pupilDy < -1.0f);
  TEST_ASSERT_TRUE(hold.tilt > 0.10f);
  TEST_ASSERT_TRUE(hold.headDy < P_IDLE.headDy - 0.5f);
  TEST_ASSERT_TRUE(hold.eyeOpen >= 0.95f);
  const CatPose melt = catKeyEval(CA_ADORE, 0.68f);       // the happy-blink caret mid-gaze
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, melt.eyeOpen);
  const CatPose reopen = catKeyEval(CA_ADORE, 0.80f);     // eyes come back STILL looking up
  TEST_ASSERT_TRUE(reopen.eyeOpen >= 0.90f);
  TEST_ASSERT_TRUE(reopen.pupilDy < -0.9f);
  const CatKey* K; int n; catKeysGet(CA_ADORE, K, n);
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyX, K[i].p.bodyX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyY, K[i].p.bodyY);
    for (int l = 0; l < 4; l++)
      TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.legLift[l], K[i].p.legLift[l]);
  }
}

// Sixth pass: each new gesture owes one dominant silhouette/beat that separates it from the
// older mood pool, plus the same exact-base hand-off checked table-wide above.
void test_sixth_pass_has_one_distinct_beat_per_mood() {
  const CatPose bunt = catKeyEval(CA_HEAD_BUNT, 0.46f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, bunt.eyeOpen);
  TEST_ASSERT_TRUE(bunt.bodyX + bunt.headDx > P_IDLE.bodyX + P_IDLE.headDx + 5.0f);
  TEST_ASSERT_TRUE(bunt.tilt > 0.25f);
  TEST_ASSERT_TRUE(bunt.tailCurl > P_IDLE.tailCurl + 0.30f);

  const CatPose stampL = catKeyEval(CA_PROTEST, 0.24f);
  const CatPose plantL = catKeyEval(CA_PROTEST, 0.36f);
  const CatPose stampR = catKeyEval(CA_PROTEST, 0.50f);
  const CatPose plantR = catKeyEval(CA_PROTEST, 0.62f);
  TEST_ASSERT_TRUE(stampL.legLift[0] > stampL.legLift[1] + 0.25f);
  TEST_ASSERT_TRUE(stampR.legLift[1] > stampR.legLift[0] + 0.25f);
  TEST_ASSERT_TRUE(stampL.legLift[0] < CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(stampR.legLift[1] < CAT_LEG_NEAR_GATE);
  TEST_ASSERT_TRUE(plantL.mouthOpen > 0.30f && plantR.mouthOpen > 0.30f);
  TEST_ASSERT_TRUE(stampL.tailCurl * stampR.tailCurl < 0.0f); // one short lash, not idle swish

  const CatPose curlMid = catKeyEval(CA_CURL_UP, 0.52f);
  const CatPose curlSettle = catKeyEval(CA_CURL_UP, 1.00f);
  TEST_ASSERT_TRUE(curlMid.yaw > P_IDLE.yaw + 0.30f);
  TEST_ASSERT_TRUE(curlMid.bodyY > P_IDLE.bodyY + 5.0f);
  TEST_ASSERT_TRUE(curlMid.tailCurl > P_IDLE.tailCurl + 0.50f);
  TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &curlSettle, sizeof(CatPose));
}

// The slow blink is the content cat's affection signal, and it must stay a FACE-ONLY beat: any
// body drift would read as the start of a gesture that never arrives. The closed eyes are HELD
// (a keyed span at zero), which is what separates it from the 120 ms reflex blink that already
// passes through the same shape on the accumulator clock.
void test_slow_blink_holds_closed_eyes_on_a_still_body() {
  TEST_ASSERT_TRUE(CAT_POSE[CA_SLOWBLINK].durMs >= 2000);
  const CatPose a = catKeyEval(CA_SLOWBLINK, 0.42f);
  const CatPose b = catKeyEval(CA_SLOWBLINK, 0.58f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.eyeOpen);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, b.eyeOpen);
  const CatKey* K; int n; catKeysGet(CA_SLOWBLINK, K, n);
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyX,  K[i].p.bodyX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyY,  K[i].p.bodyY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyRx, K[i].p.bodyRx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, P_IDLE.bodyRy, K[i].p.bodyRy);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, K[i].p.mouthOpen);
    for (int l = 0; l < 4; l++) TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, K[i].p.legLift[l]);
  }
}

// The sniff is the needy pool's non-vocal ask: the nose leads. It must read from the face and
// the lean alone -- no raised paw (begging/please territory) and no open mouth -- with several
// quick headDy bobs and a glance dropped toward the treat below the cat.
void test_sniff_leans_in_and_bobs_with_a_closed_mouth() {
  const CatKey* K; int n; catKeysGet(CA_SNIFF, K, n);
  int reversals = 0; bool leaned = false, glanced = false;
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(K[i].p.mouthOpen < 0.05f);
    for (int l = 0; l < 4; l++) TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, K[i].p.legLift[l]);
    if (K[i].p.headDx > 1.5f) leaned = true;
    if (K[i].p.pupilDy > 0.5f) glanced = true;
    if (i >= 1 && i + 1 < n) {
      float da = K[i].p.headDy - K[i - 1].p.headDy, db = K[i + 1].p.headDy - K[i].p.headDy;
      if (da * db < 0.0f) reversals++;
    }
  }
  TEST_ASSERT_TRUE(leaned);
  TEST_ASSERT_TRUE(glanced);
  TEST_ASSERT_TRUE(reversals >= 3);
}

// Waking is the sleep bridge run in reverse with its own content: it must START on the exact
// loaf (so CA_SLEEPING hands off without a second silhouette; the once-return test pins the
// P_IDLE end), pass through a closed-eye waking yawn, and pay one visible stretch before
// standing. bodyY carries the rise -- the loaf sits at 84 and idle at 75.
void test_waking_rises_through_a_yawn_and_a_stretch() {
  const CatPose start = catKeyEval(CA_WAKING, 0.00f);
  TEST_ASSERT_EQUAL_MEMORY(&P_SLEEP, &start, sizeof(CatPose));
  const CatKey* K; int n; catKeysGet(CA_WAKING, K, n);
  bool yawned = false, stretched = false;
  for (int i = 0; i < n; i++) {
    if (K[i].p.mouthOpen >= 0.45f && K[i].p.eyeOpen <= 0.001f) yawned = true;
    if (K[i].p.legFwd > 4.0f && K[i].p.bodyRx > P_IDLE.bodyRx + 2.0f) stretched = true;
  }
  TEST_ASSERT_TRUE(yawned);
  TEST_ASSERT_TRUE(stretched);
  TEST_ASSERT_TRUE(catKeyEval(CA_WAKING, 0.25f).bodyY > catKeyEval(CA_WAKING, 0.92f).bodyY);
}

// Two look bands the renderer treats as different drawings, not as a continuum. Held keyframe
// values must land outside the in-between, or the pose reads as a mistake rather than a look.
void test_authored_faces_avoid_the_bar_band_and_the_muzzle_hole() {
  for (int a = 0; a < CA_COUNT; a++) {
    const CatKey* K; int n; catKeysGet((CatAnim)a, K, n);
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE_MESSAGE(K[i].p.eyeOpen <= 0.001f || K[i].p.eyeOpen >= 0.60f,
                               "eyeOpen must be the caret or properly open, never the bar");
      TEST_ASSERT_TRUE_MESSAGE(K[i].p.mouthOpen <= 0.50f, "mouthOpen past the art limit");
    }
  }
}

// The itch is the LEG's beat. (It used to be an alternating head cock, back when no leg could be
// shown; this replaced that, so the reversal assert moved from tilt to legLift.)
void test_itch_pulses_its_scratch() {
  const unsigned n = sizeof K_ITCH / sizeof K_ITCH[0];
  int reversals = 0;
  float lo = 1.0f, hi = 0.0f;
  for (unsigned i = 1; i + 1 < n; i++) {
    float a = K_ITCH[i - 1].p.legLift[2], b = K_ITCH[i].p.legLift[2], c = K_ITCH[i + 1].p.legLift[2];
    if ((b - a) * (c - b) < 0.0f) reversals++;
    if (b < lo) lo = b;
    if (b > hi) hi = b;
  }
  TEST_ASSERT_TRUE(reversals >= 3);              // several beats, not one raise
  TEST_ASSERT_TRUE(hi - lo > 0.15f);             // with enough travel to see
  for (unsigned i = 0; i < n; i++)               // ears counter-rotate, never shrug
    TEST_ASSERT_TRUE(K_ITCH[i].p.earL * K_ITCH[i].p.earR <= 0.0f);
}

// Two things the scratch depends on that are invisible in a still frame.
void test_scratch_leg_holds_the_near_plane_and_clears_the_head() {
  const unsigned n = sizeof K_ITCH / sizeof K_ITCH[0];
  // Crossing the gate mid-pose flips the leg's painter depth, and at gate height the paw is
  // inside the torso -- so the flip is visible. Every raised key must stay clear of it; only the
  // final return to idle may cross, where the whole body is moving anyway. All raised-limb
  // poses owe this -- the lick/please arms change plane against the face, the itch's leg against
  // the body, and either flip is a one-frame pop of a limb appearing from nowhere.
  for (unsigned i = 0; i + 1 < n; i++)
    if (K_ITCH[i].p.legLift[2] > 0.0f)
      TEST_ASSERT_TRUE(K_ITCH[i].p.legLift[2] > CAT_LEG_NEAR_GATE);
  const unsigned ln = sizeof K_LICK / sizeof K_LICK[0];
  for (unsigned i = 0; i + 1 < ln; i++)
    if (K_LICK[i].p.legLift[0] > 0.0f)
      TEST_ASSERT_TRUE(K_LICK[i].p.legLift[0] > CAT_LEG_NEAR_GATE);
  const unsigned pn = sizeof K_PLEASE / sizeof K_PLEASE[0];
  for (unsigned i = 0; i + 1 < pn; i++)
    for (int leg = 0; leg < 2; leg++)
      if (K_PLEASE[i].p.legLift[leg] > 0.0f)
        TEST_ASSERT_TRUE(K_PLEASE[i].p.legLift[leg] > CAT_LEG_NEAR_GATE);

  // Sleeping's tuck must NOT trip the gate: it wants four paws folded under a loaf, on the far
  // plane, with their haunches drawn. It sits at 0.55 deliberately.
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(P_SLEEP.legLift[i] < CAT_LEG_NEAR_GATE);

  // The paw is pushed clear of the head disc rather than stopped at a tuned height. Check it at
  // the shipped head AND at the preset ceiling (headSize 2.32), which is where a constant failed:
  // the head draws last, so a paw inside it is simply erased.
  const float heads[] = { 1.38f, 2.32f };
  for (unsigned h = 0; h < 2; h++) {
    CatPreset p = CAT_PRESET[0];
    p.headSize = heads[h];
    CatPose q = P_IDLE;
    q.legLift[2] = 1.0f;
    int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
    int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
    int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
    int ground = by + bry + (int)(catLegGround + 0.5f);
    int hr = (int)(q.headR * p.headSize + 0.5f);
    int hxi = bx, hyi = by + (int)q.headDy;
    CatLegLayout L = catProjectLegs(q, bx, by, brx, bry, ground, hxi, hyi, hr);

    TEST_ASSERT_TRUE(L.rearNear[0]);                       // drawn over the body, not under it
    int dx = L.rear[0].pawX - hxi, dy = L.rear[0].pawY - hyi;
    TEST_ASSERT_TRUE_MESSAGE(dx * dx + dy * dy >= (hr + L.pawRy) * (hr + L.pawRy),
                             "raised paw must stay outside the head disc that draws over it");
    // The paw only RISES where the head leaves room for it. At the headSize ceiling the skull
    // reaches down past the shoulder and the clear-the-head push puts the paw back near the
    // ground: such a preset simply cannot show a scratch, which is a preset-authoring
    // constraint (Task 6), not a bug here. What must hold at every head size is the clearance
    // above -- a paw inside the disc is erased outright and the pose plays as nothing at all.
    if (heads[h] < 1.6f) TEST_ASSERT_TRUE(L.rear[0].pawY < ground - 20);
  }
}

// The lick's arm is steered AT the muzzle rather than raised straight up -- straight up is the
// one direction that cannot work, since the forelegs sit at only +-0.19*brx and directly
// overhead is the middle of the skull. Two properties, neither visible in a clip flag: the paw
// arrives at the muzzle, and it never climbs above the head's centre, where it would sit over the
// eyes and read as a cat hiding its face instead of grooming.
void test_lick_paw_reaches_the_muzzle_and_stays_off_the_eyes() {
  const CatPreset& p = CAT_PRESET[0];
  CatPose q = P_IDLE;
  q.headDy = -24.0f;
  int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
  int ground = by + bry + (int)(catLegGround + 0.5f);
  int hr = (int)(q.headR * p.headSize + 0.5f);
  int hxi = bx, hyi = by + (int)q.headDy;

  for (float lift = 0.70f; lift <= 1.001f; lift += 0.05f) {
    q.legLift[0] = lift;                                  // e==1 reads legLift[0]
    CatLegLayout L = catProjectLegs(q, bx, by, brx, bry, ground, hxi, hyi, hr);
    TEST_ASSERT_TRUE(L.foreNear[1]);                      // drawn after the face, not under it
    TEST_ASSERT_TRUE_MESSAGE(L.fore[1].pawY >= hyi,
                             "raised paw must stay out of the head's upper half");
  }

  q.legLift[0] = 1.0f;
  CatLegLayout L = catProjectLegs(q, bx, by, brx, bry, ground, hxi, hyi, hr);
  int mx = hxi, my = hyi + (int)((float)hr * 0.45f);
  int dx = L.fore[1].pawX - mx, dy = L.fore[1].pawY - my;
  TEST_ASSERT_TRUE_MESSAGE(dx * dx + dy * dy <= 8 * 8, "wide-open lift must land on the muzzle");
  // ...and the OTHER foreleg is untouched: only the lifted one changes plane or reach.
  TEST_ASSERT_FALSE(L.foreNear[0]);
  TEST_ASSERT_EQUAL_INT(ground, L.fore[0].pawY);
}

// Every pose, every phase, both marking modes and outline on/off: nothing leaves the grid. The
// authored poses reach further than any sweep written before them (stretch is the widest body and
// the most forward legFwd in the table).
void test_every_authored_pose_stays_on_grid() {
  CatPreset p = CAT_PRESET[0];
  memset(&G, 0xA5, sizeof(G));
  for (int outline = 0; outline <= 1; outline++) {
    p.outline = (uint8_t)outline;
    for (int a = 0; a < CA_COUNT; a++)
      for (int i = 0; i <= 20; i++) {
        CatRenderState s; catInit(s);
        s.cur = (CatAnim)a; s.phase = (float)i / 20.0f; s.blend = 1.0f;
        s.aBreathe = 1.5707963f; s.aTail = 1.5707963f;     // fullest body, max swish
        s.headYaw = s.headYawTarget = CAT_POSE[a].yaw;
        memset(&D, 0, sizeof(D));
        catRender(G.g, p, s, &D);
        TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
        TEST_ASSERT_TRUE(D.cellsFilled > 300);
      }
  }
  checkGuards();
}

void test_sleep_tail_turns_back_over_the_near_rump() {
  const CatPreset& p = CAT_PRESET[0];
  const CatPose q = P_SLEEP;
  int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
  CatTailLayout L = catProjectTail(q, p, bx, by, brx, bry);

  TEST_ASSERT_TRUE(L.frontStart < CAT_TAIL_SEGMENTS);        // a returning tip gets near-side depth
  int farX = L.seg[1].x1 < L.seg[2].x1 ? L.seg[1].x1 : L.seg[2].x1;
  TEST_ASSERT_TRUE(L.seg[CAT_TAIL_SEGMENTS - 1].x1 > farX + 10); // visible turn, not a bent straight tail
  TEST_ASSERT_TRUE(L.seg[CAT_TAIL_SEGMENTS - 1].x1 >= L.rootX);  // tip has come all the way around
}

void test_idle_tail_keeps_its_rear_plane_sweep() {
  const CatPreset& p = CAT_PRESET[0];
  const CatPose q = P_IDLE;
  int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
  CatTailLayout L = catProjectTail(q, p, bx, by, brx, bry);

  TEST_ASSERT_EQUAL_INT(CAT_TAIL_SEGMENTS, L.frontStart);     // small idle curl never crosses the torso
  for (int i = 0; i < CAT_TAIL_SEGMENTS; i++)
    TEST_ASSERT_TRUE(L.seg[i].x1 < L.rootX);                  // every joint stays on the rump side
}

void test_tail_authoring_band_stays_in_bounds() {
  const float bases[] = { 0.0f, 1.55f, 3.2f, 4.1f };
  const float curls[] = { -1.5f, -0.8f, 0.0f, 0.8f, 1.5f };
  const float swishes[] = { -1.0f, 0.0f, 1.0f };
  CatPreset p = CAT_PRESET[0];
  p.tailLen = 16.0f; p.tailFluff = 9.0f; p.marking = 1; p.outline = 1;
  memset(&G, 0xA5, sizeof(G));
  for (unsigned bi = 0; bi < sizeof bases / sizeof bases[0]; bi++)
    for (unsigned ci = 0; ci < sizeof curls / sizeof curls[0]; ci++)
      for (unsigned si = 0; si < sizeof swishes / sizeof swishes[0]; si++) {
        CatPose q = P_SLEEP;
        q.tailBase = bases[bi]; q.tailCurl = curls[ci]; q.tailSwish = swishes[si];
        CatRenderState s; catInit(s);
        s.blendFrom = q; s.blend = 0.0f;                     // render this authored pose exactly
        memset(&D, 0, sizeof(D));
        catRender(G.g, p, s, &D);
        TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
      }
  checkGuards();
}

void test_leg_projection_separates_rear_and_front_at_profile() {
  CatPose q = P_IDLE; q.yaw = 1.0f;
  const CatPreset& p = CAT_PRESET[0];
  int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
  int ground = by + bry + 6; if (ground > CAT_GH - 6) ground = CAT_GH - 6;
  // These two exercise the grounded layout, where the head args are unused (no leg is raised
  // past CAT_LEG_NEAR_GATE, so the clear-the-head push never runs).
  CatLegLayout L = catProjectLegs(q, bx, by, brx, bry, ground,
                                 bx, by + (int)q.headDy, (int)(q.headR * p.headSize + 0.5f));

  int rearMax = L.rear[0].pawX > L.rear[1].pawX ? L.rear[0].pawX : L.rear[1].pawX;
  int foreMin = L.fore[0].pawX < L.fore[1].pawX ? L.fore[0].pawX : L.fore[1].pawX;
  TEST_ASSERT_TRUE(rearMax < bx);              // rump/tail side
  TEST_ASSERT_TRUE(foreMin > bx);              // chest/head side
  TEST_ASSERT_TRUE(rearMax + L.pawRx < foreMin - L.pawRx); // visible gap between the groups
  TEST_ASSERT_TRUE(L.rear[0].hockX < L.rear[0].pawX);
  TEST_ASSERT_TRUE(L.rear[1].hockX < L.rear[1].pawX);       // both rear feet point forward
  TEST_ASSERT_EQUAL_INT(L.rear[0].pawY, L.rear[1].pawY);
  TEST_ASSERT_EQUAL_INT(L.fore[0].pawY, L.fore[1].pawY);    // no diagonal far-side pair
  TEST_ASSERT_EQUAL_INT(L.rear[0].pawY, L.fore[0].pawY);    // all paws share the profile ground
  TEST_ASSERT_EQUAL_INT(L.rear[0].pawX, L.rear[1].pawX);    // side axis points into the screen
  TEST_ASSERT_EQUAL_INT(L.fore[0].pawX, L.fore[1].pawX);    // one visible silhouette per plane
}

void test_leg_projection_keeps_rear_outside_fore_when_frontal() {
  CatPose q = P_IDLE; q.yaw = 0.0f;
  const CatPreset& p = CAT_PRESET[0];
  int bx = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);
  int ground = by + bry + 6; if (ground > CAT_GH - 6) ground = CAT_GH - 6;
  // These two exercise the grounded layout, where the head args are unused (no leg is raised
  // past CAT_LEG_NEAR_GATE, so the clear-the-head push never runs).
  CatLegLayout L = catProjectLegs(q, bx, by, brx, bry, ground,
                                 bx, by + (int)q.headDy, (int)(q.headR * p.headSize + 0.5f));

  TEST_ASSERT_TRUE(L.rear[0].pawX < L.fore[0].pawX);
  TEST_ASSERT_TRUE(L.rear[1].pawX > L.fore[1].pawX);
  TEST_ASSERT_TRUE(L.rear[0].pawY == L.rear[1].pawY); // rear depth, not arbitrary side depth
  TEST_ASSERT_TRUE(L.rear[0].pawY < L.fore[0].pawY);
}

// Mirroring is visual variety, not a second lighting setup: every authored palette index,
// including shade bands, markings, contact shadows and catchlights, must travel with the cat.
// Sweep every animation, several phases and both outline paths so a future screen-space feature
// cannot quietly reintroduce a mirrored-only look.
void test_mirror_is_an_exact_pixel_flip_for_every_pose() {
  static uint8_t a[CAT_GH][CAT_GW], b[CAT_GH][CAT_GW];
  CatPreset p = CAT_PRESET[0];
  for (int outline = 0; outline <= 1; outline++) {
    p.outline = (uint8_t)outline;
    for (int anim = 0; anim < CA_COUNT; anim++)
      for (int phase = 0; phase <= 4; phase++) {
        CatRenderState s; catInit(s);
        s.cur = (CatAnim)anim; s.phase = (float)phase / 4.0f; s.blend = 1.0f;
        s.aTail = 0.73f + (float)phase; s.aBreathe = 1.17f + (float)phase;
        s.headYaw = s.headYawTarget = CAT_POSE[anim].yaw;
        s.blinkT = phase == 2 ? CAT_BLINK_MS * 0.5f : 0.0f;
        s.twitchT = phase == 3 ? CAT_TWITCH_MS * 0.5f : 0.0f;
        catRender(a, p, s, nullptr);
        s.mirror = true;
        catRender(b, p, s, nullptr);
        int diff = 0;
        for (int y = 0; y < CAT_GH; y++)
          for (int x = 0; x < CAT_GW; x++)
            diff += a[y][x] != b[y][CAT_GW - 1 - x];
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, diff, "mirror must preserve every palette index");
      }
  }
}

void test_render_is_deterministic_and_diag_free() {
  CatRenderState s; catInit(s);
  s.phase = 0.37f; s.aTail = 1.1f; s.aBreathe = 2.2f; s.headYaw = 0.15f;
  static uint8_t a[CAT_GH][CAT_GW], b[CAT_GH][CAT_GW];
  catRender(a, CAT_PRESET[0], s, nullptr);
  memset(&D, 0, sizeof(D));
  catRender(b, CAT_PRESET[0], s, &D);         // diag on/off must not change the raster
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
  catRender(b, CAT_PRESET[0], s, nullptr);    // and repeat renders are byte-identical
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
}

void test_once_pose_terminates() {
  CatRenderState s; catInit(s);
  catSetPose(s, CAT_PRESET[0], CA_LICKING);
  int steps = (int)(CAT_POSE[CA_LICKING].durMs / 33) + 2;
  for (int i = 0; i < steps && !catPoseDone(s); i++) catAdvance(s, CAT_PRESET[0], 33.0f);
  TEST_ASSERT_TRUE(catPoseDone(s));           // guards the reaction-expiry path
}

void test_loop_pose_never_done() {
  CatRenderState s; catInit(s);                // CA_IDLE is CP_LOOP
  for (int i = 0; i < 300; i++) catAdvance(s, CAT_PRESET[0], 33.0f);   // ~10 s
  TEST_ASSERT_FALSE(catPoseDone(s));
  TEST_ASSERT_TRUE(s.phase >= 0 && s.phase < 1.0f);   // wrapped, not clamped
}

void test_accumulator_wrap_is_continuous() {
  CatRenderState s; catInit(s);
  s.aTail = 6.2731f;                           // just under 2π
  CatPose a = catEval(CAT_PRESET[0], s);
  catAdvance(s, CAT_PRESET[0], 16.0f);         // crosses the fmodf boundary
  TEST_ASSERT_TRUE(s.aTail < 1.0f);            // proves the wrap happened
  CatPose b = catEval(CAT_PRESET[0], s);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, a.tailSwish, b.tailSwish);   // no pop across the wrap
}

void test_interrupted_blend_does_not_pop() {
  CatRenderState s; catInit(s);
  catSetPose(s, CAT_PRESET[0], CA_LICKING);
  for (int i = 0; i < 3; i++) catAdvance(s, CAT_PRESET[0], 33.0f);   // mid-blend (~0.55)
  CatPose before = catEval(CAT_PRESET[0], s);
  catSetPose(s, CAT_PRESET[0], CA_IDLE);       // interrupt the blend
  CatPose after = catEval(CAT_PRESET[0], s);   // blend=0 -> exactly the snapshot
  const float* B = (const float*)&before; const float* A = (const float*)&after;
  for (unsigned i = 0; i < sizeof(CatPose) / sizeof(float); i++)
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, B[i], A[i]);
}

void test_variants_diverge_by_rate() {
  CatPreset a = CAT_PRESET[0], b = CAT_PRESET[0];
  a.tailRate = 1.0f; b.tailRate = 3.0f;
  CatRenderState sa, sb; catInit(sa); catInit(sb);
  for (int i = 0; i < 30; i++) { catAdvance(sa, a, 33.0f); catAdvance(sb, b, 33.0f); }
  TEST_ASSERT_TRUE(fabsf(sa.aTail - sb.aTail) > 0.5f);   // complaint 3 must not return
}

void test_head_yaw_slews_toward_target() {
  CatRenderState s; catInit(s);
  s.headYaw = 0.9f; s.headYawTarget = 0.0f;
  catAdvance(s, CAT_PRESET[0], 33.0f);
  TEST_ASSERT_TRUE(s.headYaw < 0.9f && s.headYaw > 0.7f);   // capped step, not a snap
  for (int i = 0; i < 30; i++) catAdvance(s, CAT_PRESET[0], 33.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, s.headYaw);          // converged
}

void test_ramp_helpers_scale_each_channel_at_its_own_depth() {
  const uint16_t white = catRGB(255, 255, 255);
  TEST_ASSERT_EQUAL_UINT16(white, catDim(white, 1.0f));           // identity at k=1
  const uint16_t h = catDim(white, 0.5f);
  TEST_ASSERT_EQUAL_UINT16(15, (h >> 11) & 0x1F);                 // 5-bit red   halved
  TEST_ASSERT_EQUAL_UINT16(31, (h >>  5) & 0x3F);                 // 6-bit green halved
  TEST_ASSERT_EQUAL_UINT16(15,  h        & 0x1F);                 // 5-bit blue  halved
  TEST_ASSERT_EQUAL_UINT16(0, catDim(white, 0.0f));
  // neither helper may bleed a channel into its neighbour's bits
  TEST_ASSERT_EQUAL_UINT16(0, catDim(catRGB(255, 0, 0), 0.62f) & 0x07FF);
  const uint16_t black = catRGB(0, 0, 0);
  TEST_ASSERT_EQUAL_UINT16(black, catLit(black, 0.0f));           // identity at k=0
  TEST_ASSERT_EQUAL_UINT16(white, catLit(black, 1.0f));           // full headroom -> white
  TEST_ASSERT_TRUE(catLit(catRGB(200, 40, 40), 0.34f) <= 0xFFFF); // no overflow past 16 bits
}

// Every ramp must be monotonically darker, band 0 to band 3. A preset that authored these by
// hand could invert two steps and the lighting would read as noise instead of form.
void test_preset_ramps_are_monotonic_and_filled() {
  for (unsigned i = 0; i < CAT_PRESET_N; i++) {
    const uint16_t* pal = CAT_PRESET[i].pal;
    const uint8_t* ramps[2] = { CAT_FUR, CAT_ACC };
    for (int rr = 0; rr < 2; rr++) {
      const uint8_t* ramp = ramps[rr];
      for (int b = 0; b + 1 < CAT_BANDS; b++) {
        int lum0 = ((pal[ramp[b]]     >> 11) & 0x1F) + ((pal[ramp[b]]     >> 5) & 0x3F);
        int lum1 = ((pal[ramp[b + 1]] >> 11) & 0x1F) + ((pal[ramp[b + 1]] >> 5) & 0x3F);
        TEST_ASSERT_TRUE(lum0 > lum1);
      }
    }
    // blitCatGrid is a bare pal[idx] lookup that skips only CI_TRANS, so any slot the renderer
    // can emit must be filled — an unset one blits black rather than degrading gracefully.
    // CI_EYE and CI_MOUTH are exempt: both are authored flat black for tiny-panel contrast, so 0
    // is intentional there and this check cannot distinguish it from "unset". blitCatGrid skips
    // on the CI_TRANS INDEX, not on the RGB565 value, so both black features remain drawable.
    for (int slot = CI_FUR_0; slot <= CI_BLUSH; slot++)
      if (slot != CI_EYE && slot != CI_MOUTH) TEST_ASSERT_NOT_EQUAL_UINT16(0, pal[slot]);
  }
}

// The whole point of the rewrite: a filled ellipse must come out as a gradient, not one tone.
// The lit end has to land toward the lamp (upper-left) and the dark end away from it.
void test_shaded_ellipse_is_a_gradient_facing_the_light() {
  CatRaster r = fresh();
  shEllipse(r, 56, 56, 20, 20);
  bool seen[CAT_BANDS] = { false, false, false, false };
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
    int b = catRampBand(CAT_FUR, G.g[y][x]);
    if (b >= 0) seen[b] = true;
  }
  for (int b = 0; b < CAT_BANDS; b++) TEST_ASSERT_TRUE(seen[b]);   // all four bands present
  // upper-left of centre is lit, lower-right is in shadow
  int bLit = catRampBand(CAT_FUR, G.g[46][46]), bDark = catRampBand(CAT_FUR, G.g[66][66]);
  TEST_ASSERT_TRUE(bLit >= 0 && bDark >= 0);
  TEST_ASSERT_TRUE(bLit < bDark);
  checkGuards();
}

// far shifts a whole part one band darker, and must never run off the end of the ramp.
void test_far_shifts_one_band_and_clamps() {
  CatRaster a = fresh(); shEllipse(a, 56, 56, 20, 20);
  uint8_t nearCentre = G.g[56][56];
  CatRaster b = fresh(); b.far = true; shEllipse(b, 56, 56, 20, 20);
  uint8_t farCentre = G.g[56][56];
  TEST_ASSERT_EQUAL_INT(catRampBand(CAT_FUR, nearCentre) + 1, catRampBand(CAT_FUR, farCentre));
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++)
    if (G.g[y][x]) TEST_ASSERT_TRUE(catRampBand(CAT_FUR, G.g[y][x]) >= 0);   // still on the ramp
  checkGuards();
}

// Junction definition. Per-cell lighting has no seam of its own, so an occluder lays a halo on
// the surface UNDER it — without one the head fuses into the body. The halo must darken only
// what is already there, never paint outside the part, and never raise the clip flag.
void test_contact_halo_darkens_only_what_is_under_it() {
  CatRaster plain = fresh();
  plain.part = CATP_BODY; shEllipse(plain, 40, 56, 18, 18);
  static uint8_t noHalo[CAT_GH][CAT_GW];
  memcpy(noHalo, G.g, sizeof(noHalo));
  int filledBefore = D.cellsFilled;

  CatRaster r = fresh();
  r.part = CATP_BODY; shEllipse(r, 40, 56, 18, 18);
  r.part = CATP_HEAD; r.contact = true; shEllipse(r, 70, 56, 18, 18); r.contact = false;

  bool darkened = false;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
    if (x >= 52) continue;                            // left of the head: body territory only
    int a = catRampBand(CAT_FUR, noHalo[y][x]), b = catRampBand(CAT_FUR, G.g[y][x]);
    if (a >= 0 && b > a) darkened = true;             // the halo stepped a body cell down
    if (a < 0) TEST_ASSERT_TRUE(b < 0);               // and never painted a cell that was empty
  }
  TEST_ASSERT_TRUE(darkened);
  TEST_ASSERT_TRUE(filledBefore > 0);
  checkGuards();
}

// A halo is drawn CAT_CONTACT cells larger than its part, so it reaches off-grid where the part
// itself does not. It must stay silent about that or the rig reports a false clip.
void test_contact_halo_does_not_raise_the_clip_flag() {
  CatRaster r = fresh();
  r.part = CATP_HEAD; r.contact = true;
  shEllipse(r, CAT_GW / 2, CAT_CONTACT + 1, 2, 2);    // halo overhangs the top edge, shape does not
  r.contact = false;
  TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
  checkGuards();
}

void test_fit_len_keeps_the_reach_in_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, catFitLen(0.0f,  1.0f, -5.0f, 10.0f));   // grows to hi
  TEST_ASSERT_FLOAT_WITHIN(0.01f,  5.0f, catFitLen(0.0f, -1.0f, -5.0f, 10.0f));   // grows to lo
  TEST_ASSERT_FLOAT_WITHIN(0.01f,  0.0f, catFitLen(20.0f, 1.0f, -5.0f, 10.0f));   // already out
  TEST_ASSERT_TRUE(catFitLen(0.0f, 0.0f, -5.0f, 10.0f) > 1e8f);                   // k~0, in range
  TEST_ASSERT_FLOAT_WITHIN(0.01f,  0.0f, catFitLen(99.0f, 0.0f, -5.0f, 10.0f));   // k~0, out
}

// The whisker fan overhangs the silhouette on purpose and used to run off the grid at high yaw.
// Every unwritten pose declares yaw 0.90, so this corner had to be closed before authoring them.
void test_extreme_yaw_and_size_stays_on_grid() {
  const float heads[] = { 0.7f, 1.27f, 1.4f };
  const float yaws[]  = { 0.0f, 0.9f, 1.0f };
  for (int h = 0; h < 3; h++) for (int y = 0; y < 3; y++) {
    CatPreset p = CAT_PRESET[0];
    p.headSize = heads[h]; p.bodyChub = 1.5f; p.earLen = 14.0f; p.eyeShape = 1.4f;
    CatRenderState s; catInit(s);
    s.headYaw = s.headYawTarget = yaws[y];
    memset(&D, 0, sizeof(D));
    catRender(G.g, p, s, &D);
    TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
  }
  // Outline ring coverage: same extremes with outline=1 must also stay on-grid.
  for (int h = 0; h < 3; h++) for (int y = 0; y < 3; y++) {
    CatPreset p = CAT_PRESET[0];
    p.headSize = heads[h]; p.bodyChub = 1.5f; p.earLen = 14.0f; p.eyeShape = 1.4f; p.outline = 1;
    CatRenderState s; catInit(s);
    s.headYaw = s.headYawTarget = yaws[y];
    memset(&D, 0, sizeof(D));
    catRender(G.g, p, s, &D);
    TEST_ASSERT_EQUAL_UINT8(0, D.clipped);
  }
}

void test_flat_collapses_the_torso_bands() {
  // flat swaps the body/leg/tail ramp for CAT_FUR_STICKER, which collapses to {lit, base, base, base}.
  // Surviving dark cells are contact-halo steps ONLY: catStepDarker walks the full CAT_FUR ramp
  // regardless of flat, while the far plane's band+1 lands on base for every band >= 1, so it can
  // never emit a dark cell on the sticker ramp. Deliberate per spec §Sticker pass.
  CatPreset p = CAT_PRESET[0]; p.marking = 0; p.outline = 0;
  CatRenderState s; catInit(s);
  static uint8_t a[CAT_GH][CAT_GW], b[CAT_GH][CAT_GW];
  p.flat = 0; catRender(a, p, s, nullptr);
  p.flat = 1; catRender(b, p, s, nullptr);
  int da = 0, db = 0;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
    da += (a[y][x] == CI_FUR_2) + (a[y][x] == CI_FUR_3);
    db += (b[y][x] == CI_FUR_2) + (b[y][x] == CI_FUR_3);
  }
  TEST_ASSERT_GREATER_THAN_INT(0, db);
  TEST_ASSERT_LESS_THAN_INT(da / 2, db);
}

void test_outline_rims_the_silhouette() {
  // Every silhouette-edge cell is CI_OUTLINE after the under-draw + fill passes — except
  // whiskers, which draw AFTER the outline (deliberate: light whiskers are the dark-background
  // inversion of the refs) and are CI_FUR_0 lines. marking=0 is not protecting anything here:
  // CI_OUTLINE sits outside CAT_FUR/CAT_ACC, so the tabby remap's band lookup always misses it
  // regardless of marking — kept 0 only to match the other outline fixtures in this file.
  CatPreset p = CAT_PRESET[0]; p.marking = 0; p.flat = 0; p.outline = 1;
  CatRenderState s; catInit(s);
  static uint8_t g[CAT_GH][CAT_GW];
  catRender(g, p, s, nullptr);
  int rim = 0;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
    if (g[y][x] == CI_TRANS || g[y][x] == CI_FUR_0) continue;
    bool edge = y == 0 || x == 0 || y == CAT_GH - 1 || x == CAT_GW - 1 ||
                g[y - 1][x] == CI_TRANS || g[y + 1][x] == CI_TRANS ||
                g[y][x - 1] == CI_TRANS || g[y][x + 1] == CI_TRANS;
    if (!edge) continue;
    rim++;
    TEST_ASSERT_EQUAL_UINT8(CI_OUTLINE, g[y][x]);
  }
  TEST_ASSERT_GREATER_THAN_INT(150, rim);   // the rim of a ~50-cell-wide cat is hundreds of cells
}

void test_outline_underdraw_sits_outside_the_fill() {
  // The old post-pass recolored cells INSIDE the silhouette: toggling outline never moved the
  // bbox. The under-draw wraps the fills in a grown ring, so the bbox must grow. Bottom edge
  // only: the whisker fan owns the horizontal extremes and the ear lift interacts with the top.
  CatPreset p = CAT_PRESET[0]; p.marking = 0; p.flat = 1;
  CatRenderState s; catInit(s);
  static uint8_t g[CAT_GH][CAT_GW];
  static CatDiagnostics d0, d1;
  p.outline = 0; catRender(g, p, s, &d0);
  p.outline = 1; catRender(g, p, s, &d1);
  TEST_ASSERT_TRUE(d1.bboxY1 > d0.bboxY1);   // ring extends below the lowest paw fill
  // Toggling outline must change EXTENT and nothing else in the attribution: the ring is
  // bookkeeping underlay, not part content, so it must not move cellsFilled/occlusion/overlap.
  TEST_ASSERT_EQUAL_UINT16(d0.cellsFilled, d1.cellsFilled);        // ring is underlay, not fill
  TEST_ASSERT_EQUAL_UINT16(d0.headBodyOverlap, d1.headBodyOverlap);
  for (int i = 0; i < CAT_PART_COUNT; i++)
    TEST_ASSERT_EQUAL_UINT16(d0.occluded[i], d1.occluded[i]);
}

void test_outline_underdraw_has_no_interior_bamboo() {
  // BOUNDED REGRESSION GUARD, not a red/green test (see task-9-report.md): a bamboo band is
  // outline CROSSING the fill — an outline cell with fill on both opposite sides (above+below,
  // or left+right). Both the old post-pass and the new under-draw measure 4 crossings on
  // CAT_PRESET[0] (concave part-junctions: tail/leg, tail/body — these are expected survivors
  // per the artist's rule, not bamboo). This guard exists so a future change can't make outline
  // cross the fill wholesale again; it does not distinguish post-pass from under-draw by itself
  // — test_outline_underdraw_sits_outside_the_fill (bbox growth) is the actual red/green carrier.
  CatPreset p = CAT_PRESET[0]; p.marking = 0; p.flat = 1; p.outline = 1;
  CatRenderState s; catInit(s);
  static uint8_t g[CAT_GH][CAT_GW];
  catRender(g, p, s, nullptr);
  int crossing = 0;
  for (int y = 1; y < CAT_GH - 1; y++) for (int x = 1; x < CAT_GW - 1; x++) {
    if (g[y][x] != CI_OUTLINE) continue;
    // fill = any drawn cell that is neither background nor outline nor a whisker line
    #define CAT_TEST_FILL(yy, xx) (g[yy][xx] != CI_TRANS && g[yy][xx] != CI_OUTLINE && g[yy][xx] != CI_FUR_0)
    bool vert = CAT_TEST_FILL(y - 1, x) && CAT_TEST_FILL(y + 1, x);
    bool horz = CAT_TEST_FILL(y, x - 1) && CAT_TEST_FILL(y, x + 1);
    #undef CAT_TEST_FILL
    crossing += vert || horz;
  }
  TEST_ASSERT_LESS_THAN_INT(10, crossing);   // post-pass 4, under-draw 4 measured; margin to 10
}

void test_fur_only_skips_the_outline_band() {
  CatRaster r = fresh(); r.furOnly = true;
  G.g[10][10] = CI_OUTLINE; G.g[10][11] = CI_FUR_2;
  catCapsule(r, 10, 10, 11, 10, 0, CI_BLUSH);
  TEST_ASSERT_EQUAL_UINT8(CI_OUTLINE, G.g[10][10]);   // the rim survives a blush pass
  TEST_ASSERT_EQUAL_UINT8(CI_BLUSH, G.g[10][11]);   // ordinary fur still takes it
}

// clipTo and furOnly are live on the SAME pixel exactly once in the renderer: the eye catchlight
// (clipTo = CI_EYE) is drawn from inside the eye loop's furOnly wrap. clipTo must win outright, or
// CI_EYE -- neither fur nor accent -- trips furOnly's rejection and the catchlight silently
// disappears (fix round 1 caught this with a green suite and no test pinning it down). Lock the
// precedence directly rather than relying on it being re-discovered by rendering the whole cat.
void test_clipto_wins_over_furonly_on_the_same_pixel() {
  CatRaster r = fresh();
  G.g[10][10] = CI_EYE;                              // stand-in for the eye body already painted
  r.furOnly = true;                                   // both flags live at once, as in the eye loop
  r.clipTo = CI_EYE;
  catCapsule(r, 10, 10, 10, 10, 0, CI_HILITE);
  r.clipTo = 0; r.furOnly = false;
  TEST_ASSERT_EQUAL_UINT8(CI_HILITE, G.g[10][10]);    // clipTo wins: furOnly never gets a say
  checkGuards();
}

void test_ear_tip_radius_narrows_with_earPoint() {
  int base = (catEarRad(28) * 5) / 8; if (base < 2) base = 2;
  TEST_ASSERT_EQUAL_INT(base, catEarTipRad(28, 0.0f));      // 0 = legacy tip radius
  int prev = catEarTipRad(28, 0.0f);
  for (int i = 1; i <= 4; i++) {                            // monotonic non-increasing
    int r = catEarTipRad(28, 0.25f * (float)i);
    TEST_ASSERT_TRUE(r <= prev);
    prev = r;
  }
  TEST_ASSERT_EQUAL_INT(1, catEarTipRad(28, 1.0f));         // fully pointy = 1-cell tip
}

void test_whisker_twitch_schedules_fires_and_decays() {
  CatPreset p = CAT_PRESET[0];
  CatRenderState s; catInit(s);
  float tMax = 0; int fired = 0;
  for (int i = 0; i < 2000; i++) {                 // 32 s at 16 ms steps
    catAdvance(s, p, 16.0f);
    if (s.twitchT > 0) { fired = 1; if (s.twitchT > tMax) tMax = s.twitchT; }
  }
  TEST_ASSERT_EQUAL_INT(1, fired);                 // rarer than blink, but well within 32 s
  TEST_ASSERT_TRUE(tMax <= CAT_TWITCH_MS);         // counts down from the constant
  TEST_ASSERT_TRUE(s.twitchNextMs > 0.0f);         // scheduler always re-arms
}

void test_tabby_tail_carries_accent_rings() {
  // Rings live on the tail, which sweeps left of the body ellipse — forehead ticks (the only
  // other marking-1 accent) can never reach x < bx - brx, so accents there prove the rings.
  CatRenderState s; catInit(s);
  static uint8_t g[CAT_GH][CAT_GW];
  CatPreset p = CAT_PRESET[0]; p.marking = 1; p.flat = 1; p.outline = 0;
  catRender(g, p, s, nullptr);
  CatPose q = catEval(p, s);
  int bx = (int)(q.bodyX + 0.5f), brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  int acc = 0;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < bx - brx; x++)
    acc += catRampBand(CAT_ACC, g[y][x]) >= 0;
  TEST_ASSERT_GREATER_THAN_INT(10, acc);
}

// The feat table is BAKED (two trig calls a frame is the point of the model). This recomputes it
// from the (k, yf) pairs in the comments so a hand-edited constant cannot pass unnoticed.
// CF_EARB is special-cased to the NEGATIVE cosine root: asin(k/ce) only ever returns the acute
// angle, but the ears sit behind the coronal plane (top-back of the skull), not in front of it like
// every other feat here, so their azimuth is genuinely the obtuse supplement -- same sine (hence
// same front-on x), opposite cosine (hence opposite travel off-axis). That is an anatomical choice,
// not a derivation error, which is why this one entry gets its own sign instead of sharing the
// principal sqrtf every other feat uses (fix round 3, owner catch: ears moved the wrong way).
// The disc clip is new machinery and the muzzle blaze's only bound: head and body are the same
// four fur bands, so nothing about WHAT is under the brush can stop the blaze at the chin. Test
// the gate itself rather than the marking -- a blaze that leaked would weld the head's coat to the
// chest bib, which is exactly what this marking exists to keep apart.
void test_disc_clip_confines_a_fill_to_its_circle() {
  const int cx = 50, cy = 50, rad = 12;
  for (int mirror = 0; mirror <= 1; mirror++) {
    CatRaster r = fresh();
    r.mirror = (mirror != 0);
    r.clipCx = cx; r.clipCy = cy; r.clipR = rad;
    catEllipse(r, cx + 6, cy + 6, 40, 40, CI_FUR_0);   // far larger than the disc, and off-centre
    r.clipR = 0;
    int inside = 0, outside = 0;
    for (int y = 0; y < CAT_GH; y++)
      for (int x = 0; x < CAT_GW; x++) {
        if (!G.g[y][x]) continue;
        // catPlot clips BEFORE the mirror flip, so a mirrored run lands about the mirrored centre.
        int px = r.mirror ? CAT_GW - 1 - x : x;
        int dx = px - cx, dy = y - cy;
        if (dx * dx + dy * dy > rad * rad) outside++; else inside++;
      }
    TEST_ASSERT_EQUAL_INT(0, outside);
    TEST_ASSERT_TRUE(inside > 0);                      // and it did not clip everything away
    checkGuards();
  }
  // clipR 0 is OFF, not "radius zero" -- an unclipped fill must still paint.
  CatRaster r2 = fresh();
  catEllipse(r2, cx, cy, 8, 8, CI_FUR_0);
  int painted = 0;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) if (G.g[y][x]) painted++;
  TEST_ASSERT_TRUE(painted > 0);
  checkGuards();
}

void test_feat_table_matches_its_derivation() {
  struct { const CatFeat* f; float k, yf; bool negCa; } T[] = {
    { &CF_EYE, 0.62f, 0.10f, false }, { &CF_BLUSH, 0.72f, 0.48f, false },
    { &CF_WHISK, 0.58f, 0.46f, false }, { &CF_NOSE, 0.00f, 0.34f, false },
    { &CF_TICK, 0.35f, -0.72f, false }, { &CF_TICK0, 0.00f, -0.72f, false },
    { &CF_EARB, 0.50f, -0.34f, true },
    { &CF_BLAZE_TIP, 0.00f, -0.08f, false }, { &CF_BLAZE, 0.50f, 0.72f, false },
  };
  for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
    float ce = sqrtf(1.0f - T[i].yf * T[i].yf);
    float sa = T[i].k / ce, ca = sqrtf(1.0f - sa * sa);
    if (T[i].negCa) ca = -ca;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, ce, T[i].f->ce);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sa, T[i].f->sa);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, ca, T[i].f->ca);
  }
}

// phi = 0 must reproduce the exact expression this pass replaces: side * k * hr.
void test_facing_zero_is_the_old_expression() {
  CatTurn t = catTurn(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,  0.62f * 28.0f, catAz(28.0f, t, CF_EYE,   1.0f).x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -0.62f * 28.0f, catAz(28.0f, t, CF_EYE,  -1.0f).x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,  0.72f * 28.0f, catAz(28.0f, t, CF_BLUSH, 1.0f).x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,  0.58f * 28.0f, catAz(28.0f, t, CF_WHISK, 1.0f).x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,  0.50f * 28.0f, catAz(28.0f, t, CF_EARB,  1.0f).x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,  0.0f,          catAz(28.0f, t, CF_NOSE,  1.0f).x);
  // fore is normalized, so the front view is full-width, not cos(a)-shrunk (0.78 would be the bug)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, catAz(28.0f, t, CF_EYE, 1.0f).fore);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, catAz(28.0f, t, CF_WHISK, 1.0f).fore);
}

void test_forehead_ticks_follow_spherical_meridian_arcs() {
  static const float CE[3] = { 0.474974f, 0.693974f, 0.828493f };
  static const float YF[3] = { -0.88f, -0.72f, -0.56f };
  const float hr = 28.0f;

  CatTickArc side = catTickArc(hr, catTurn(0.0f), CF_TICK, 1.0f);
  TEST_ASSERT_TRUE(side.vis);
  TEST_ASSERT_TRUE(side.x[0] < side.x[1]);             // crown bends inward
  TEST_ASSERT_TRUE(side.x[1] < side.x[2]);             // brow fans outward
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, side.x[1] / CE[1], side.x[i] / CE[i]);
    float y = hr * YF[i];
    TEST_ASSERT_TRUE(side.x[i] * side.x[i] + y * y <= hr * hr + 0.01f);
  }

  CatTickArc centre0 = catTickArc(hr, catTurn(0.0f), CF_TICK0, 1.0f);
  for (int i = 0; i < 3; i++) TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, centre0.x[i]);

  CatTickArc centreTurn = catTickArc(hr, catTurn(CAT_HEAD_YAW_ART_MAX), CF_TICK0, 1.0f);
  TEST_ASSERT_TRUE(centreTurn.vis);
  TEST_ASSERT_TRUE(centreTurn.x[0] < centreTurn.x[1]);
  TEST_ASSERT_TRUE(centreTurn.x[1] < centreTurn.x[2]);
}

// Every eye facing test above exercises the BAKED CF_EYE constant (k=0.62), never the live feat
// `catRender` actually derives from `catEyeK` (k=0.592) at cat_proc.h:1167-1173 -- so the value
// that ships had no direct coverage (final review, 2026-07-29). Reproduce that derivation here and
// pin the one property it exists for: front-on separation is ~2*catEyeK*hr, not 2*0.62*hr.
void test_eye_separation_uses_the_live_catEyeK_derivation() {
  float esa = catEyeK / CF_EYE.ce;
  CatFeat fe = { CF_EYE.ce, esa, sqrtf(1.0f - esa * esa) };
  CatTurn t = catTurn(0.0f);
  CatAz eL = catAz(28.0f, t, fe, -1.0f), eR = catAz(28.0f, t, fe, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.0f * catEyeK * 28.0f, eR.x - eL.x);
}

// The burial invariant: a sphere feature can never leave the head circle, at any yaw.
// This is what replaces the old (unsatisfiable) |corner| + rad <= 0.95*hr clamp.
void test_facing_features_stay_inside_the_skull() {
  const CatFeat* F[] = { &CF_EYE, &CF_BLUSH, &CF_WHISK, &CF_NOSE, &CF_TICK, &CF_TICK0, &CF_EARB };
  const float YF[]   = { 0.10f, 0.48f, 0.46f, 0.34f, -0.72f, -0.72f, -0.34f };
  for (int hr = 12; hr <= 40; hr += 4)
    for (int i = 0; i <= 100; i++) {
      CatTurn t = catTurn((float)i * 0.01f);
      for (unsigned k = 0; k < sizeof(F) / sizeof(F[0]); k++)
        for (float side = -1.0f; side <= 1.0f; side += 2.0f) {
          float x = catAz((float)hr, t, *F[k], side).x, y = YF[k] * (float)hr;
          TEST_ASSERT_TRUE(x * x + y * y <= (float)hr * (float)hr + 0.01f);
        }
    }
}

// Monotone travel WHILE VISIBLE, monotone visibility, and the vanish happens at the feature's OWN
// rim (hr*cos e) -- not the head's. A flat 0.98*hr bound is wrong off the equator: CF_TICK's
// cos e is 0.69, so it legitimately goes at 0.69*hr.
void test_facing_vanishes_only_at_its_own_rim() {
  const CatFeat* F[] = { &CF_EYE, &CF_BLUSH, &CF_WHISK, &CF_TICK };
  for (unsigned k = 0; k < sizeof(F) / sizeof(F[0]); k++) {
    float prevX = -1e9f, lastVisX = 0.0f;
    bool wasVis = true, everHidden = false;
    for (int i = 0; i <= 100; i++) {
      CatAz a = catAz(28.0f, catTurn((float)i * 0.01f), *F[k], 1.0f);
      if (a.vis) {
        TEST_ASSERT_FALSE(everHidden);            // visibility never comes back
        TEST_ASSERT_TRUE(a.x >= prevX - 0.01f);   // travels toward the facing side
        prevX = a.x; lastVisX = a.x; wasVis = true;
      } else { if (wasVis) everHidden = true; wasVis = false; }
    }
    TEST_ASSERT_TRUE(everHidden);                                  // every one of these does vanish
    TEST_ASSERT_TRUE(lastVisX >= 0.98f * 28.0f * F[k]->ce);        // at its own rim
  }
}

// Assert the layout at the yaws the DEVICE renders, iterating CAT_POSE[] rather than literals --
// this pass retunes those values, so a hardcoded 0.12/0.30/0.90 would contradict its own spec.
void test_face_layout_at_shipped_yaws() {
  for (int a = 0; a < CA_COUNT; a++) {
    CatTurn t = catTurn(CAT_POSE[a].yaw);
    CatAz eL = catAz(28.0f, t, CF_EYE, -1.0f), eR = catAz(28.0f, t, CF_EYE, 1.0f);
    CatAz nz = catAz(28.0f, t, CF_NOSE, 1.0f);
    TEST_ASSERT_TRUE(eL.x * eL.x <= 28.0f * 28.0f);      // both inside the skull, always
    TEST_ASSERT_TRUE(eR.x * eR.x <= 28.0f * 28.0f);
    if (eL.vis && eR.vis) {                              // muzzle BETWEEN the pair...
      TEST_ASSERT_TRUE(nz.x > eL.x && nz.x < eR.x);
    } else if (eL.vis) {                                 // ...but AHEAD of a lone stress-yaw survivor.
      TEST_ASSERT_TRUE(nz.x > eL.x);
    }
  }
}

void test_pose_head_yaws_stay_in_the_chibi_art_band() {
  for (int a = 0; a < CA_COUNT; a++)
    TEST_ASSERT_TRUE(CAT_POSE[a].yaw <= CAT_HEAD_YAW_ART_MAX);
}

void test_ear_turn_is_attenuated_and_normalized() {
  CatTurn face = catTurn(1.0f);
  // Endpoints first: gain 0 IS the identity turn (what the device ships and folds away), gain 1
  // reproduces the face turn exactly, so the seam can never quietly stop being a pure blend.
  CatTurn id = catEarTurn(face, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, id.sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, id.cp);
  CatTurn full = catEarTurn(face, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, face.sp, full.sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, face.cp, full.cp);
  // ...then the properties the sphere-burial proof downstream depends on, at every gain the rig
  // can scrub to: unit length, and never turned FURTHER than the face it is attenuating.
  float prev = -1.0f;
  for (int i = 0; i <= 20; i++) {
    float g = (float)i / 20.0f;
    CatTurn e = catEarTurn(face, g);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, e.sp * e.sp + e.cp * e.cp);
    TEST_ASSERT_TRUE(e.sp <= face.sp + 1e-5f);
    TEST_ASSERT_TRUE(e.sp >= prev - 1e-5f);              // monotone in the gain
    prev = e.sp;
  }
}

// The leading inner ear does not foreshorten as the head turns -- it SNAPS OFF, because the
// inner-ear pass bails on `irad > avail` while the outer ear is still ~65% of its front-on area.
// The visible failure is a one-pink-one-bare face, and it is preset-dependent: CAT_PRESET[0]
// survived a gain that broke 22 of the 194 viable presets in this sweep, which is exactly how
// catEarYawGain 0.20 shipped. Pin the PROPERTY, not the number: whatever gain a future artist
// picks, a cat that has a leading inner ear looking straight at you must still have one at the
// art ceiling. Sweeping earLen x earPoint x headSize because that is the space the six planned
// presets live in -- one preset is not the test.
void test_leading_inner_ear_survives_the_art_ceiling() {
  static uint8_t g[CAT_GH][CAT_GW];
  const float EL[] = { 0.0f, 0.5f, 1.0f, 2.0f, 2.5f, 4.0f, 6.0f, 8.0f };
  const float EP[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
  const float HS[] = { 0.7f, 0.9f, 1.0f, 1.2f, 1.38f, 1.4f };
  int viable = 0;
  for (unsigned i = 0; i < sizeof EL / sizeof *EL; i++)
    for (unsigned j = 0; j < sizeof EP / sizeof *EP; j++)
      for (unsigned k = 0; k < sizeof HS / sizeof *HS; k++) {
        CatPreset p = CAT_PRESET[0];
        p.earLen = EL[i]; p.earPoint = EP[j]; p.headSize = HS[k];
        int pink[2] = { 0, 0 };
        for (int t = 0; t < 2; t++) {                    // t=0 front-on, t=1 at the art ceiling
          CatRenderState s; catInit(s);
          s.headYaw = s.headYawTarget = t ? CAT_HEAD_YAW_ART_MAX : 0.0f;
          CatPose q = catEval(p, s);
          catRender(g, p, s, nullptr);
          // catRender's own head placement; +x of it is the side the ear turn carries backward.
          int cx = (int)(q.bodyX + q.headDx + q.yaw * 12.0f + 0.5f);
          for (int y = 0; y < CAT_GH; y++)
            for (int x = cx + 1; x < CAT_GW; x++) if (g[y][x] == CI_EAR) pink[t]++;
        }
        // Presets with no room for pink at ANY yaw are the documented earPoint/room guard, not a
        // facing regression -- skip them rather than pretend the turn is what removed the ear.
        if (pink[0] == 0) continue;
        viable++;
        TEST_ASSERT_TRUE(pink[1] > 0);
      }
  TEST_ASSERT_TRUE(viable > 150);                        // the sweep must not silently skip itself
}

// The open bowl has a FIXED TOP and grows only downward from the shared omega smile. It must stay
// inside the chin throughout the rig's 0..0.50 band, leave the nose intact, and add a pink tongue
// only once the bowl is tall enough to hold one. Before the fixed-top rewrite, the value named
// mouth height was passed as an ellipse RADIUS while the centre moved by only half of it; the
// supposedly downward opening grew upward around the nose and visually swallowed it.
void test_open_mouth_limits() {
  static uint8_t g[CAT_GH][CAT_GW];
  for (int hs = 7; hs <= 14; hs++) {                     // headSize 0.7 .. 1.4
    CatPreset p = CAT_PRESET[0];
    p.headSize = (float)hs / 10.0f;
    int noseClosed = 0, blushClosed = 0, mouthTopClosed = CAT_GH;
    for (int mi = 0; mi <= 10; mi++) {
      float mouth = 0.05f * (float)mi;                   // 0 .. 0.50, the documented safe band
      // Drive the pose through blendFrom at blend 0 (catLerp returns it verbatim) rather than
      // the CAT_TUNE override seam: defining CAT_TUNE here would stop these tests compiling the
      // same code the device does, and this needs no new seam to reach one field.
      CatRenderState s; catInit(s);
      s.headYaw = s.headYawTarget = 0.0f;
      CatPose base = catEval(p, s);
      base.mouthOpen = mouth;
      s.blendFrom = base; s.blend = 0.0f;
      catRender(g, p, s, nullptr);
      CatPose q = catEval(p, s);
      int hr = (int)(q.headR * p.headSize + 0.5f);
      float cy = q.bodyY + q.headDy - q.bodyRy * 0.45f * (p.bodyChub - 1.0f);
      float earTop = (float)hr + p.earLen + (float)catEarRad(hr) + 2.0f;
      if (cy < earTop) cy = earTop;                      // catRender's own head lift
      int nose = 0, blush = 0, mouthCells = 0, mouthTop = CAT_GH, pastChin = 0;
      for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
        if (g[y][x] == CI_NOSE) nose++;
        if (g[y][x] == CI_BLUSH) blush++;
        if (g[y][x] == CI_MOUTH) {
          mouthCells++;
          if (y < mouthTop) mouthTop = y;
          if ((float)y > cy + (float)hr - 1.0f) pastChin++;
        }
      }
      if (!mi) {
        noseClosed = nose; blushClosed = blush; mouthTopClosed = mouthTop;
        TEST_ASSERT_TRUE(noseClosed > 0);
        TEST_ASSERT_TRUE(mouthCells > 0);                 // the closed omega is real geometry
        continue;
      }
      TEST_ASSERT_TRUE(mouthCells > 0);                  // the mouth must actually open
      TEST_ASSERT_EQUAL_INT(noseClosed, nose);           // the nose is never the casualty
      TEST_ASSERT_TRUE(mouthTop >= mouthTopClosed);       // the bowl never grows toward the nose
      if (mi == 10) TEST_ASSERT_TRUE(blush > blushClosed);  // widest opening always has a tongue
      TEST_ASSERT_EQUAL_INT(0, pastChin);                // ...the chin is, and not below 0.50
    }
  }
}

// The muzzle (nose + mouth) is silently deleted by the furOnly wrap once headYaw crosses ~0.75 --
// see the CAT_POSE[] comment in cat_proc.h for the mechanism (CF_NOSE.ce^2 + 0.34^2 == 1.0 exactly,
// so the nose anchor sits ON the skull rim at profile, and the mouth strokes below it fall entirely
// outside the head circle with no `fore` scaling to save them). Owner ruling: no code fix, this is
// a mechanical CEILING, not a bug to chase. Authored poses now stop at the stricter chibi art
// ceiling; this test keeps the wider stress range pinned so future rig work cannot regress it.
void test_muzzle_exists_up_to_the_yaw_ceiling() {
  const float yaws[] = { 0.0f, 0.3f, 0.6f, 0.75f };
  for (int i = 0; i < 4; i++) {
    CatRenderState s; catInit(s);
    s.headYaw = s.headYawTarget = yaws[i];
    static uint8_t g[CAT_GH][CAT_GW];
    catRender(g, CAT_PRESET[0], s, nullptr);
    int mouth = 0;
    for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) mouth += (g[y][x] == CI_MOUTH);
    TEST_ASSERT_GREATER_THAN_INT(0, mouth);
  }
}

// The anti-fuse cap is a MAXIMUM on eye width and must not run once one eye is hidden: the two
// projections converge past the limb (gap 0.194*hr at yaw 0.90, exactly 0 at 1.0), so a cap taken
// from the gap would crush the survivor to nothing.
void test_eye_cap_only_applies_while_both_visible() {
  CatTurn t = catTurn(1.0f);
  CatAz eL = catAz(28.0f, t, CF_EYE, -1.0f), eR = catAz(28.0f, t, CF_EYE, 1.0f);
  TEST_ASSERT_FALSE(eR.vis);                             // leading eye is gone at full profile
  TEST_ASSERT_TRUE(eL.vis);
  float gap = eR.x - eL.x; if (gap < 0) gap = -gap;
  TEST_ASSERT_TRUE(gap < 1.0f);                          // they have converged: cap would be < 0
  // ...so the render must still draw a full-width eye. Count CI_EYE cells at full yaw.
  CatRenderState s; catInit(s);
  s.headYaw = s.headYawTarget = 1.0f;
  static uint8_t g[CAT_GH][CAT_GW];
  CatPreset p = CAT_PRESET[0];
  catRender(g, p, s, nullptr);
  int eye = 0;
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) eye += (g[y][x] == CI_EYE);
  // Threshold recalibrated (final review, 2026-07-29): a mutation test that restores the deleted
  // `eye[0].vis && eye[1].vis` guard (i.e. runs the anti-fuse cap unconditionally, `if (true)`)
  // measured CI_EYE dropping 113 (unmutated) -> 26 (mutated) at this exact yaw, on the axis-aware
  // eye split that shipped alongside this cap. The old `> 12` bound sat BELOW the mutated value --
  // it passed on its own regression, because "~2 cells" was calibrated against the old isotropic
  // capsule eye (max(erx,ery) disc), and the axis-aware split turns a crushed eye into a 3x15
  // stadium instead. 60 sits cleanly between 26 and 113.
  TEST_ASSERT_GREATER_THAN_INT(60, eye);
}

// halfBase must GROW with earPoint (a pointier ear is a wider-based one, or it reads as a horn)
// while the tip radius shrinks -- the two together are what make the silhouette a cat ear.
void test_ear_half_base_grows_as_the_tip_narrows() {
  int R = catEarRad(28);
  float prevHB = -1.0f; int prevRad = 99;
  for (int i = 0; i <= 4; i++) {
    float pt = 0.25f * (float)i;
    int rad = catEarTipRad(28, pt);
    float hb = catEarHalfBase(R, rad, pt);
    TEST_ASSERT_TRUE(hb > prevHB);          // strictly wider
    TEST_ASSERT_TRUE(rad <= prevRad);       // never blunter
    prevHB = hb; prevRad = rad;
  }
  // earPoint 0 keeps today's root width exactly: 2*(R - rad) + 2*rad = 2R
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(R - catEarTipRad(28, 0.0f)), catEarHalfBase(R, catEarTipRad(28, 0.0f), 0.0f));
}

// The burial invariant, on the corners that actually get drawn. The pre-capsule triangle's base was
// a fixed-cell chord against a scaling skull and hung outside it as "wings"; these corners are
// sphere features, so |(x,y)| <= hr holds at every yaw with no clamp.
void test_ear_base_corners_stay_inside_the_skull() {
  for (int hr = 12; hr <= 40; hr += 4)
    for (int pi = 0; pi <= 4; pi++)
      for (int yi = 0; yi <= 10; yi++) {
        float pt = 0.25f * (float)pi;
        CatTurn t = catTurn((float)yi * 0.1f);
        int R = catEarRad(hr), rad = catEarTipRad(hr, pt);
        float hb = catEarHalfBase(R, rad, pt);
        for (float side = -1.0f; side <= 1.0f; side += 2.0f) {
          CatEar E = catEarShape(hr, 2.5f, side, 0.0f, t, hb);
          float h = (float)hr;
          TEST_ASSERT_TRUE(E.blx * E.blx + E.by * E.by <= h * h + 0.01f);
          TEST_ASSERT_TRUE(E.brx * E.brx + E.by * E.by <= h * h + 0.01f);
        }
      }
}

// Pink must never land on the skull. Adjacency CANNOT see this (pink over head fur is still
// surrounded by fur), so assert it geometrically instead: the head is a full disc of radius hr
// drawn BEFORE the inner ear, so any CI_EAR cell inside that circle is overpaint.
// ponytail: this replaces the diag provenance counter the spec proposed -- same guarantee, zero
// production code, and it mirrors the head's own containment test exactly.
//
// Both halves matter, and the first version of this test only checked one. The position loop's
// body is `if (g[y][x] != CI_EAR) continue`, so with zero pink painted it asserts nothing -- which
// is exactly what happened: the original `ihalf < 1.0f` guard (present since Task 5's first pass,
// unmodified through Fix round 1) rejected the pink outright at earPoint 0 (the then-shipped
// `CAT_PRESET[0]` value), so 6/6 of those cases ran this loop body zero times, along with 19/24 of
// the earPoint>0 sweep (25/30 total, silently vacuous). Count first, then assert existence.
// `earPoint` is a per-unit rig knob the owner ships ("some people really like the pointy
// ears"), not just an internal tuning value, so this now asserts `count > 0` at EVERY earPoint in
// the sweep (0/0.25/0.5/0.75/1.0), not only one preset value -- a pointy-eared unit with a bald inner
// ear is a shipped defect, not an edge case (fix round 3, owner catch). Fix round 3 also dropped
// the fixed "too thin to read as pink" width floor that made 0.75/1.0 paint nothing: the `ihalf`
// formula bounds its own minimum (see the production comment beside it), so there was never a
// correctness reason for that floor, only an aesthetic guess that turned out wrong. The combined
// (both-ears) count is what's asserted, not each ear individually -- at yaw 0.30 one ear's axis is
// legitimately fully consumed reaching a safe station before it draws anything, so only the other
// ear contributes, which is correct behaviour, not a gap.
//
// headSize sweep widened (final review, 2026-07-29): the assertion above only ever ran at the
// shipped `headSize` (hr = 28). `test_inner_ear_never_escapes_the_outer_ear`'s full hr 12-40 sweep
// shows 162/1740 cases paint NO pink at all, every one of them at hr <= 20 with earPoint 0.75/1.00
// (the `irad > avail` skip has no room even for the corner-rounding radius there) -- so a green
// suite at hr=28 alone was silently hiding that a future small-headed preset would ship bald ears.
// Two smaller headSize values are added here (1.0 -> hr 20, 0.8 -> hr 16) to make that limitation
// visible in the suite rather than papered over.
// The zero-pink region is NOT the clean rectangle "hr<=20 AND earPoint>=0.75" -- it also depends on
// pose (measured: hr=20/earPoint=1.00 is zero at the three yaw-0.90 poses but 14-16 cells at the
// yaw<=0.30 poses; hr=20/earPoint=0.75 is never zero). So existence is asserted unconditionally
// only where it provably always holds -- hr >= 21 -- which IS the boundary: at hr <= 20 no
// assertion is made (the per-cell skull-overpaint check below still runs on whatever DOES paint).
// This is the documented preset-authoring constraint: a future headSize below ~1.0 with a pointy
// ear can ship bald, and this test does not promise otherwise for that range.
void test_inner_ear_never_lands_on_the_skull() {
  static uint8_t g[CAT_GH][CAT_GW];
  const float headSizes[] = { 0.8f, 1.0f, 1.38f };   // hr 16, 20 (below the guarantee), 28 (shipped)
  for (int hs = 0; hs < 3; hs++)
    for (int pi = 0; pi <= 4; pi++)
      for (int a = 0; a < CA_COUNT; a++) {
        CatPreset p = CAT_PRESET[0]; p.headSize = headSizes[hs]; p.earPoint = 0.25f * (float)pi;
        CatRenderState s; catInit(s);
        s.cur = (CatAnim)a; s.headYaw = s.headYawTarget = CAT_POSE[a].yaw;
        catRender(g, p, s, nullptr);
        CatPose q = catEval(p, s);
        int hr = (int)(q.headR * p.headSize + 0.5f);
        float hx = q.bodyX + q.headDx + q.yaw * 12.0f;
        float hy = q.bodyY + q.headDy - q.bodyRy * 0.45f * (p.bodyChub - 1.0f);
        float earTop = (float)hr + p.earLen + (float)catEarRad(hr) + 2.0f;
        if (hy < earTop) hy = earTop;
        int hxi = (int)(hx + 0.5f), hyi = (int)(hy + 0.5f);
        int count = 0;
        for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
          if (g[y][x] != CI_EAR) continue;
          count++;
          int dx = x - hxi, dy = y - hyi;
          TEST_ASSERT_TRUE(dx * dx + dy * dy > hr * hr);
        }
        if (hr >= 21) TEST_ASSERT_GREATER_THAN_INT(0, count);   // guaranteed range: must ship pink
        // hr <= 20: existence not guaranteed (known gap, see comment above) -- no assertion here.
      }
}

// The opposite failure from the test above: that one catches pink too LOW (landing on the skull),
// this one catches pink too WIDE or too close to the tip -- escaping the outer ear onto the
// background or the outline ring.
//
// It asserts the invariant DIRECTLY: every CI_EAR cell must lie inside one of the two outer-ear
// rounded triangles, rebuilt here from the same public helpers catDrawOuterEar uses (catEarRad /
// catEarTipRad / catEarHalfBase / catEarShape / catHrot) and tested with the same predicate catRTri
// fills with (the three edge signs, else catNearSeg on the three sides). That is stricter than
// "pink isn't on the background": pink sitting on HEAD fur just outside the ear would satisfy
// r.furOnly and still fail here.
//
// It deliberately does NOT assert a fur BORDER (an earlier version checked 4-adjacency to
// CI_TRANS/CI_OUTLINE, which is a border test, not an escape test). Containment is now a mechanism
// -- the inner-ear fill runs under r.furOnly, so a non-fur cell cannot take the paint at all -- and
// the accepted cost of that ruling is that where the width model over-reaches, pink lands ON the
// ear's outermost fur cell instead of past it: 400 of these 1740 cases have zero fur between pink
// and the background (measured; 0 have pink on the background, which is what furOnly makes
// impossible). Re-adding an adjacency assertion would fail those 400 by design, and the previous
// attempt to buy that border back arithmetically -- a perpendicular-offset solve with a tuned
// cushion -- still left real escapes and could only be committed with the sweep narrowed to one
// preset. Don't narrow this sweep again; if it ever fails, the mechanism has been broken.
void test_inner_ear_never_escapes_the_outer_ear() {
  static uint8_t g[CAT_GH][CAT_GW];
  for (int hrTarget = 12; hrTarget <= 40; hrTarget++)       // headSize is solved for each hr below
    for (int outline = 0; outline <= 1; outline++)
      for (int pi = 0; pi <= 4; pi++)
        for (int a = 0; a < CA_COUNT; a++) {
          CatPreset p = CAT_PRESET[0];
          p.earPoint = 0.25f * (float)pi; p.outline = (uint8_t)outline;
          CatRenderState s; catInit(s);
          s.cur = (CatAnim)a; s.headYaw = s.headYawTarget = CAT_POSE[a].yaw;
          CatPose q = catEval(p, s);
          p.headSize = (float)hrTarget / q.headR;      // hr = round(headR * headSize)
          q = catEval(p, s);
          int hr = (int)(q.headR * p.headSize + 0.5f);
          TEST_ASSERT_EQUAL_INT(hrTarget, hr);
          catRender(g, p, s, nullptr);
          // Rebuild both outer ears exactly as catDrawOuterEar does, including catRender's own
          // head placement (the earTop lift matters: it moves hd.cy, and so every rotated corner).
          float hx = q.bodyX + q.headDx + q.yaw * 12.0f;
          float hy = q.bodyY + q.headDy - q.bodyRy * 0.45f * (p.bodyChub - 1.0f);
          float earTop = (float)hr + p.earLen + (float)catEarRad(hr) + 2.0f;
          if (hy < earTop) hy = earTop;
          CatHead hd; hd.cx = hx; hd.cy = hy; hd.ct = cosf(q.tilt); hd.st = sinf(q.tilt);
          CatTurn tn = catEarTurn(catTurn(q.headYaw), catEarYawGain);
          int R = catEarRad(hr), rad = catEarTipRad(hr, p.earPoint);
          float halfBase = catEarHalfBase(R, rad, p.earPoint);
          int vx[2][3], vy[2][3];
          for (int e = 0; e < 2; e++) {
            CatEar E = catEarShape(hr, p.earLen, e ? 1.0f : -1.0f, e ? q.earR : q.earL, tn, halfBase);
            catHrot(hd, E.blx, E.by, vx[e][0], vy[e][0]);
            catHrot(hd, E.brx, E.by, vx[e][1], vy[e][1]);
            catHrot(hd, E.tx,  E.ty, vx[e][2], vy[e][2]);
          }
          for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) {
            if (g[y][x] != CI_EAR) continue;
            bool in = false;
            for (int e = 0; e < 2 && !in; e++) {
              int x0 = vx[e][0], y0 = vy[e][0], x1 = vx[e][1], y1 = vy[e][1], x2 = vx[e][2], y2 = vy[e][2];
              long long A = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
              if (A < 0) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; A = -A; }
              if (A != 0) {
                long long e0 = (long long)(x1 - x0) * (y - y0) - (long long)(y1 - y0) * (x - x0);
                long long e1 = (long long)(x2 - x1) * (y - y1) - (long long)(y2 - y1) * (x - x1);
                long long e2 = (long long)(x0 - x2) * (y - y2) - (long long)(y0 - y2) * (x - x2);
                in = e0 >= 0 && e1 >= 0 && e2 >= 0;
              }
              if (!in && rad > 0)
                in = catNearSeg(x, y, x0, y0, x1, y1, rad) || catNearSeg(x, y, x1, y1, x2, y2, rad) ||
                     catNearSeg(x, y, x2, y2, x0, y0, rad);
            }
            TEST_ASSERT_TRUE(in);
          }
        }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fit_len_keeps_the_reach_in_range);
  RUN_TEST(test_extreme_yaw_and_size_stays_on_grid);
  RUN_TEST(test_ramp_helpers_scale_each_channel_at_its_own_depth);
  RUN_TEST(test_preset_ramps_are_monotonic_and_filled);
  RUN_TEST(test_shaded_ellipse_is_a_gradient_facing_the_light);
  RUN_TEST(test_far_shifts_one_band_and_clamps);
  RUN_TEST(test_contact_halo_darkens_only_what_is_under_it);
  RUN_TEST(test_contact_halo_does_not_raise_the_clip_flag);
  RUN_TEST(test_ellipse_fills_center_and_stays_in_bbox);
  RUN_TEST(test_offgrid_ellipse_sets_clip_flag_only);
  RUN_TEST(test_capsule_r0_is_continuous_line);
  RUN_TEST(test_capsule_thick_has_round_caps);
  RUN_TEST(test_tri_fills_and_ignores_winding);
  RUN_TEST(test_rtri_fills_interior_and_rounds_corners);
  RUN_TEST(test_rtri_degenerate_is_a_disc);
  RUN_TEST(test_rtri_shades_as_one_gradient);
  RUN_TEST(test_rtri_rim_grows_the_silhouette);
  RUN_TEST(test_masked_remap_stays_inside_fur);
  RUN_TEST(test_clipto_writes_only_over_target_index);
  RUN_TEST(test_occlusion_and_overlap_counters);
  RUN_TEST(test_idle_render_in_bounds_and_nonempty);
  RUN_TEST(test_meow_cycle_has_readable_vocal_beats);
  RUN_TEST(test_meow_cycle_stays_in_bounds_with_a_complete_face);
  RUN_TEST(test_sleep_cycle_is_a_quiet_loaf_with_one_dream_flick);
  RUN_TEST(test_sleep_cycle_stays_in_bounds_through_breathing_extremes);
  RUN_TEST(test_no_keyframe_buries_the_head_in_the_body);
  RUN_TEST(test_every_once_pose_returns_exactly_to_its_mood_base);
  RUN_TEST(test_mood_gestures_have_distinct_body_beats);
  RUN_TEST(test_grooming_variants_have_their_own_repeated_beats);
  RUN_TEST(test_blaze_marking_lightens_the_chest_and_keeps_the_tabby);
  RUN_TEST(test_adore_gazes_up_with_a_head_tilt);
  RUN_TEST(test_sixth_pass_has_one_distinct_beat_per_mood);
  RUN_TEST(test_every_sleep_entry_damps_the_tail_before_the_loaf);
  RUN_TEST(test_stretch_holds_full_extension_long_enough);
  RUN_TEST(test_pounce_crouches_wiggles_then_leaves_the_ground);
  RUN_TEST(test_slow_blink_holds_closed_eyes_on_a_still_body);
  RUN_TEST(test_sniff_leans_in_and_bobs_with_a_closed_mouth);
  RUN_TEST(test_waking_rises_through_a_yawn_and_a_stretch);
  RUN_TEST(test_authored_faces_avoid_the_bar_band_and_the_muzzle_hole);
  RUN_TEST(test_itch_pulses_its_scratch);
  RUN_TEST(test_scratch_leg_holds_the_near_plane_and_clears_the_head);
  RUN_TEST(test_lick_paw_reaches_the_muzzle_and_stays_off_the_eyes);
  RUN_TEST(test_every_authored_pose_stays_on_grid);
  RUN_TEST(test_sleep_tail_turns_back_over_the_near_rump);
  RUN_TEST(test_idle_tail_keeps_its_rear_plane_sweep);
  RUN_TEST(test_tail_authoring_band_stays_in_bounds);
  RUN_TEST(test_leg_projection_separates_rear_and_front_at_profile);
  RUN_TEST(test_leg_projection_keeps_rear_outside_fore_when_frontal);
  RUN_TEST(test_mirror_is_an_exact_pixel_flip_for_every_pose);
  RUN_TEST(test_render_is_deterministic_and_diag_free);
  RUN_TEST(test_once_pose_terminates);
  RUN_TEST(test_loop_pose_never_done);
  RUN_TEST(test_accumulator_wrap_is_continuous);
  RUN_TEST(test_interrupted_blend_does_not_pop);
  RUN_TEST(test_variants_diverge_by_rate);
  RUN_TEST(test_head_yaw_slews_toward_target);
  RUN_TEST(test_flat_collapses_the_torso_bands);
  RUN_TEST(test_outline_rims_the_silhouette);
  RUN_TEST(test_outline_underdraw_sits_outside_the_fill);
  RUN_TEST(test_outline_underdraw_has_no_interior_bamboo);
  RUN_TEST(test_fur_only_skips_the_outline_band);
  RUN_TEST(test_clipto_wins_over_furonly_on_the_same_pixel);
  RUN_TEST(test_ear_tip_radius_narrows_with_earPoint);
  RUN_TEST(test_whisker_twitch_schedules_fires_and_decays);
  RUN_TEST(test_tabby_tail_carries_accent_rings);
  RUN_TEST(test_disc_clip_confines_a_fill_to_its_circle);
  RUN_TEST(test_feat_table_matches_its_derivation);
  RUN_TEST(test_facing_zero_is_the_old_expression);
  RUN_TEST(test_forehead_ticks_follow_spherical_meridian_arcs);
  RUN_TEST(test_eye_separation_uses_the_live_catEyeK_derivation);
  RUN_TEST(test_facing_features_stay_inside_the_skull);
  RUN_TEST(test_facing_vanishes_only_at_its_own_rim);
  RUN_TEST(test_face_layout_at_shipped_yaws);
  RUN_TEST(test_pose_head_yaws_stay_in_the_chibi_art_band);
  RUN_TEST(test_ear_turn_is_attenuated_and_normalized);
  RUN_TEST(test_leading_inner_ear_survives_the_art_ceiling);
  RUN_TEST(test_open_mouth_limits);
  RUN_TEST(test_muzzle_exists_up_to_the_yaw_ceiling);
  RUN_TEST(test_eye_cap_only_applies_while_both_visible);
  RUN_TEST(test_ear_half_base_grows_as_the_tip_narrows);
  RUN_TEST(test_ear_base_corners_stay_inside_the_skull);
  RUN_TEST(test_inner_ear_never_lands_on_the_skull);
  RUN_TEST(test_inner_ear_never_escapes_the_outer_ear);
  return UNITY_END();
}
