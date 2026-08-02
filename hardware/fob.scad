// ocellus fob enclosure -- docs/superpowers/specs/2026-07-13-fob-enclosure-design.md
//
// z = 0 is the BACK face, +z toward the screen. -Y is the USB end, +Y the lug.
//
// The board cavity is NOT modelled. It is recovered from the original case
// (docs/Waveshare+1.stl), which already fits the board. Do not edit that STL.
//
// ONE PIECE, no lid. The cell drops in through the front, sits in the bay, and
// the board presses in on top and retains it. That works because the board rests
// on two pads (the GPIO pad the original has, plus the USB pad added here), NOT
// on the original's floor -- which is what made it tilt in the first place. No
// lid means no lid seat, and no lid seat means no ledge eating the wall.
//
// The pocket is OVAL but the front REVEAL (the round screen opening, recovered
// from the original) is a near-perfect circle -- r 19.25, offset reveal_dy +Y of
// origin (toward the lug), Kasa-fit off the exported face at rms 1um. The outer
// skin is ROUND and centred ON that reveal (radius outer_rr), so the front bezel
// is a UNIFORM ring -- the whole point of the round shell. Centring the round skin
// on the ORIGIN instead (the first cut at this) leaves the bezel fat at -Y and
// thin at the lug, because the reveal is not origin-centred. Deliberate added
// material: the skin grows out at the lug and the sides to carry the -Y bezel
// width all the way around; the -Y edge itself stays put. (An old freehand round
// at Ø48 ballooned the side wall to 4.5mm; sizing off the reveal keeps the growth
// to exactly what the uniform bezel needs.)

part = "body";          // body | coupon | button | button-coupon

/* ---- the cell. CHANGE THIS AND THE CASE RESIZES ITSELF. ----
   702030 (30x20x7) is the default: it front-loads through the pocket with room
   to spare. (Pass-through is set by X/Y only, so 602030's 6mm fits the same
   opening -- Z just sets bay depth / case thickness. Switched off the 602030 to
   the 702030: same connector for ~half the price.) The 602530 (30x25x6) does
   NOT front-load -- see the assert below. */
cell         = [30, 20, 7];    // bare 702030: X = length, Y = width, Z = thickness
cell_clear   = 0.5;     // per side. The symmetric ceiling is 0.55: above it the bay's
                        // +Y wall grows into the slider corridor (assert 216). 0.5
                        // keeps 0.05 off that pinch while forgiving unit-to-unit size
                        // variance.
pack_len     = 1.6;     // the PCM + kapton at the +X short end runs past the bare cell;
                        // add it to the bay's X LENGTH (not width) so the packaged end
                        // drops in. 1.6 = measured worst of 5 (31.6mm long) minus the 30
                        // bare, so bay = 32.6 clears the worst by 1.0mm (0.5/side). It
                        // lengthens the bay corner, driving the round skin out via
                        // bay_r -> outer_a (~0.9mm on the diameter vs bare, auto-derived).
bay_corner   = 0.8;     // capped at cell_clear * 2.414 -- LiPos have sharp corners

/* ---- wire relief ----
   The cell's leads exit the +X/-Y CORNER of the cell and route up to the MX1.25
   header. The notch runs the bay's -Y wall out (in -Y) from an inboard edge all the
   way to the +X corner, so the corner where the cable actually comes off is open and
   the sealed tab + wires are not crushed -- it absorbs cell-length variance too.
   -Y is the ONE free direction: the +Y sliders cap symmetric cell_clear at 0.55/side,
   so growing the bay symmetrically is out. A FULL-WIDTH -Y extension is also out --
   the -X corner would reach the round skin. Keeping the notch to the +X half stays
   inside the skin (assert below). Its far +X corner leaves ~1.8mm of skin wall at
   wire_relief 3.5; the assert fires before that thins below min_wall. */
wire_w       = 10;      // inboard (-X) coverage of the notch; +X edge runs to the corner
wire_relief  = 3.5;     // how far past the bay -Y wall the notch runs

/* ---- walls & fillets ---- */
min_wall     = 0.8;
fillet_front = 1.0;     // bezel edge: bounded by the wall
fillet_back  = 1.5;     // the face your hand touches

/* ---- board pocket (from the original -- do not invent these) ---- */
pocket_h     = 11.10;   // the original case's full height
plate_h      = 1.20;    // its back plate, which we cut away so the cell can pass
pocket_a     = 19.45;   // the board cavity, oval semi-axes
pocket_b     = 20.60;
// The original's outer surface is CONCAVE at the USB boss shoulders, so the
// convex hull bridges those hollows and hull()-minus-part picks up a phantom
// shell of material (<=0.46mm thick, r 21.06..21.98, off at x = +-6..8 -- NOT on
// the -Y axis, where the hull touches the boss face). Subtract that into the body
// and it carves thin internal voids inside the wall. This envelope clips them.
// expect.py guards it: "wall at USB shoulder".
envelope_x   = 41.8;
envelope_y   = 42.2;

/* ---- USB-C slot (measured off the original) + the seat fix ----
   NOTE usb_z0 and usb_pad_z are measured FROM THE POCKET FLOOR, not from z=0.
   The slot is the original's opening exactly: bisecting its mesh gives the
   opening at x +-4.70, z 1.40..4.90. Do not "correct" these. */
usb_w        = 9.40;
usb_hi       = 3.50;
usb_z0       = 1.40;
// = 1.40. The connector's UNDERSIDE -- the pad rises to meet it. Started at plate_h
// (1.20, the original's floor, which clears the connector by 0.2); the printed
// coupon seated on that and asked for the 0.2 back, which lands it exactly on
// usb_z0. That is not a coincidence: usb_z0 IS the connector's underside, so the
// slot floor and the pad top are the same plane. Do NOT raise it further -- see
// usb_pad() for what is 1.1mm above here and why it stops the board going in.
usb_pad_z    = usb_z0;
usb_pad_arc  = 60;      // degrees of arc, centred on -Y
usb_pad_w    = 2.5;     // radial width of the pad, inward from the pocket wall

/* ---- pry hole: the only way back out of a one-piece case ---- */
pry_d        = 3.0;
pry_y        = 16.5;    // +Y, past the bay, under the pad the board rests on

/* ---- BOOT / RESET: drop-in slab sliders ----
   Switch centres circle-fitted off the Waveshare drawing (docs/ESP32-S3-Touch-
   LCD-1.28-details-size-2.jpg): ~3.1 x 2.3mm at (+-11.5, +11.1), faces ~1.0mm
   above the recess floor. A finger-sized captive plunger straight above them
   reaches the cell, so the button goes AROUND: a flat slab slider whose press
   boss sits at (+-8.2, 13.8) -- lugward, where the case is solid full-depth --
   with a nub cantilevered back over the switch above the bay's z-band.

   A SLAB, not a piston: a slab in a slot cannot rotate, so the off-axis nub
   stays over the switch. The cavity is the slider's plan silhouette + clearance,
   one prism from the skin top up through the board-seat pad: that is what lets
   the slider DROP IN from the open pocket before the board goes in -- the board
   then locks it captive (the O6 foot cannot pass the O5 skin opening). Travel
   is stopped by the switch bottoming out; its spring is the return.

   cap at (8.2, 13.8) and not further out or lower: the pocket-oval inset
   assert below binds on the pocket's diagonal, and the foot circle must clear
   the BAY VOLUME (y 10.4) -- any cavity wall inside the bay band crosses the
   coincident bay-top/plate-top plane at z=8.5 and CGAL emits non-manifold
   membrane faces there (found the hard way; assert 2 pins it). Inboard is
   bounded by the pry hole. */
sw_x         = 11.5;    // switch centre
sw_y         = 11.1;
sw_face      = 1.0;     // switch face above the recess floor -- measured once,
                        // the button coupon is what verifies it against reality
cap_x        = 8.2;     // press boss centre
cap_y        = 13.8;
skin_h       = 1.0;     // back skin under the slider
open_d       = 5.0;     // press opening in the skin
boss_d       = 4.7;     // boss through it
boss_proud   = 0.7;     // boss stands proud of the back face -- this IS the max
                        // possible protrusion (foot fully seated). Was 1.5; most of
                        // that was slop, so 0.8 came off. With the stiff contact arm
                        // (below) the reach is now MODELLED, not drooped: the pad rests
                        // pretravel below the switch, so a press engages ~0.2mm up from
                        // foot-seated (~0.5 proud) with a short clean travel -- no
                        // reliance on print droop. Pocket-presses are benign: BOOT is
                        // just the user button while running; download mode needs BOOT
                        // held at power-on. Verify feel on the button-coupon.
foot_d       = 6.0;     // captive foot: 0.5/side engagement over the opening
foot_h       = 1.2;
slab_t       = 3.0;     // slider body thickness
nub_d        = 2.2;     // the contact pad: O2.2 flat, over the switch's 2.5x3 plunger.
                        // The plunger clicks off-centre, so board-to-board wander (the
                        // PCB floats vs the screen the case registers to) is forgiven
                        // as long as the pad stays over the plunger and off its fixed
                        // rim. Bigger buys area, not tolerance -- the stiff arm buys
                        // tolerance by landing the pad where the model puts it.
pretravel    = 0.2;     // contact-pad top rests this far below the switch face at rest
shaft_ext    = 0.5;     // extra inner-shaft length: lifts the whole arm+pad this much
                        // toward the switch. Dialed on real prints -- a full printed body
                        // at 0.2 (pad modelled right at the switch face) still read a hair
                        // short, so it's up to 0.5. That puts the MODELLED pad ~0.3 above
                        // the switch face; it works because real prints under-reach by about
                        // that much (foot not fully seating, pad/print height, arm a touch
                        // shy). NOTE this is BOOT: the self-press margin is now the thing to
                        // watch across print batches/boards -- if a unit wakes in download
                        // mode at rest, back shaft_ext off.
slide_clear  = 0.2;     // per side, slider vs cavity. Tightened from 0.3: the cavity
                        // tracks slab_t, so a fatter slab widens the hole to match -- the
                        // play IS this gap. Less play -> the slab stays centred in the
                        // hole so the pad stays over the switch. Too tight binds; coupon
                        // is the gate.

/* ---- keychain lug: +Y, opposite the USB port ---- */
lug_hole_d   = 5.0;
lug_r        = 4.5;     // -> a 2.0mm web around the hole
lug_out      = 3.0;     // how far the hole centre sits beyond the body
lug_t        = 6.0;

/* ---- back & bay ---- */
back_h       = 2.0;     // solid back. the back fillet lives here.

$fn = 180;

/* ================= derived ================= */
bay      = [cell[0] + pack_len + 2*cell_clear, cell[1] + 2*cell_clear];
bay_h    = cell[2] + 0.5;
bay_r    = sqrt(pow(bay[0]/2, 2) + pow(bay[1]/2, 2));   // corner reach

// The bay's TOP replaces the original's back plate, so the cell can pass through
// the pocket into the bay. The board then sits 1.3mm above it, on its pads.
pocket_z = back_h + bay_h - plate_h;
total_h  = pocket_z + pocket_h;

/* ---- module retention: rim supports (anti-rock) + crush ribs (anti-spin) ----
   The board seats on the -Y usb pad and the +Y GPIO shelf -- BOTH on the Y axis,
   so at +-X it is unsupported and pressing a side see-saws about that line (the
   cavity floor drops ~1.3mm below the PCB rest plane there). And the pocket is a
   bare friction fit on an undersize, unit-varying module, so it also spins.
   rim_support() pads each +-X edge up to the rest plane -> 4-point base;
   crush_rib() adds thin ribs the module compresses on insertion, taking up each
   unit's lateral gap. Both UNIONed after the board_void cut, like usb_pad. Derived
   (needs pocket_z), so a future cell-thickness change carries them along. All of
   it is tuned on the coupon before the batch. */
board_rest = pocket_z + 2.5;      // 10.8 -- PCB rest plane (2.5 above pocket floor)
rim_x      = 18.0;    // +-X pad centre (outer edge merges into the ~19.3 wall)
rim_w      = 3.0;     // pad X size
rim_l      = 6.0;     // pad Y size (Y-spread resists the see-saw)
rim_drop   = 0.1;     // pad top this far below board_rest, so a proud print can't tilt
                      // the board onto it: the +-Y seats keep defining rest; the pad
                      // only catches the see-saw. 0 = true coplanar 4-point.
wall_in    = 19.3;    // pocket wall inner face at +-X, y=0 (measured off body.stl)
rib_inter  = 0.40;    // crush interference: rib tip inboard of wall_in. Two ribs take up
                      // ~2x this of the pocket-to-board gap (measured ~1mm on the coupon).
                      // Undersize modules crush less; the tilt-in eases seating. PLA shaves
                      // more than it squishes -- back off if it won't seat, up if it spins.
rib_w      = 0.9;     // rib footprint along the wall (triangular section -> crushes)
rib_dy     = 3.0;     // two ribs per side at +-rib_dy: resists rotation, not just centres
rib_z0     = pocket_z + 3.7;   // ~12 -- rib z-band: the module's glass edge, clear of
rib_z1     = pocket_z + 8.0;   // ~16 -- the seat below AND bezel.py's z=17.8 reveal
                               // slice above (ribs poking into it read as a non-round
                               // reveal and fail the uniform-bezel check)

/* ---- slider derived ---- */
floor_z    = pocket_z + plate_h;                 // 8.5 -- pad plane / recess floor
btn_len    = norm([sw_x - cap_x, sw_y - cap_y]);
btn_dir    = [(sw_x - cap_x) / btn_len, (sw_y - cap_y) / btn_len];
// the slab stops where its slot edge lands 0.1 above the bay wall; the nub
// ledge covers the rest, above z = floor_z where the bay no longer exists
slab_end_y = bay[1]/2 + slide_clear + 0.1 + slab_t/2;
btn_end    = [cap_x + btn_dir[0] * (cap_y - slab_end_y) / -btn_dir[1], slab_end_y];

// Outer: offset the pocket by (fillet + min_wall); grow it if the bay corner
// needs more. The bay bound is a conservative circular one -- always safe.
outer_a  = max(pocket_a + fillet_front + min_wall, bay_r + fillet_back + min_wall);
outer_b  = outer_a * (pocket_b + fillet_front + min_wall)
                   / (pocket_a + fillet_front + min_wall);
// Round skin, concentric with the round reveal. reveal_dy is the reveal centre's
// +Y offset (measured, see the header). outer_rr keeps the -Y edge exactly where
// the oval had it (outer_r + reveal_dy out from the reveal centre), so the widest
// current bezel -- the -Y/USB one -- is carried uniformly all round.
reveal_dy = 0.93;
outer_r   = max(outer_a, outer_b);        // the oval's long offset (unshifted)
outer_rr  = outer_r + reveal_dy;          // round skin radius, about the reveal centre
outer_x  = 2 * outer_rr;
outer_y  = 2 * outer_rr;

body_top  = reveal_dy + outer_rr;         // +Y outer edge, after the shift
lug_reach = body_top + lug_out;

// A LiPo is a prism with sharp corners; a bigger arc cuts into where the corner
// has to go and the cell will not seat.
assert(bay_corner <= cell_clear * 2.414,
       "bay_corner too large for cell_clear -- the cell's sharp corner will not fit");

// ONE PIECE means the cell enters through the board pocket. If it cannot pass,
// it cannot be installed -- and forcing a LiPo pouch through a too-tight opening
// punctures it. Fitting a bigger cell needs a lid, i.e. a different design.
assert(pow((bay[0]/2) / pocket_a, 2) + pow((bay[1]/2) / pocket_b, 2) <= 1.0,
       "cell cannot pass through the board pocket -- it cannot be front-loaded. \
Either use a smaller cell, or go back to a two-piece case with a lid.");

// The wire notch's far corner is at the +X bay edge, wire_relief deep. It must stay
// inside the round skin (else the shell balloons -- see the note at wire_relief) with
// a wall left, and clear the -Y USB seat pad's inner edge (else it undercuts what the
// board rests on). reveal_dy is the skin centre.
assert(norm([bay[0]/2, bay[1]/2 + wire_relief + reveal_dy]) <= outer_rr - min_wall,
       "wire notch corner reaches the round skin -- shorten wire_relief or pull it off the +X corner");
assert(bay[1]/2 + wire_relief <= envelope_y/2 - usb_pad_w - 1.0,
       "wire notch undercuts the -Y USB seat pad -- shorten wire_relief");

// Extreme points for assert 1. The foot family bounds the actual cavity; the
// nub family is kept in the sweep as the slider's own reach over the switch
// (not cut geometry -- the recess above the pad is native void).
btn_extreme = concat(
    [for (a = [0:15:345]) [cap_x + (foot_d/2 + slide_clear) * cos(a),
                           cap_y + (foot_d/2 + slide_clear) * sin(a)]],
    [for (a = [0:15:345]) [sw_x + (nub_d/2 + slide_clear) * cos(a),
                           sw_y + (nub_d/2 + slide_clear) * sin(a)]]);
// 1. inside the pocket oval, inset 0.8: the inset IS the surviving pad ring the
//    board seats on, and inside the oval is what makes drop-in possible at all
assert(max([for (p = btn_extreme)
        pow(p[0] / (pocket_a - 0.8), 2) + pow(p[1] / (pocket_b - 0.8), 2)]) <= 1,
       "slider cavity breaks out of the pocket oval (or eats the pad ring) -- pull cap_x/cap_y inboard");
// 2. the cavity must clear the BAY VOLUME entirely -- not merely the cell. A
//    cavity wall inside the bay band crosses the coincident bay-top/plate-top
//    plane at z=8.5 and CGAL emits non-manifold membrane faces there. The
//    0.05 keeps the comparison off exact float equality.
assert(cap_y - foot_d/2 - slide_clear >= bay[1]/2 + 0.05,
       "slider cavity enters the bay -- raise cap_y (non-manifold pinch at z=8.5)");
// 4. captive engagement and boss ring gap
assert((foot_d - open_d)/2 >= 0.5 && (open_d - boss_d)/2 >= 0.1,
       "foot/opening/boss stack-up broken -- the slider falls out or jams");
// 6. stay clear of the pry hole
assert(cap_x - foot_d/2 - slide_clear >= pry_d/2 + 2.0,
       "slider cavity too close to the pry hole");

/* ================= geometry ================= */

module orig()
    translate([-128, -128, 0])
        import("../docs/Waveshare+1.stl", convexity = 10);

module envelope()
    resize([envelope_x, envelope_y, total_h + 2])
        cylinder(d = 42, h = total_h + 2);

// The validated board cavity, verbatim from the original case.
module board_void()
    intersection() {
        difference() { hull() orig(); orig(); }
        translate([0, 0, -1]) envelope();
    }

// Outer profile: a rectangle with both outer corners rounded, at different radii.
// hull() of two circles plus a sliver on the axis gives exactly that. Radial
// reference is outer_rr, so the revolve below is a true circle.
module body_profile()
    hull() {
        translate([outer_rr - fillet_back,  fillet_back])            circle(r = fillet_back);
        translate([outer_rr - fillet_front, total_h - fillet_front]) circle(r = fillet_front);
        square([0.01, total_h]);
    }

// Revolve it, then shift +Y onto the reveal centre: the skin is a circle of radius
// outer_rr CONCENTRIC with the round reveal, so the front bezel is a uniform ring
// even though the pocket underneath is oval and off-centre.
module body()
    translate([0, reveal_dy, 0])
        rotate_extrude() body_profile();

module body_xy()
    translate([0, reveal_dy]) circle(r = outer_rr - 1);

// Open from the back plate up INTO the pocket: that is how the cell gets in.
module bay()
    translate([0, 0, back_h])
        linear_extrude(bay_h)
            offset(r = bay_corner)
                square([bay[0] - 2*bay_corner, bay[1] - 2*bay_corner], center = true);

// A slot off the bay's -Y wall for the lead tab + wires, running from -wire_w/2 out
// to the +X bay corner (bay[0]/2) -- the corner the cable exits. Same z-band as the
// bay, so the solid back below and the board seat above are untouched. Overlaps 1mm
// into the bay (+Y) so the two voids merge cleanly (no coincident-wall membrane).
module wire_relief_void()
    translate([0, 0, back_h])
        linear_extrude(bay_h)
            translate([-wire_w/2, -(bay[1]/2 + wire_relief)])
                square([bay[0]/2 + wire_w/2, wire_relief + 1]);

// The original's slot stops at the ORIGINAL's outer surface, so it will not reach
// daylight through the new wall. Re-cut it, running well past the outer.
//
// A WINDOW, not a notch to the rim -- the wall above it stays. The board does not
// descend straight down onto its seat: it goes in USB end FIRST, tilted, so the
// connector enters this window sideways, then the lug end swings down. That is how
// the original does it and it front-loads too. Opening this to the rim (so the
// board can be pressed straight in) leaves the outer skin either side of the
// opening standing as two ~1mm fins, and they snap. Do not.
// A STADIUM, not a rectangle: the shape of the plug. Fully rounded ends, r = usb_hi/2
// -- which is what the original has too (its opening narrows to x +-3 down at z 1.5
// while running +-4 through the middle; the square re-cut threw that away). The
// receptacle's own outline (~8.94 x 3.16, r ~1.58) sits strictly inside this, so
// rounding takes away nothing the connector needs.
module usb_slot()
    translate([0, 0, pocket_z + usb_z0 + usb_hi/2])
        rotate([90, 0, 0])
            linear_extrude(outer_y)
                hull() {
                    translate([-(usb_w - usb_hi)/2, 0]) circle(d = usb_hi);
                    translate([ (usb_w - usb_hi)/2, 0]) circle(d = usb_hi);
                }

// One piece, friction fit, nothing to grab: without this the board is in there for
// good. The hole runs from the back face up THROUGH the GPIO pad to the cavity, so
// a pin lands on the PCB itself and lifts the lug end -- the tilt-in, reversed. The
// pad is solid from y~11 out to the wall (probed off the original), so the pin
// meets bare board, not a connector, and the pad loses nothing that matters.
module pry_hole()
    translate([0, pry_y, -1])
        cylinder(d = pry_d, h = pocket_z + 5);

// One slider's footprints, +X side; mirror([1,0,0]) makes the BOOT side.
// g inflates for the cavity; g = 0 is the slider itself.
module btn_plan(g)          // the slab, cap end to slab end
    hull() {
        translate([cap_x, cap_y]) circle(d = slab_t + 2*g);
        translate(btn_end)        circle(d = slab_t + 2*g);
    }
module btn_ledge_plan(g)    // the contact arm: slab end tapering to the O2.2 pad
    hull() {
        translate(btn_end)      circle(d = slab_t + 2*g);
        translate([sw_x, sw_y]) circle(d = nub_d + 2*g);
    }
// The cavity: O5 press opening through the skin, then ONE prism -- foot circle
// plus slab slot -- from the skin top up PAST the pad/shelf (to floor_z + 2),
// so the shaft is open the whole way and the slider can descend into it. The
// ledge/nub plan is NOT cut: above the pad plane the switch recess is native
// void, and cutting it full-depth is what put cavity walls inside the bay band
// (non-manifold membranes at the coincident z=8.5 plane).
//
// The slot and foot edges land at y 10.5 -- 0.1mm from the bay wall at 10.4.
// That sliver is BELOW one nozzle width: slicers will either cull it (small
// windows into the bay -- harmless, the cell tops out at y 10.0) or print a
// fragile fin. Check for fin debris in the slot on the button coupon before
// blaming a sticky slider on anything else. The corridor is genuinely pinned:
// cap_y cannot rise (assert 1 fails at 14.1) without pulling cap_x inboard.
module btn_cavity() {
    translate([cap_x, cap_y, -1]) cylinder(d = open_d, h = skin_h + 1.01);
    translate([0, 0, skin_h]) linear_extrude(floor_z + 2.0 - skin_h) {
        translate([cap_x, cap_y]) circle(d = foot_d + 2*slide_clear);
        btn_plan(slide_clear);
    }
}

// The loose part. Boss-down at its live XY: prints as-is (tiny footprint --
// use a brim), and the layout doubles as the assembly diagram. The slab
// overhangs the foot edge by ~0.9mm at z 2.2; at one layer that bridges fine.
// The contact arm rides 0.1 ABOVE the pad plane: the plate under it is solid
// (no channel is cut there), so the foot -- not the arm -- defines the rest
// position, and the seated arm hangs with 0.1 clearance.
//
// Boss-down is deliberate: the spec's lie-on-its-side idea cannot work as
// modelled (foot r 3.0 > slab half-thickness 1.5 -- it would rest on a knife
// edge). The old cost -- a ~0.5mm-thin cantilever carrying a thin nub, drooping
// into the pretravel -- is why the arm is now a full-height (~0.7mm) solid slab:
// stiff enough not to droop, so the pad's rest height is the modelled one. The
// button coupon's click test is still the gate; a fouled or mushy click shows
// there, not in the assembled fob.
module btn_slider() {
    translate([cap_x, cap_y, -boss_proud])
        cylinder(d = boss_d, h = boss_proud + skin_h + 0.01);
    translate([cap_x, cap_y, skin_h]) cylinder(d = foot_d, h = foot_h);
    translate([0, 0, skin_h + foot_h])
        linear_extrude(floor_z + 0.1 + shaft_ext - (skin_h + foot_h)) btn_plan(0);
    // The reach arm IS the contact: a thick (stiff) tapered slab from the slab end
    // over to the switch, its flat top the O2.2 pad that presses the plunger. It
    // fills the whole floor_z+0.1 -> (switch face - pretravel) budget, so it cannot
    // droop like the old 0.5mm flap + thin nub did -- the pad lands where the model
    // puts it, which is what forgives board-to-board switch wander. shaft_ext lifts
    // its base (and the whole slab below) that much closer to the switch.
    translate([0, 0, floor_z + 0.1 + shaft_ext])
        linear_extrude(sw_face - pretravel - 0.1) btn_ledge_plan(0);
}

// THE FIX, and it is a BACKSTOP, not a shelf. The board rests on the GPIO pad at
// +Y (solid from y~11 out to the wall). At -Y it rests on nothing: what stops the
// USB end sagging in the original is the back plate, 1.2mm, and we cut that away so
// the cell can pass. This restores it as a rim at -Y only, at its original height.
//
// It must NOT be raised to the PCB's seating plane (~2.5). The USB-C is MID-MOUNT:
// its body hangs ~1.1mm BELOW the PCB, down to z = usb_z0 = 1.4. A pad at 2.5 is
// 1.1mm up inside the connector, and the board will not go in -- the port fouls the
// pad at the insertion angle. Printed coupon proved it. 1.2 leaves the connector
// 0.2mm of air, exactly as the original does.
// A RIM hugging the pocket wall at -Y -- not a plate across the pocket.
module usb_pad()
    intersection() {
        translate([0, 0, pocket_z]) linear_extrude(usb_pad_z) circle(r = 21);
        translate([0, 0, pocket_z]) linear_extrude(usb_pad_z)
            polygon([
                [0, 0],
                [30 * cos(270 - usb_pad_arc/2), 30 * sin(270 - usb_pad_arc/2)],
                [30 * cos(270 + usb_pad_arc/2), 30 * sin(270 + usb_pad_arc/2)],
            ]);
        difference() {
            translate([0, 0, pocket_z - 1]) envelope();
            translate([0, 0, pocket_z - 2])
                resize([envelope_x - 2*usb_pad_w, envelope_y - 2*usb_pad_w, total_h + 4])
                    cylinder(d = 42, h = total_h + 4);
        }
    }

// Anti-rock. A pad under each +-X module edge, from the pocket floor up to the PCB
// rest plane, so the board seats on FOUR points instead of the two -Y/+Y seats it
// see-saws on. Its outer edge runs into the wall (merges, stiffer); clipped to the
// envelope so it cannot bulge the skin. UNIONed after board_void, like usb_pad.
module rim_support()
    intersection() {
        for (s = [-1, 1])
            translate([s * rim_x, 0, pocket_z])
                linear_extrude(board_rest - rim_drop - pocket_z)
                    offset(r = 0.8) square([rim_w - 1.6, rim_l - 1.6], center = true);
        translate([0, 0, pocket_z - 1]) envelope();
    }

// Anti-spin. Two triangular ribs per side on the +-X pocket wall, tip rib_inter
// inboard of the wall, over the module's glass edge. The undersize/varying module
// shaves or compresses them on the USB-first tilt-in, taking up its own gap so it
// cannot rotate. Triangular so it crushes rather than blocks; y=0 stays clear.
module crush_rib()
    for (s = [-1, 1], dy = [-rib_dy, rib_dy])
        translate([0, 0, rib_z0])
            linear_extrude(rib_z1 - rib_z0)
                polygon([
                    [s * wall_in,             dy - rib_w/2],
                    [s * wall_in,             dy + rib_w/2],
                    [s * (wall_in - rib_inter), dy],
                ]);

// A radial ear. Prints in-plane with the disc -- no supports -- and a lanyard
// loads it ALONG the layer lines, the strong direction.
module lug()
    translate([0, 0, (total_h - lug_t) / 2])
        linear_extrude(lug_t)
            hull() {
                translate([0, lug_reach]) circle(r = lug_r);
                body_xy();
            }

module fob()
    // the pad is a UNION, added AFTER the void is subtracted or board_void() eats
    // it. The lug hole is cut at the top level so nothing can fill it back in.
    union() {
        difference() {
            union() { body(); lug(); }
            translate([0, 0, pocket_z]) board_void();
            bay();
            wire_relief_void();
            usb_slot();
            pry_hole();
            btn_cavity();
            mirror([1, 0, 0]) btn_cavity();
            translate([0, lug_reach, -1]) cylinder(d = lug_hole_d, h = total_h + 2);
        }
        usb_pad();
        rim_support();
        crush_rib();
    }

if (part == "body")
    fob();
else if (part == "coupon")
    // The FULL pocket: floor (with both seat pads) up to the bezel rim. The rim is
    // the whole point -- without it there is nothing to judge "flush" against, and
    // the coupon cannot do its one job. Drops only the bay and the solid back.
    //
    // Cut at pocket_z, NOT pocket_z + plate_h: the USB backstop's top IS plate_h, so
    // cutting there shaves off the entire pad and the coupon silently stops testing
    // the one thing it is for.
    intersection() {
        fob();
        translate([-40, -40, pocket_z])
            cube([80, 80, total_h - pocket_z + 1]);
    }
else if (part == "button") {
    btn_slider();
    mirror([1, 0, 0]) btn_slider();
}
else if (part == "button-coupon")
    // The +X button's neighborhood, cut from the REAL body -- skin, opening,
    // shaft, wedge, pad, and the pocket above it, so a slider can be dropped
    // in and clicked against a bare board resting on the pad. Full height on
    // purpose: the rim is what the board registers against.
    intersection() {
        fob();
        translate([cap_x - 8, 9, -1]) cube([16, 12, total_h + 2]);
    }
