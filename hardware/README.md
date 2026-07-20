# ocellus fob enclosure

Parametric OpenSCAD case for the Waveshare ESP32-S3-Touch-LCD-1.28.
**One piece, round Ø46.7 × 18.4 mm.** The skin is a circle centred on the round
screen reveal (which sits 0.93 mm toward the lug), so the front bezel is a **uniform
~4.1 mm ring**. The pocket underneath is oval, so the wall varies (~1.8 mm at the
−Y/USB end up to ~4 mm at the lug) — that's the deliberate cost of the even bezel.
~6 mm LiPo bay, rounded edges, keychain lug.

For reference: the original case is 41.1 × 43.0 × 11.1 with a ~1.1 mm wall.


## Build

    hardware/build.sh coupon    # the fit coupon -- print THIS first
    hardware/build.sh body

Each exports to `out/`, renders two PNGs, and runs the geometry checks in
`expect.py`.

## Print the coupon first

The coupon is the **full pocket** (z 7.3–18.4): floor, the USB backstop, *and the
bezel rim*. The rim is the point — without it there is nothing to judge "flush"
against. Tilt the board in, look at the bezel, adjust, rebuild, reprint. It drops
the bay and the solid back, so it is a fraction of the print.

It cuts at `pocket_z`, **not** `pocket_z + plate_h`: the backstop's top *is* the old
floor plane, so cutting at the floor shaves the pad clean off and the coupon
silently stops testing the one thing it is for.

## How it goes together

One piece. **The cell drops in through the front**, sits in the bay, and the board
presses in on top and retains it.

The board rests on the **GPIO pad** — the broad shelf the original has at +Y, solid
from y ≈ 11 out to the wall. At the USB end it rests on nothing at all: what stops
that end sagging in the original is the **1.2 mm back plate**, and cutting that plate
away is precisely what lets the cell pass. So the plate comes back as a rim at −Y
only (`usb_pad_z = plate_h`).

**That rim is a backstop, not a shelf, and the difference is the whole game.** The
USB‑C is *mid‑mount*: its body hangs ~1.1 mm below the PCB, down to `usb_z0` = 1.4.
Raise the rim to the PCB's own seating plane (2.5, which looks right if you think of
it as a second pad) and it stands 1.1 mm up inside the connector — the port fouls it
at the insertion angle and the board simply will not go in. A printed coupon proved
that.

`usb_pad_z = usb_z0` = **1.4**: the rim rises to meet the connector's underside. It
started at `plate_h` (1.2, the original's floor, which clears the connector by 0.2);
the board seated on that, and closing the last 0.2 mm lands it exactly on `usb_z0` —
not a coincidence, since `usb_z0` *is* the connector's underside. The slot floor and
the pad top are the same plane.

## The USB window

A **stadium** — the shape of the plug, fully rounded ends, r = `usb_hi`/2. The
original is rounded too (its opening narrows to x ±3 down at z 1.5 while running ±4
through the middle); the square re-cut simply threw that away. The receptacle's own
outline (~8.94 × 3.16, r ≈ 1.58) sits strictly inside the stadium, so the rounding
costs the connector nothing.

No lid ⇒ no lid seat ⇒ no ledge eating the wall. The lid was half the bulk of the
first design.

**The board goes in USB end first, tilted** — the connector enters its window
sideways, then the lug end swings down onto its pad. It takes a little patience. It
does *not* press straight in: the wall above the window is in the way, exactly as it
is in the original, which front-loads the same way.

## Getting the board back out

A **3 mm pry hole** through the back at y = +16.5 (`pry_d`, `pry_y`). It runs up
through the GPIO pad to the cavity, so a pin pushes on the PCB itself and lifts the
lug end — the tilt-in, reversed. The pad is solid from y ≈ 11 out to the wall, so
the pin meets bare board rather than a connector.

Without it the board is in there for good: one piece, friction fit, nothing to grab.

## BOOT / RESET sliders

BOOT is `BUTTON_PIN` on this board — the toy's only user input — so it gets a real
button, not a pin hole. Two loose printed sliders (`part = "button"`, both in one
plate, boss-down, brim recommended) drop into silhouette-shaped wells in the body
**before the board goes in**; the board locks them captive. Press the Ø4.7 boss —
0.7 mm proud of the back face — at (±8.2, 13.8): a stiff arm reaches over each switch
centre (±11.5, 11.1) and its flat Ø2.2 pad presses the plunger; the switch's own
spring returns it. The Ø6 foot cannot pass the Ø5 skin opening — that is the captive
stop. After a pry-out the sliders lift straight out.

The boss proudness IS the max possible protrusion (foot fully seated); it was 1.5 mm,
trimmed to 0.7 once the printed unit showed most of it was dead travel. Reach is now
geometry, not luck: the old thin ledge + thin nub was a ~0.5 mm cantilever that
drooped into the pretravel, so an earlier revision over-reached the nub to compensate
for the droop. Instead the ledge and nub are merged into one full-height stiff slab
ending in the pad — it doesn't droop, so the pad lands where the model puts it. A
`shaft_ext` (0.5 mm, dialed in on real prints) lifts the whole arm toward the switch,
still capped at 0.7 mm protrusion. At 0.5 the *modeled* pad sits ~0.3 mm past the switch
face — it works because real prints under-reach by about that much (foot seating, print
height). (This is BOOT — the self-press margin is now what bites first: if a print/board
seats high enough to hold BOOT down at rest, that unit wakes in download mode every
power-on; back `shaft_ext` off if so.)

The broad flat pad + stiff arm also buy board-to-board tolerance: the Waveshare PCB
floats relative to the screen the case registers to, so the switch wanders. The pad
lands where the model puts it (stiff arm) and the plunger clicks off-centre, so the
wander is forgiven — as long as the Ø2.2 pad stays over the 2.5×3 plunger and off its
fixed rim. Wider than the plunger would bottom on the rim; the coupon (ideally 2–3
boards) is the check. `slide_clear` was tightened 0.3 → 0.2 mm/side to keep the slab
centred in its hole (the cavity tracks `slab_t`, so the play is the clearance, not the
diameter); too tight binds, so that too is a coupon call.

Print `part = "button-coupon"` first when anything in this area changes: one
complete well cut from the real body — drop a slider in, rest a bare board on the
pad, and verify slide / click / return. The switch-face height (1.0 mm above the
recess floor) is a measured-once number; the coupon is what checks it.

A slider inserted backwards (pad outboard) sits proud of the pad and the board
will not seat — self-announcing, no keying needed.

The wells run 0.1 mm from the battery-bay wall — below one nozzle width, so the
slicer either culls that sliver (harmless windows into the bay) or prints a
fragile fin. Fin debris in the well is the first thing to check if a slider
sticks; the coupon shows it before the body print does.

Assembly order: sliders → cell → board (USB end first, tilted, as before).

## Battery

`cell = [30, 20, 7]` — a **702030**, ~450–500 mAh. **Change this one line and the
case resizes itself**; the oval outer is derived from it. (Switched off the 6 mm
**602030** to the 7 mm **702030** — same connector for roughly half the price. Only
the Z changed: pass-through is set by X/Y, so the case just gains ~1 mm of thickness.)

`pack_len = 1.6` is added to the bay's **X length** (not width) for the PCM + kapton
that runs past the bare cell at the +X short end — without it the packaged end won't
drop in. 1.6 comes from the measured worst of 5 cells (**31.6 mm** long) minus the 30
bare, giving a 32.6 mm bay that clears that worst cell by 1.0 mm (0.5/side). It
lengthens the bay corner, so `bay_r` drives the round skin out (`outer_a`): ~0.9 mm on
the overall diameter vs the bare cell. That is the price of the extra length, and it is
auto-derived — the asserts and `expect.py` bbox track it. Longer future cells: bump
`pack_len`; the case keeps resizing.

The **602530** (30×25×6, 500 mAh) will *not* build: an `assert()` refuses it,
because it cannot pass through the board pocket — it only clears bare, with ~0.5 mm
to spare, and forcing a LiPo pouch through a gap that tight is how you puncture a
cell. Fitting it needs a two-piece case with a lid, and the lid brings the bulk
back. That is a real fork, not an oversight.

The battery lead reaches the MX1.25 header straight up through the bay opening. A
**wire notch** (`wire_w` × `wire_relief`) extends the bay in −Y and runs out to the
**+X/−Y corner** — where the leads come off the cell — so the sealed tab and wires
seat without being crushed, and it soaks up cell-length variance too. −Y is the *only*
free direction: the +Y BOOT/RESET sliders cap symmetric `cell_clear` at 0.55/side
(assert 216, so it sits at **0.5**), and a full-width −Y extension would push the round
skin out — keeping the notch to the +X half stays inside it. `wire_relief` is bounded
two ways (both asserted): ~7 mm before it undercuts the −Y seat pad, and the +X corner
must keep `min_wall` of skin (~1.8 mm slack at 3.5).

## Print orientation

Print **back-face down** (as exported). The back fillet then needs no supports, and
the lug lies in-plane, so a lanyard loads it along the layer lines rather than
across them.

## Do not

- **Edit `docs/Waveshare+1.stl`.** Read-only reference input. The board cavity is
  recovered from it (`hull(orig) − orig`, clipped) precisely so the friction fit —
  already known to work on a printed part — is never re-derived and cannot regress.

- **Re-centre the round outer on the origin.** The outer skin *is* round now (by
  request — a uniform-width front bezel), but it is centred on the **round reveal**
  (r 19.25, sitting `reveal_dy` = 0.93 mm toward the lug), *not* the origin. The
  reveal is off-centre, so an origin-centred round skin gives a fat −Y bezel and a
  thin lug bezel — measured, not guessed (`bezel.py` circle-fits both boundaries off
  the exported STL and checks they're concentric). `outer_rr` = `outer_r + reveal_dy`
  keeps the −Y edge put and carries that widest bezel all the way around. An old
  freehand round at Ø48 ballooned the side wall to 4.5 mm; sizing off the reveal
  keeps growth to what the even bezel needs. If you shift/replace the display, re-run
  `bezel.py` and update `reveal_dy`.

- **Remove `envelope()`.** The original's outer surface is *concave* at the USB boss
  shoulders, so the convex hull bridges those hollows and the raw difference picks up
  a phantom shell (≤0.46 mm thick, r 21.06–21.98, off at x ≈ ±6–8 — *not* on the −Y
  axis, where the hull touches the boss face). Subtract that into the body and it
  carves thin internal voids inside the wall, invisible in any render. The
  `wall at USB shoulder` probe guards this.

- **Raise `bay_corner` above `cell_clear × 2.414`.** A LiPo has sharp corners; a
  bigger arc cuts into where the corner has to go. There is an `assert()` for it.

- **Raise `usb_pad_z` above `usb_z0`.** It is a backstop under a mid-mount connector,
  not a shelf under the board — see "How it goes together". `usb_z0` is where the
  connector's underside is; anything above that is *inside* the connector, and at 2.5
  (the PCB's seat) the board will not go in at all. The `connector space above pad`
  probe guards it.

- **"Correct" `usb_w` / `usb_hi` / `usb_z0`.** They are the original's opening
  exactly — bisecting its mesh gives x ±4.70, z 1.40–4.90. If the port does not line
  up with the window, the board is not seated; the window is not the problem. (They
  are the stadium's bounding box, not a rectangle — see "The USB window".)

- **Open the USB window up to the rim.** Tempting, because then the board presses
  straight down instead of needing the tilt — but the connector's boss also needs
  the crescent behind the wall relieved, and what is then left of the outer skin
  either side of the opening is two ~1 mm fins. They snap. The printed part proved
  it. Tilt the board in instead; the `wall above USB window` probe guards this.

- **Trust the OpenSCAD preview.** It z-fights on coincident faces and shows holes
  that do not exist. `build.sh` renders the *exported STL* for this reason.

## Traps that cost real time

- OpenSCAD only **warns** on a failed `import()` and still exits 0, cheerfully
  emitting a solid puck with no pocket at all. `build.sh` treats it as fatal.
- A failed export leaves the **previous** run's STL in place and every check then
  passes on a stale file. `build.sh` deletes the target first.
- The manifold check compares vertices **exactly**. Rounding them "for float safety"
  merges genuinely distinct vertices 1e-6 apart and reports a watertight solid as
  broken.
- Nothing above was caught by looking at renders. They were caught by probes, or by
  holding the printed part.
