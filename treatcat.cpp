// Interactive "treat cat" (anim id 46). Ported from
// references/treatcat-round-lcd/treatcat_gc9a01/treatcat_gc9a01.ino (LovyanGFX) to ocellus's
// Arduino_GFX. Renders into the shared canvas; dispatched from main.cpp (renderTreatcat). Grown into
// a light care-loop pet: the pure PetState model (pet_state.*) drives an animated cat (kitty_anim.h).
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <string.h>
#include "treatcat.h"
#include "animations.h"
#include "shufflebag.h"
#include "cat_proc.h"
#include "cat_choreo.h"
#include "pet_state.h"
#include "config.h"
#include "config_store.h"
#include "treatcat_content.h"
#include "font_freesans9.h"   // "middle" dialog font (proportional 9pt); Arduino_GFX_Library.h above defines its GFXfont structs

extern Arduino_GFX      *gfx;      // main.cpp:210
extern Arduino_Canvas   *canvas;   // main.cpp:227
extern Config           gConfig;   // main.cpp:40

volatile uint32_t gTreatTap = 0;

static const int W = 240, H = 240;
static_assert(AFFIRM_N <= 96 && QUOTES_N <= 96 && CATLINES_N <= 96, "a content pool outgrew ShuffleBag::q");

// injected RNG for the shuffle bags (Arduino random is fine here; the picker stays Arduino-free)
static uint32_t tcRng(uint32_t n) { return (uint32_t)random(0, (long)n); }
static ShuffleBag bAff{{},0,-1}, bQuo{{},0,-1}, bCat{{},0,-1};
static const char* pickAffirm() { return AFFIRM[shufflebagPick(bAff, AFFIRM_N, tcRng)]; }

struct Heart { float x, y, vx, vy, wob; uint32_t born; };   // wob = small spawn-time phase (bounded arg into tsin)
struct Star  { float x, y, phase, sp; bool cross; };        // phase advances per frame, kept in [0,2pi)
struct Cloud { float x, y, scale, sp; };
static const int NHE = 48, NST = 40, NCL = 4;
static Heart hearts[NHE]; static int nHe = 0;
static Star  stars[NST];
static Cloud clouds[NCL];
#if defined(BOARD_WAVESHARE_128)
static constexpr bool kCareEnabled = true;    // battery board: touch feeds
#else
static constexpr bool kCareEnabled = false;   // bench S3 / C3 / console: ambient, no decay
#endif
static PetState pet;
static bool  fortuneMode = false;
static uint32_t fortuneStart = 0, revealDoneAt = 0, bounceAt = 0, rotateAt = 0;
static String panelText, fortuneText;
static int   catRX, catRY, catRW, catRH, boxRX, boxRY, boxRW, boxRH;
static bool  inited = false;
static uint32_t lastNow = 0;

static uint32_t rotateGap() { return 27000 + (uint32_t)random(0, 6000); }   // lab t-units (~60-73s real after /0.45)

static void tcInit() {
  randomSeed(esp_random());
  for (int i = 0; i < NST; i++) stars[i] = { (float)random(0,60), (float)random(0,40),
    (float)random(0,6283)/1000.0f, 0.0012f + (float)random(0,3000)/1000000.0f, random(0,100) < 16 };
  clouds[0] = {6,10,1.1f,0.0032f}; clouds[1] = {38,20,1.4f,0.0021f};
  clouds[2] = {24,5,0.85f,0.0027f}; clouds[3] = {50,34,1.0f,0.0018f};
  panelText = pickAffirm();
  pet.reset(treatsLoad());     // full stats, treat count carried from NVS (0 on blank)
  inited = true;
}

// treatcat uses radian Math.sin in the lab -> NOT ocellus fastSin (byte-index). Own LUT: index by
// radians*(256/2pi), mask 0xFF so a negative/bounded phase wraps cleanly. C3-cheap, no soft-float trig.
static int16_t s_sin[256]; static bool s_sinReady = false;
static void tsinInit() { for (int i = 0; i < 256; i++) s_sin[i] = (int16_t)(sinf(i * 2.0f * (float)PI / 256.0f) * 127.0f); s_sinReady = true; }
static inline float tsin(float rad) { int idx = (int)(rad * (256.0f / (2.0f * (float)PI))); return s_sin[idx & 0xFF] / 127.0f; }

static const int P = 4;
static void pR(int gx,int gy,int gw,int gh,uint16_t c){ canvas->fillRect(gx*P,gy*P,gw*P,gh*P,c); }
static void pDisc(float cx,float cy,float r,uint16_t c){
  for(int gy=(int)(cy-r);gy<=(int)(cy+r);gy++){ float dy=gy-cy,k=r*r-dy*dy; if(k<0)continue;
    float dx=sqrtf(k); int x0=(int)lroundf(cx-dx),x1=(int)lroundf(cx+dx);
    canvas->fillRect(x0*P,gy*P,(x1-x0+1)*P,P,c); } }

// ---- decorative helpers, ported 1:1 from treatcat_gc9a01.ino (cv.-> canvas->, col565-> gfx->color565) ----

// fluffy cloud: overlapping puffs (grid coords) with a soft under-shadow (.ino 103-108)
static void fluffy(float cx, float cy, float s) {
  static const float puffs[7][3] = {{0,3,3},{4,1,4},{8,-0.2f,4.6f},{12,1,4},{15.5f,3,3.2f},{6,4,3.6f},{10.5f,4,3.3f}};
  uint16_t Wc = gfx->color565(238,242,251), Sc = gfx->color565(195,203,224);
  for (int i = 0; i < 7; i++) pDisc(cx+puffs[i][0]*s, cy+puffs[i][1]*s+1.2f, puffs[i][2]*s, Sc);
  for (int i = 0; i < 7; i++) pDisc(cx+puffs[i][0]*s, cy+puffs[i][1]*s, puffs[i][2]*s, Wc);
}

// pixel heart, 5x5, s-px blocks (.ino 111-114)
static void pHeart(int x, int y, int s, uint16_t c) {
  static const char* rows[5] = {"01010","11111","11111","01110","00100"};
  for (int ry = 0; ry < 5; ry++) for (int rx = 0; rx < 5; rx++) if (rows[ry][rx] == '1') canvas->fillRect(x+rx*s, y+ry*s, s, s, c);
}

// single-object moon with a real phase: fill only the lit region bounded by the terminator.
// k: -1 full .. ->1 thin crescent. left=true lights the left side. (.ino 118-127)
static void moon(float cx, float cy, int R, float k, bool left) {
  const int B = 2; uint16_t C = gfx->color565(233,231,244), CR = gfx->color565(200,198,222);
  cx = lroundf(cx/B)*B; cy = lroundf(cy/B)*B;
  for (int gy = -R; gy <= R; gy += B) { float w = sqrtf(fmaxf(0, R*R - gy*gy)); if (w < B*0.6f) continue;
    float term = k*w; int x0 = (int)(left ? -w : term), x1 = (int)(left ? -term : w);
    for (int gx = (int)(lroundf((float)x0/B)*B); gx <= x1; gx += B) canvas->fillRect((int)cx+gx, (int)cy+gy, B, B, C); }
  static const int cr[4][2] = {{-4,-4},{4,4},{-8,4},{6,-6}};
  for (int i = 0; i < 4; i++) { int lx = cr[i][0], ly = cr[i][1]; float w = sqrtf(fmaxf(0, R*R - ly*ly));
    if (left ? (lx <= -k*w) : (lx >= k*w)) canvas->fillRect((int)cx+lx, (int)cy+ly, B, B, CR); }
}

// pokemon-style battle box: dark keyline, blue frame, white interior (.ino 130-134)
static void battleBox(int x, int y, int w, int h) {
  canvas->fillRoundRect(x, y, w, h, 10, gfx->color565(18,26,46));
  canvas->fillRoundRect(x+2, y+2, w-4, h-4, 8, gfx->color565(63,111,208));
  canvas->fillRoundRect(x+5, y+5, w-10, h-10, 5, gfx->color565(247,248,242));
}

// parchment box for fortunes (shadow + border + fill), plus an inner keyline (.ino 136-141)
static void pixBox(int x, int y, int w, int h) {
  canvas->fillRect(x+4, y+4, w, h, gfx->color565(18,16,46));                  // shadow
  canvas->fillRect(x, y, w, h, gfx->color565(239,228,166));                   // border
  canvas->fillRect(x+3, y+3, w-6, h-6, gfx->color565(35,33,82));              // fill
  canvas->drawRect(x+5, y+5, w-10, h-10, gfx->color565(143,134,214));         // inner keyline
}

// ---- sky moods: [top, mid, bottom] rgb, cross-faded over time (.ino 69-92) ----
static const int NSKY = 8;
static const uint8_t SKY[NSKY][3][3] = {
  {{26,42,90},{106,74,138},{200,90,106}},   // dusk
  {{34,34,74},{74,58,112},{160,90,128}},    // twilight
  {{14,38,54},{28,112,96},{64,64,124}},     // aurora
  {{40,20,70},{122,50,132},{212,112,152}},  // amethyst
  {{12,28,66},{26,86,122},{72,152,162}},    // deep sea
  {{44,30,84},{150,72,90},{224,150,80}},    // ember
  {{30,58,110},{90,90,154},{176,106,138}},  // clear dusk
  {{42,42,106},{122,74,138},{208,106,90}},  // sunset
};
static float g_skyF; static int g_skyI;   // set each frame; skyAt() reads them
static void skyBlend(int stop, int& r, int& g, int& b) {
  const uint8_t* A = SKY[g_skyI][stop]; const uint8_t* B = SKY[(g_skyI+1)%NSKY][stop];
  r = A[0]+(B[0]-A[0])*g_skyF; g = A[1]+(B[1]-A[1])*g_skyF; b = A[2]+(B[2]-A[2])*g_skyF;
}
// sky color at a given y (top->mid over the upper half, mid->bottom over the lower half)
static void skyAt(int y, int& r, int& g, int& b) {
  int r0,g0,b0,r1,g1,b1; float f;
  if (y < H/2) { skyBlend(0,r0,g0,b0); skyBlend(1,r1,g1,b1); f = (float)y/(H/2); }
  else         { skyBlend(1,r0,g0,b0); skyBlend(2,r1,g1,b1); f = (float)(y-H/2)/(H/2); }
  r = r0+(r1-r0)*f; g = g0+(g1-g0)*f; b = b0+(b1-b0)*f;
}
static void drawSky() { for (int y = 0; y < H; y++) { int r,g,b; skyAt(y,r,g,b); canvas->drawFastHLine(0,y,W,gfx->color565(r,g,b)); } }

// ---- procedural cat (cat_proc.h): a CAT_GW x CAT_GH index grid, block-blitted xCAT_P. ----
// Geometry is DERIVED from the grid so it can't drift from the renderer. Replaces the baked
// 25x17 sprite bank; see the spec's Geometry section for why the grid was re-aspected.
static const int CAT_W  = CAT_GW * CAT_P;           // 112
static const int CAT_H  = CAT_GH * CAT_P;           // 112
static const int CAT_DX = (W - CAT_W) / 2;          // 64
// The cat STANDS ON the affirmation box: renderTreatcat measures the box before drawing the cat
// and grounds the blit so the paws' bottom row (grid CAT_GH-2 -- catProjectLegs' pawYMax plus the
// leg capsule radius) lands on the box's top frame row, which the box then draws over. This is
// not the old "CAT_DY = 0, feet on the box line" (whose 35-48% occlusion of the low poses forced
// the fixed-float CAT_DY = 24 era): the blit tracks the MEASURED top, so a taller box pushes the
// whole cat up instead of swallowing it. Worst-case 3-line box (top y=146) puts the blit at 36,
// keeping the ears (grid row ~4) clear of the treat counter at y=4..22.
static const int CAT_GROUND_ROW = (CAT_GH - 2) * CAT_P;   // blit y = boxTop - CAT_GROUND_ROW

static uint8_t        gCatGrid[CAT_GH][CAT_GW];     // 12.5 KB scratch, internal RAM
static CatRenderState gCatRS;
static uint32_t       gCatLastMs = 0;

// index grid -> framebuffer. Slot 0 is transparent (the sky shows through), exactly as
// CAT_KEY_IDX was for the baked art.
static void blitCatGrid(int dx, int dy, const CatPreset& p) {
  uint16_t* fb = canvas->getFramebuffer();
  for (int sy = 0; sy < CAT_GH; sy++) {
    for (int sx = 0; sx < CAT_GW; sx++) {
      uint8_t idx = gCatGrid[sy][sx];
      if (idx == CI_TRANS) continue;
      uint16_t c = p.pal[idx];
      for (int yy = 0; yy < CAT_P; yy++) { int py = dy + sy*CAT_P + yy; if (py < 0 || py >= H) continue;
        for (int xx = 0; xx < CAT_P; xx++) { int px = dx + sx*CAT_P + xx; if (px < 0 || px >= W) continue;
          fb[py * W + px] = c; } } } }
}

// once-play reaction that overrides the base (CA_COUNT = none). Set by feed/pet/quirk.
static CatAnim  catReact = CA_COUNT;
static uint32_t catReactStart = 0;
// Gesture scheduling (mood edges, idle fidget + beg clocks, the mood pools) lives in
// cat_choreo.h so it is natively tested; this file only starts what it returns.
static CatChoreo choreo;
static uint32_t catRnd(uint32_t n) { return (uint32_t)random(0, (long)n); }

// Pick base from mood, run the reaction/quirk overrides, evolve + draw. now/dy from renderTreatcat
// (dy is the finished blit y: measured box top minus CAT_GROUND_ROW, hop already applied).
static void spawnHearts(int x, int y, uint32_t now);   // defined below with the tap handlers

static void selectAndDrawCat(PetMood m, uint32_t now, int dy) {
  // catVariant is 0..5 by config contract, but only CAT_PRESET_N presets exist until the
  // six-preset task lands — clamp rather than read off the end of the table.
  uint8_t variant = gConfig.catVariant < CAT_PRESET_N ? gConfig.catVariant : 0;
  const CatPreset& p = CAT_PRESET[variant];
  CatAnim base = (m == PET_SLEEPY) ? CA_SLEEPING : (m == PET_NEEDY) ? CA_MEOW : CA_IDLE;

  if (catReact != CA_COUNT && catPoseDone(gCatRS)) {                      // once-play finished
    choreo.onGestureEnd(catReact, now, catRnd);   // a wash or itch may schedule its bout follow-up
    catReact = CA_COUNT;
  }
  CatAnim start = choreo.next(m, now, catReact != CA_COUNT, catRnd);
  if (start != CA_COUNT) {
    if (start == CA_SNIFF) gCatRS.twitchT = CAT_TWITCH_MS;   // whiskers flick as the nose leads
    // A SPONTANEOUS slow blink, adoring gaze or head bunt is the cat offering affection
    // unprompted (the pet response already gets its hearts at the tap site) -- the reference
    // art floats hearts by the head.
    if (start == CA_SLOWBLINK || start == CA_ADORE || start == CA_HEAD_BUNT)
      spawnHearts(catRX + catRW / 2, catRY + catRH / 3, now);
    catReact = start;
    catReactStart = now;
  }
  if (choreo.dreamTwitch(m, now, catRnd)) gCatRS.twitchT = CAT_TWITCH_MS;   // dreaming, mid-loaf

  // Restart on a NEW trigger too, not just a changed pose: the tap handlers re-arm catReact
  // with a fresh catReactStart, and tapping twice mid-stretch must replay it. (The baked path
  // got this free from its frame index; catSetPose is edge-triggered, so it needs the seq.)
  static uint32_t lastReactStart = 0;
  CatAnim show = (catReact != CA_COUNT) ? catReact : base;
  if (show != gCatRS.cur || catReactStart != lastReactStart) {
    catSetPose(gCatRS, p, show);                         // blends from the on-screen pose
    lastReactStart = catReactStart;
  }

  // Half-rate pose: catRender is ~2/3 of the whole frame (measured 14.4 of 21.9 ms, [tc] profile
  // 2026-07-30), and at 30 fps it left no frame gap, so the light-sleep nap never fired and the
  // board sat ~22 mA above an eye mode. The pose morphs are slow enough that evolving the GRID at
  // 15 Hz is invisible; the blit below still runs every frame, so the hop and the box-top ground
  // keep full-rate motion. dt accumulates across the skipped frame (gCatLastMs only advances when
  // we render), so the pose clock does not slow down.
  // ponytail: frame-parity skip, not change-detection -- breathe evolves every frame anyway.
  static bool catSkip = false;
  catSkip = !catSkip;
  if (!catSkip || gCatLastMs == 0) {
    uint32_t dt = gCatLastMs ? now - gCatLastMs : 16;    // first frame after enter: nominal
    if (dt > 200) dt = 200;                              // a long stall must not fast-forward the pose
    gCatLastMs = now;
    catAdvance(gCatRS, p, (float)dt);
    catRender(gCatGrid, p, gCatRS, nullptr);             // nullptr: no CatDiagnostics on device
  }
  blitCatGrid(CAT_DX, dy, p);
}

// greedy word-wrap against the CURRENTLY SET GFX font (caller sets it first). Heap-free: candidate
// lines are measured in a stack buffer and emitted into fixed line buffers -- no per-frame String
// alloc (this runs every frame, and treatcat is always-on on the console).
static int wrapGfx(const char* s, int maxW, char out[][64], int maxLines) {
  int n = 0, i = 0, len = (int)strlen(s);
  char line[64]; int ll = 0; line[0] = 0;
  while (n < maxLines && i <= len) {
    int j = i; while (j < len && s[j] != ' ') j++;            // word = s[i..j)
    char cand[96]; int cl = 0;
    if (ll) { memcpy(cand, line, ll); cl = ll; cand[cl++] = ' '; }
    int wl = j - i; if (wl > (int)sizeof(cand) - 1 - cl) wl = (int)sizeof(cand) - 1 - cl;
    memcpy(cand + cl, s + i, wl); cl += wl; cand[cl] = 0;
    int16_t x1, y1; uint16_t w, h; canvas->getTextBounds(cand, 0, 0, &x1, &y1, &w, &h);
    if ((int)w > maxW && ll > 0) {                            // too wide: flush the line, the word starts the next
      memcpy(out[n], line, ll); out[n][ll] = 0; n++;
      ll = j - i; if (ll > 63) ll = 63; memcpy(line, s + i, ll); line[ll] = 0;
    } else {                                                  // accept the candidate
      ll = cl > 63 ? 63 : cl; memcpy(line, cand, ll); line[ll] = 0;
    }
    if (j >= len) { if (n < maxLines) { memcpy(out[n], line, ll); out[n][ll] = 0; n++; } break; }
    i = j + 1;
  }
  return n;
}
static void drawCentered(const char* s, int cx, int y, int size, uint16_t color) {
  int w = (int)strlen(s) * 6 * size;
  canvas->setTextSize(size); canvas->setTextColor(color);
  canvas->setCursor(cx - w / 2, y); canvas->print(s);
}

static void startFortune(uint32_t now) {
  fortuneMode = true; fortuneStart = now; revealDoneAt = 0;
  int r = random(0, 3);   // ~2/3 author quotes, ~1/3 cat-voice
  fortuneText = (r < 2) ? QUOTES[shufflebagPick(bQuo, QUOTES_N, tcRng)]
                        : CATLINES[shufflebagPick(bCat, CATLINES_N, tcRng)];
}
static void spawnHearts(int x, int y, uint32_t now) {
  int n = 2 + random(0, 3);
  for (int i = 0; i < n && nHe < NHE; i++) hearts[nHe++] = { (float)x + (random(0,1000)/1000.0f-0.5f)*20,
    (float)y - 6, (random(0,1000)/1000.0f-0.5f)*0.5f, -(0.5f + random(0,600)/1000.0f), (float)random(0,6283)/1000.0f, now };
}

static void onTap(int x, int y, uint32_t now) {
  if (fortuneMode) { if ((float)(now - fortuneStart) * 0.45f > 350) fortuneMode = false; return; }  // guard the triggering tap
  if (x >= boxRX && x <= boxRX+boxRW && y >= boxRY && y <= boxRY+boxRH) {   // box -> next affirmation
    panelText = pickAffirm(); rotateAt = now + (uint32_t)(rotateGap() / 0.45f);
    gCatRS.twitchT = CAT_TWITCH_MS;   // the cat NOTICES the box change beside it -- whisker flick
    return; }
  if (!(x >= catRX && x <= catRX+catRW && y >= catRY && y <= catRY+catRH)) return;  // missed the cat
  bounceAt = now;                                                          // hop acknowledges any cat tap
  if (kCareEnabled) {
    pet.forceWake();                                                       // a tap always wakes + reacts
    if (pet.feed()) {                                                      // hungry -> ate
      catReact = CA_LICKING; catReactStart = now;
      choreo.onFeed(now);                       // lick first, then the post-meal face wash
      spawnHearts(x, y, now);
      treatsSave(pet.treats());                                           // immediate NVS write (bounded: feed only fires below threshold)
      if (pet.treats() % 5 == 0) startFortune(now);
    } else {                                                               // content -> pet: affection only, no stat, no persist
      catReact = choreo.onPet(catRnd); catReactStart = now;                // stretch, tail hug -- or a slow blink BACK
      spawnHearts(x, y, now);
    }
  } else {                                                                 // ambient boards have no tap source; harmless fallback
    catReact = choreo.onPet(catRnd); catReactStart = now; spawnHearts(x, y, now);
  }
}

static void drawFortune(uint32_t now) {
  canvas->setFont();   // pixBox uses no font; footer below uses the built-in one
  float prog = fminf(1, (float)(now - fortuneStart) * 0.45f / 220.0f);
  int top = (int)lroundf(42 - (1 - prog) * 140);
  pixBox(26, top, 188, 150);
  long el = (long)((float)(now - fortuneStart) * 0.45f) - 220;   // typewriter starts after the drop settles
  int chars = (int)(el / 26);
  int flen = (int)fortuneText.length();
  if (chars >= flen && !revealDoneAt) revealDoneAt = now;
  if (revealDoneAt && (float)(now - revealDoneAt) * 0.45f > 2600) fortuneMode = false;   // ~5.8s real
  bool cur = !revealDoneAt && ((now >> 7) & 1);
  int shown = chars < 0 ? 0 : min(chars, flen);
  char buf[160]; int k = 0;
  for (int i = 0; i < shown && k < 158; i++) buf[k++] = fortuneText[i];
  if (cur && k < 158) buf[k++] = '_';
  buf[k] = 0;
  // FreeSans 9pt body, tight 17px pitch so even the longest ~115-char quote fits ~7 lines inside the box
  canvas->setFont(&FreeSans9pt7b); canvas->setTextSize(1);
  char lines[8][64]; int nl = wrapGfx(buf, 160, lines, 8);
  canvas->setTextColor(gfx->color565(239,228,166));
  for (int i = 0; i < nl; i++) { canvas->setCursor(40, top + 22 + i*17); canvas->print(lines[i]); }
  canvas->setFont();   // built-in for the footer + the next frame's counter
  if (revealDoneAt) drawCentered("tap to dismiss", 120, top + 140, 1, gfx->color565(143,134,214));
}

// Called from main.cpp's onAnimEnter when treatcat becomes active: drop any tap latched in another
// mode, and start on a fresh scene (don't resume a half-typed fortune from a prior visit). The treat
// count is deliberately kept -- it's "endless".
void treatcatOnEnter() { gTreatTap = 0; fortuneMode = false; revealDoneAt = 0;
  catInit(gCatRS); gCatLastMs = 0; catReact = CA_COUNT;
  gCatRS.mirror = random(0, 2) != 0;   // scene facing, decided once per visit; exact image flip
  choreo.enterScene(); }

// {"cmd":"pet"} / {"cmd":"petsim","full":N,"en":N}. Substring-matched the same way "bat"/"batsim"
// are: the quoted "pet" cannot match inside "petsim", so order does not matter here.
bool treatcatPetCmd(const char* line, char* out, unsigned outLen) {
  bool sim = strstr(line, "\"petsim\"") != nullptr;
  bool tap = strstr(line, "\"pettap\"") != nullptr;
  if (!sim && !tap && strstr(line, "\"pet\"") == nullptr) return false;
  if (sim) {
    const char* pf = strstr(line, "\"full\"");
    const char* pe = strstr(line, "\"en\"");
    const char* pt = strstr(line, "\"treats\"");
    float f = pf ? (float)atof(strchr(pf, ':') + 1) : pet.fullness();
    float e = pe ? (float)atof(strchr(pe, ':') + 1) : pet.energy();
    pet.debugSet(f, e);
    if (pt) pet.debugSetTreats((uint32_t)atoi(strchr(pt, ':') + 1));
    catReact = CA_COUNT;            // drop any once-play reaction so the new mood shows immediately
  }
  // {"cmd":"pettap"} injects a tap at the cat's centre through the SAME mailbox the touch ISR
  // uses, so it exercises onTap's real path (hit test, feed-vs-pet split, fortune trigger) rather
  // than a parallel one. Without it the feed loop cannot be tested from the host at all.
  //
  // The mailbox carries SCREEN coordinates (touchPoll() normalizes at read, see touch.cpp), same
  // as catRX/catRY, so no rotation compensation belongs here anymore (2026-08-01).
  if (strstr(line, "\"pettap\"") != nullptr) {
    int tx = catRX + catRW / 2, ty = catRY + catRH / 2;
    gTreatTap = 0x80000000u | ((uint32_t)tx << 12) | (uint32_t)ty;
  }
  // fortune is reported because it is otherwise unobservable from the host: it is a screen state
  // with no stat behind it, so "did the 5-treat fortune fire?" had no answer short of watching the
  // panel. That is exactly the question this hook exists to settle.
  snprintf(out, outLen,
           "{\"type\":\"pet\",\"full\":%.1f,\"en\":%.1f,\"treats\":%u,\"asleep\":%s,\"fortune\":%s}",
           pet.fullness(), pet.energy(), (unsigned)pet.treats(),
           pet.asleep() ? "true" : "false", fortuneMode ? "true" : "false");
  return true;
}

void renderTreatcat(uint32_t now) {
  if (!s_sinReady) tsinInit();
  if (!inited) tcInit();

  // ---- sky (scaled absolute time, fmod'd so precision drift at extreme uptime is cosmetic) ----
  float t = (float)now * 0.45f;                 // lab t-units
  float ph = fmodf(t * 0.00012f, NSKY); g_skyI = (int)ph; g_skyF = ph - g_skyI;
  drawSky();

  // ---- stars, moon, clouds (bounded phase for the fast twinkle) ----
  float dt = (float)(now - lastNow); if (dt < 0 || dt > 200) dt = 33;   // clamp re-entry / first frame
  lastNow = now;
  pet.tick(dt, kCareEnabled);              // REAL-ms decay; ambient boards no-op
  PetMood pm = pet.mood(kCareEnabled);
  for (int i = 0; i < NST; i++) { Star& s = stars[i];
    s.phase += dt * 0.45f * s.sp; if (s.phase > 6.2831853f) s.phase -= 6.2831853f;
    float a = 0.3f + 0.7f * (0.5f + 0.5f * tsin(s.phase));
    int br = (int)(255 * a); uint16_t c = gfx->color565(br, br, (int)(br * 0.92f));
    if (s.cross) { pR((int)s.x, (int)s.y-1, 1, 3, c); pR((int)s.x-1, (int)s.y, 3, 1, c); }
    else pR((int)s.x, (int)s.y, 1, 1, c); }
  const float mspan = W + 90; float mx = fmodf(t * 0.0055f, mspan) - 45, my = W * 0.19f; const int mr = 14;
  if (mx > -mr && mx < W + mr) { int pass = ((int)(t * 0.0055f / mspan)) % 7;
    static const float pk[7] = {-1,-0.55f,-0.1f,0.35f,0.6f,0.1f,-0.35f};
    static const bool  pl[7] = {true,true,false,false,true,true,false};
    moon(mx, my, mr, pk[pass], pl[pass]); }
  for (int i = 0; i < NCL; i++) { Cloud& c = clouds[i];
    // span/offset sized so the cloud body (~-3..+19 grid * scale) is fully off both edges at the wrap,
    // so it scrolls in from the left edge instead of popping in half-drawn (old 60+18/-9 wrapped mid-screen)
    float span = 60 + 24 * c.scale, cx = fmodf(c.x + t * c.sp, span) - 20 * c.scale; fluffy(cx, c.y, c.scale); }

  // ---- affirmation box: PLEA while hungry (board), else rotate on the idle timer.
  // Selected and MEASURED here, before the cat, because the box top is the cat's ground; DRAWN
  // after the cat (below) so its frame overlaps the paw row. Runs on fortune frames too,
  // invisibly -- the ground must not depend on whether the box is on screen this frame.
  static bool wasNeedy = false;
  bool needy = kCareEnabled && pm == PET_NEEDY;
  if (needy) {
    panelText = String(PLEA);
  } else if (wasNeedy) {                                                   // just recovered: fresh line now, don't wait for the timer
    panelText = pickAffirm(); rotateAt = now + (uint32_t)(rotateGap() / 0.45f);
  } else {
    if (rotateAt == 0) rotateAt = now + (uint32_t)(rotateGap() / 0.45f);
    if (now >= rotateAt) { panelText = (random(0,100) < 28) ? String(PLEA) : String(pickAffirm());
      rotateAt = now + (uint32_t)(rotateGap() / 0.45f); }
  }
  wasNeedy = needy;

  // FreeSans 9pt: a readable "middle" between the 8px and 16px built-in font; proportional, so a
  // sentence fits in fewer lines -> a shorter box that covers less of the cat. Dialogue-box style
  // (left-aligned block), box auto-sized from the wrapped text via getTextBounds.
  canvas->setFont(&FreeSans9pt7b); canvas->setTextSize(1);
  const int bottomY = 214, pad = 7, padX = 11, lh = 18, ascent = 13;   // 9pt: ~13px ascent, 18px line pitch. padX>pad: battleBox's 5px frame eats the margin, so horizontal text needs extra to clear the wall
  float R = W / 2.0f; float hwMax = sqrtf(fmaxf(0, R*R - (bottomY-R)*(bottomY-R))) - 4;   // circle-safe half width at the box bottom
  int textW = (int)(2*hwMax - 2*padX);
  char lines[8][64]; int nl = wrapGfx(panelText.c_str(), textW, lines, 8);
  int widest = 0; int16_t x1, y1; uint16_t w, h;                    // box width from the widest wrapped line
  for (int i = 0; i < nl; i++) { canvas->getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h); widest = max(widest, (int)w); }
  int bw = min((int)(2*hwMax), widest + 2*padX), bh = nl*lh + 2*pad;
  int bx = (W - bw) / 2, by = bottomY - bh;
  boxRX = bx; boxRY = by; boxRW = bw; boxRH = bh;
  canvas->setFont();   // measuring done; the cat/counter sections expect the built-in font

  // ---- cat + hearts + tap mailbox ----
  float hopE = (float)(now - bounceAt) * 0.45f;
  int hop = (int)(-fmaxf(0, 1 - hopE / 150.0f) * 5);
  int catDy = by - CAT_GROUND_ROW;                       // paw row on the measured box top
  selectAndDrawCat(pm, now, catDy + hop);
  catRX = CAT_DX; catRY = catDy; catRW = CAT_W; catRH = CAT_H;   // derived cell rect (hop is draw-only)

  // consume the atomic tap: read-and-clear (already screen-space, see touch.cpp), then hit-test
  uint32_t tap = gTreatTap;
  if (tap & 0x80000000u) { gTreatTap = 0;
    int tx = (int)((tap >> 12) & 0xFFF), ty = (int)(tap & 0xFFF);
    onTap(tx, ty, now);
  }

  // hearts: fade toward the sky (no true alpha); bounded wobble phase into tsin
  for (int i = nHe - 1; i >= 0; i--) { Heart& h = hearts[i]; float age = (float)(now - h.born) * 0.45f;
    if (age > 950) { hearts[i] = hearts[--nHe]; continue; }
    float a = 1 - age / 950.0f;
    float hx = h.x + h.vx * age * 0.14f + tsin(age * 0.02f + h.wob) * 3, hy = h.y + hop + h.vy * age * 0.14f;
    int sr, sg, sb; skyAt((int)fminf(fmaxf(hy,0),H-1), sr, sg, sb);
    uint16_t c = gfx->color565((int)(255*a+sr*(1-a)), (int)(111*a+sg*(1-a)), (int)(174*a+sb*(1-a)));
    pHeart((int)lroundf(hx), (int)lroundf(hy), 2, c); }

  // ---- treat counter: red heart + white count (size 2 = 12x16) ----
  pHeart(101, 4, 3, gfx->color565(255, 59, 78));
  canvas->setTextSize(2); canvas->setTextColor(gfx->color565(255,255,255));
  canvas->setCursor(123, 6); canvas->print(pet.treats());

  if (fortuneMode) { drawFortune(now); return; }   // fortune takes over the lower half

  // ---- affirmation box draw: geometry was measured above, before the cat ----
  canvas->setFont(&FreeSans9pt7b); canvas->setTextSize(1);
  battleBox(bx, by, bw, bh);
  uint16_t tc = panelText.equals(PLEA) ? gfx->color565(184,63,106) : gfx->color565(36,32,56);
  canvas->setTextColor(tc);
  for (int i = 0; i < nl; i++) { canvas->setCursor(bx + padX, by + pad + ascent + i*lh); canvas->print(lines[i]); }  // explicit left margin -> can't spill left
  canvas->setFont();   // back to the built-in font for the counter/fortune on the next frame
}
