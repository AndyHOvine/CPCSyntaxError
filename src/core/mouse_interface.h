// CPCSyntaxError — mouse interfaces on the CPC's I/O bus: the Kempston mouse and TotO's
// MultiPlay. (The AMX mouse sits on the joystick port: KeyboardMatrix::amx*.)
//
// KEMPSTON MOUSE (CPCWiki "Kempston Mouse"), all reads:
//   &FBEE  X position, decoded xxxx x0x1 xxx0 xxx0
//   &FBEF  Y position, decoded xxxx x0x1 xxx0 xxx1
//   &FAEF  buttons,    decoded xxxx x0x0 xxx0 xxxx: bit 0 right, bit 1 left, active low;
//          bits 2-7 read 1
//   Two 8-bit counters that wrap: X right is positive, Y UP is positive ("inverted relative
//   to screen coordinates"). A read does not clear them; they are 0 at power-on and a bus
//   reset leaves them.
//
// MULTIPLAY (CPCWiki "MultiPlay"), all reads:
//   &F990 / &F991  port A / B action bits: 0 up, 1 down, 2 left, 3 right, 4 fire 1 or left
//                  button, 5 fire 2 or right, 6 fire 3 or middle, 7 always 0 (the page's
//                  BASIC tests "a AND 16" for LEFT: a pressed button is 1)
//   &F992 / &F993  port A mouse X / Y; &F994 / &F995 port B
//   "Mouse register X/Y return the offset since the last read", a 4-bit value sign-extended
//   to 8 bits ("bit 7-4 = bit 3"), which saturates and "will never overflow or wrap". Y is
//   positive downwards: the page's cursor programs add it to a row counted down the screen.
//   &F996 / &F997 are unused. The mouse is in one port (A or B, chosen); the other is empty.
#pragma once
#include "common.h"

namespace cpcse {

struct MouseInterface {
    enum Kind { None = 0, Kempston, MultiplayA, MultiplayB };
    int kind = None;

    uint8_t kempstonX = 0, kempstonY = 0;   // the Kempston's counters
    int multiplayX = 0, multiplayY = 0;     // the MultiPlay's offsets since the last read (-8..7)
    int buttons = 0;                        // bit 0 left, 1 right, 2 middle held

    bool handlesPort(int port) const;
    int readPort(int port);
    // Host mouse counts: x right positive, y DOWN positive.
    void move(int dx, int dy);
    void button(int index, bool pressed);
};

} // namespace cpcse
