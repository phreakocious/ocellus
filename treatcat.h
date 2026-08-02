#pragma once
#include <cstdint>

// Interactive "treat cat" (anim id 46). Renders into the shared canvas. On the touch board a
// tap feeds a treat (hearts at the tap point); on non-touch boards it runs ambient (affirmations
// auto-rotate). Ported from references/treatcat-round-lcd/treatcat_gc9a01/treatcat_gc9a01.ino.
void renderTreatcat(uint32_t now);

// Reset transient scene state on mode entry (clears a stale tap + any in-progress fortune; keeps the
// endless treat count). Call from onAnimEnter(TREATCAT_ID).
void treatcatOnEnter();

// Bench hooks, beside "bat"/"batsim" in main.cpp's serial poll rather than in protocol.cpp,
// because they touch live device state and not the persisted config:
//   {"cmd":"pet"}                              -> {"type":"pet","full":..,"en":..,"treats":..,"asleep":..}
//   {"cmd":"petsim","full":5,"en":9,"treats":4} -> same reply, after forcing the stats (all keys optional)
//   {"cmd":"pettap"}                           -> inject a tap at the cat's centre
// These exist because the shipped decay makes the care loop untestable by hand: ~25 min to the
// first hunger, and 10-23 min between feeds after that (FEED_GAIN 55 over HUNGRY_THRESH 40), so
// the every-5-treats fortune is over an hour of waiting away. pettap goes through the same
// gTreatTap mailbox the touch ISR uses, so it exercises onTap's real path.
// Returns false if the line is none of the three.
bool treatcatPetCmd(const char* line, char* out, unsigned outLen);

// Atomic tap mailbox: the button task packs (1u<<31)|(x<<12)|y (x,y in 0..239); renderTreatcat
// reads-and-clears it. A single aligned 32-bit word -> torn-free across tasks/cores.
extern volatile uint32_t gTreatTap;
