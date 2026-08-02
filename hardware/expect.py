#!/usr/bin/env python3
"""Per-part geometric expectations. Run via build.sh.

Kept in step with fob.scad's derived values by hand. If a dimension below stops
matching what the .scad computes, the bbox check is the first thing that fires.
"""
import sys

import check

# --- derived, mirroring fob.scad for the default cell (702030) ---
BACK_H = 2.0
BAY_H = 7.5                     # cell[2] + 0.5  (702030: 7mm thick)
PLATE_H = 1.2
POCKET_Z = BACK_H + BAY_H - PLATE_H     # 7.3  -- the original's z=0
POCKET_H = 11.1
TOTAL_H = POCKET_Z + POCKET_H           # 18.4
FLOOR = POCKET_Z + PLATE_H              # 8.5  -- the board cavity's floor
OUTER_R = 22.86                         # bay-corner-driven now (packaged cell longer in X):
                                        # bay_r 19.39 + fillet_back 1.5 + min_wall 0.8 -> outer_a
                                        # 21.69, *b/a ratio -> outer_b/outer_r 22.86
REVEAL_DY = 0.93                        # round reveal centre offset +Y (measured, rms 1um)
OUTER_RR = OUTER_R + REVEAL_DY          # 23.33 -- round skin radius, about the reveal centre
OA = OUTER_RR                           # +-X extent of the skin
OB = OUTER_R                            # 22.40 -- the -Y skin edge (kept where the oval had it)
LUG_REACH = REVEAL_DY + OUTER_RR + 3.0  # 27.26
LUG_TIP = LUG_REACH + 4.5               # 31.76
PRY_Y = 16.5
CAP_X, CAP_Y = 8.2, 13.8                # slider press-boss centre (switches at +-11.5, +11.1)
SW_X, SW_Y = 11.5, 11.1                  # switch centres
BAY_HALF_Y = 10.5                       # the bay. The CELL is 10.0 -- fob.scad's
                                        # assert is written against the cell, because
                                        # the 0.5mm between them is air, not battery.

EXPECT = {
    "body": {
        "bbox_lo": (-OA, -OB, 0.0),
        "bbox_hi": (OA, LUG_TIP, TOTAL_H),
        "solid": [
            # THE probe that guards envelope(). Without the clip, hull()-minus-part
            # leaks a <=0.46mm shell of phantom material at the USB boss shoulders
            # and subtracts it from the wall. SOLID when clipped, void when not.
            # Off-axis on purpose: on -Y the hull touches the boss face and there
            # is no defect to find.
            ("wall at USB shoulder", (-8.0, -19.73, POCKET_Z + 7.0)),
            ("wall +X", (20.4, 0.0, 12.0)),
            # the round skin, concentric with the reveal (0, +0.93), r 23.33: these
            # two were OUTSIDE the old origin-centred round (r 22.40) and are now
            # plastic. They guard both the growth and that the skin moved onto the
            # reveal (a +X point AND a lug-side +Y point, since the shift is in Y).
            ("round side grew +X", (23.0, 0.93, 12.0)),
            ("round skin grew +Y", (0.0, 23.5, 12.0)),
            ("solid back", (0.0, 0.0, 1.0)),
            # the wall outside the bay's worst corner must survive
            ("wall at bay corner", (16.6, 11.6, 4.0)),
            # the seat pad: a rim at the pocket wall, -Y end
            ("usb seat pad", (0.0, -19.6, POCKET_Z + 1.0)),
            # the shelf the wire notch must NOT eat: solid between the notch end
            # (-14) and the seat pad. Its inner edge is what supports the pad.
            ("shelf under usb pad", (0.0, -16.0, 5.0)),
            # the outer skin wall outboard of the notch's +X corner (notch ends at
            # x=15.5) must survive -- breach it and the pocket opens to daylight.
            ("skin outboard of notch corner", (17.0, -12.0, 5.0)),
            # The wall ABOVE the USB window. Open this to the rim -- so the board
            # can be pressed straight down instead of tilted in USB-end-first --
            # and the outer skin either side of the opening is left standing as
            # two ~1mm fins. They snap off. Printed part proved it.
            ("wall above USB window", (0.0, -21.0, POCKET_Z + 7.0)),
            # the pry hole goes through the GPIO pad, not through the board's seat.
            # x = 4.0: the seat strip between the pry hole (O3 at x=0) and the
            # button well (foot circle around x=8.2) -- both are designed voids now
            ("gpio pad beside pry hole", (4.0, PRY_Y, POCKET_Z + 2.0)),
            # the skin survives where the old pin holes were -- and everywhere
            # outside the O5 press opening
            ("skin at old pin hole", (11.5, 11.5, 0.5)),
            ("skin lip below opening", (CAP_X, 10.6, 0.5)),
            # the pad ring outboard of the cavity: what the board still seats on
            ("pad ring past cavity", (CAP_X, 17.5, 7.9)),
            # the wedge between the cavity and the bay must survive. NOT nearer
            # the nub: the cap-side half of that circle is slot, cut full depth
            ("wall between slot and bay", (13.5, 10.8, 5.0)),
            # the window is a STADIUM (the plug's shape), not a rectangle. This sits
            # in the old rectangle's bottom-right corner: void when it was square,
            # solid once the ends are rounded.
            ("slot corner rounded", (4.5, -21.5, POCKET_Z + 1.55)),
            # the lug's web. Offset in X on purpose: a probe at (0, lug_reach)
            # lands inside the very hole it is meant to be testing.
            ("lug web", (3.5, LUG_REACH, TOTAL_H / 2)),
            # retention: the +-X rim-support pads (anti-rock) reach up to the PCB
            # rest plane. Original cavity is VOID here (floor drops to ~9.5); the
            # pad makes it solid to ~10.7. Probe above the floor, below the top.
            ("rim support pad +X", (18.0, 0.0, 10.3)),
            ("rim support pad -X", (-18.0, 0.0, 10.3)),
            # the crush ribs (anti-spin): tip at wall_in - rib_inter = 19.05, two per
            # side at y=+-3. A point just inboard of the wall, on the rib row, is rib.
            ("crush rib +X", (19.15, 3.0, 15.0)),
            ("crush rib -X mirror", (-19.15, -3.0, 15.0)),
        ],
        "void": [
            ("board pocket", (0.0, 0.0, 14.0)),
            ("outside", (30.0, 0.0, 9.0)),
            ("battery bay", (0.0, 0.0, 5.0)),
            # just inside the bay's worst corner (16.3, 10.5)
            ("bay corner", (15.0, 10.0, 5.0)),
            # the +X length gained for the PCM/kapton end: x 16.1 was solid at the old
            # bay[0]/2 = 16.0 (pack_len 1.0), now void at 16.3. Guards pack_len = 1.6.
            ("bay +X length", (16.1, 0.0, 5.0)),
            # the -Y wire notch: the lead tab + wires live here (bay wall at -10.5
            # runs out to -14). Void, or the wire is crushed and the cell won't seat.
            # It runs to the +X corner -- where the cable exits -- so a corner probe too.
            ("wire notch", (0.0, -12.5, 5.0)),
            ("wire notch at +X corner", (13.0, -12.0, 5.0)),
            # THE one-piece check: the original's back plate must be cut away over
            # the bay, or the cell cannot drop through the pocket into the bay --
            # and the whole no-lid design collapses.
            ("plate cut for cell", (0.0, 0.0, POCKET_Z + 0.6)),
            # the slot must reach daylight through the new wall
            ("usb slot in wall", (0.0, -21.5, POCKET_Z + 3.1)),
            # nothing above the pad, or the board cannot seat on it
            ("above usb pad", (0.0, -19.6, POCKET_Z + 4.0)),
            # THE pad probe. The USB-C is mid-mount: its body hangs below the PCB,
            # down to usb_z0 = 1.4. Raise the backstop into that band (2.5, to reach
            # the PCB's own seating plane, was the guess) and the port fouls it at
            # the insertion angle and the board will not go in. Printed coupon.
            ("connector space above pad", (0.0, -19.6, POCKET_Z + 1.6)),
            # the pry hole must run clean through the back AND through the pad --
            # stop short and it leaves a membrane, and the board is in there for good
            ("pry hole in back", (0.0, PRY_Y, 1.0)),
            ("pry hole through pad", (0.0, PRY_Y, POCKET_Z + 2.0)),
            # the slider cavities: press opening in the skin, foot room above it,
            # slab slot at mid-depth, and a clean drop-in shaft through the pad --
            # blocked shaft = the slider cannot be inserted at all
            ("press opening in skin", (CAP_X, CAP_Y, 0.5)),
            ("reset press opening", (-CAP_X, CAP_Y, 0.5)),
            ("foot room above skin", (CAP_X, CAP_Y, 1.5)),
            ("slab slot mid-depth", (9.6, 12.7, 5.0)),
            ("drop-in shaft through pad", (CAP_X, CAP_Y, FLOOR + 0.5)),
            # no channel is CUT here -- above the pad plane the switch recess is
            # native void, which is exactly what the nub operates in
            ("recess above pad at nub", (SW_X, SW_Y, FLOOR + 0.1)),
            ("lug hole", (0.0, LUG_REACH, TOTAL_H / 2)),
            # the board must still seat: nothing above the rim pad's top plane
            ("above rim pad -- board seats here", (18.0, 0.0, 11.5)),
            # y=0 between the two ribs stays open, and the ribs never reach absurdly
            # far inboard (x=18.4 = 0.9mm interference -- void for any sane rib_inter,
            # so tuning rib_inter in the 0.25-0.6 range doesn't churn this probe)
            ("cavity clear between ribs", (19.15, 0.0, 15.0)),
            ("rib does not overreach", (18.4, 3.0, 15.0)),
        ],
    },
    "coupon": {
        # the full pocket: floor up to the bezel rim. The RIM is the point.
        # Cut at POCKET_Z, not FLOOR -- the backstop's top IS the old floor plane,
        # so cutting at FLOOR shaves the pad off and the coupon tests nothing.
        "bbox_lo": (-OA, -OB, POCKET_Z),
        "bbox_hi": (OA, LUG_TIP, TOTAL_H),
        "solid": [
            ("coupon wall", (20.4, 0.0, 12.0)),
            ("coupon seat pad", (0.0, -19.6, POCKET_Z + 0.6)),
            # the retention features must survive into the coupon so it can test them
            ("coupon rim pad", (18.0, 0.0, 10.3)),
            ("coupon crush rib", (19.15, 3.0, 15.0)),
        ],
        "void": [
            ("coupon pocket", (0.0, 0.0, 14.0)),
            ("coupon usb slot", (0.0, -21.5, POCKET_Z + 3.1)),
            ("coupon connector space", (0.0, -19.6, POCKET_Z + 1.6)),
        ],
    },
    "button": {
        # both sliders, mirrored, at their live XY, boss-down on the bed.
        # z: boss -0.7..1.0 (0.7 proud of the back face -- the max protrusion),
        # foot 1.0..2.2 (both anchored to the back skin, fixed). The slab top and
        # the contact arm/pad above it ride floor_z, so they lift with the cell's
        # thickness: slab 2.2..FLOOR+0.6, contact arm FLOOR+0.6..FLOOR+1.3, ending
        # in the O2.2 pad ~0.3 past the modelled switch face (real prints under-
        # reach that much). The O2.2 pad at the switch reaches x +-12.6, y to 10.0.
        "bbox_lo": (-12.6, 10.0, -0.7),
        "bbox_hi": (12.6, 16.8, FLOOR + 1.3),
        "solid": [
            ("boss", (CAP_X, CAP_Y, 0.5)),
            ("proud boss stub", (CAP_X, CAP_Y, -0.25)),
            ("foot", (CAP_X, CAP_Y, 1.6)),
            ("slab mid", (9.6, 12.7, 5.0)),
            ("contact pad", (SW_X, SW_Y, FLOOR + 0.65)),
            ("mirrored boss", (-CAP_X, CAP_Y, 0.5)),
            ("mirrored contact pad", (-SW_X, SW_Y, FLOOR + 0.65)),
        ],
        "void": [
            # the arm floats above the slab end -- nothing at slab height
            # under the pad, that plan-area belongs to the arm only
            ("below the contact arm", (SW_X, SW_Y, 5.0)),
            ("between the sliders", (0.0, CAP_Y, 5.0)),
        ],
    },
    "button-coupon": {
        # the +X button's neighborhood, full height: skin, opening, shaft, pad.
        # The point is a slide/click/return test with a real board BEFORE a
        # 4-hour body print -- sw_face = 1.0 is a measured-once number.
        "bbox_lo": (0.2, 9.0, 0.0),
        "bbox_hi": (16.2, 21.0, TOTAL_H),
        "solid": [
            ("coupon skin lip", (CAP_X, 10.6, 0.5)),
            ("coupon skin at old pin hole", (11.5, 11.5, 0.5)),
            ("coupon pad ring", (CAP_X, 17.5, 7.9)),
        ],
        "void": [
            ("coupon press opening", (CAP_X, CAP_Y, 0.5)),
            ("coupon drop-in shaft", (CAP_X, CAP_Y, FLOOR + 0.5)),
            ("coupon recess at nub", (SW_X, SW_Y, FLOOR + 0.1)),
        ],
    },
}


def main():
    part = sys.argv[1] if len(sys.argv) > 1 else "body"
    if part not in EXPECT:
        print("no expectations for %r yet -- skipping" % part)
        return
    check.check("out/%s.stl" % part, EXPECT[part])


if __name__ == "__main__":
    main()
