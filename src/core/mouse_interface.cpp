// CPCSyntaxError — the Kempston mouse and the MultiPlay. See mouse_interface.h.
#include "mouse_interface.h"

#include <algorithm>

namespace cpcse {

// The Kempston's partial decode: A10, A8, A4 and A0 ("confirmed connected").
static bool atKempstonX(int p) { return (p & 0x0511) == 0x0100; }
static bool atKempstonY(int p) { return (p & 0x0511) == 0x0101; }
static bool atKempstonButtons(int p) { return (p & 0x0510) == 0x0000; }

bool MouseInterface::handlesPort(int port) const {
    port &= 0xffff;
    if (kind == Kempston) return atKempstonX(port) || atKempstonY(port) || atKempstonButtons(port);
    if (kind == MultiplayA || kind == MultiplayB) return port >= 0xf990 && port <= 0xf995;
    return false;
}

int MouseInterface::readPort(int port) {
    port &= 0xffff;
    if (kind == Kempston) {
        if (atKempstonX(port)) return kempstonX;
        if (atKempstonY(port)) return kempstonY;
        return 0xfc | ((buttons & 2) ? 0 : 0x01) | ((buttons & 1) ? 0 : 0x02);
    }
    const int ours = kind == MultiplayA ? 0 : 1;            // the port the mouse is in
    if (port == 0xf990 || port == 0xf991) {
        if (port - 0xf990 != ours) return 0x00;              // nothing in the other port
        return ((buttons & 1) ? 0x10 : 0) | ((buttons & 2) ? 0x20 : 0) | ((buttons & 4) ? 0x40 : 0);
    }
    const int reg = port - 0xf992;                           // 0 A.X, 1 A.Y, 2 B.X, 3 B.Y
    if (reg / 2 != ours) return 0x00;
    int& offset = (reg & 1) ? multiplayY : multiplayX;
    const int v = offset;
    offset = 0;                                              // the offset since the last read
    return v & 0xff;                                         // -8..7, bits 7-4 = bit 3
}

void MouseInterface::move(int dx, int dy) {
    if (kind == Kempston) {
        kempstonX = (uint8_t)(kempstonX + dx);
        kempstonY = (uint8_t)(kempstonY - dy);               // up is positive
    } else if (kind == MultiplayA || kind == MultiplayB) {
        multiplayX = std::clamp(multiplayX + dx, -8, 7);     // saturates, never wraps
        multiplayY = std::clamp(multiplayY + dy, -8, 7);
    }
}

void MouseInterface::button(int index, bool pressed) {
    if (index < 0 || index > 2) return;
    if (pressed) buttons |= 1 << index; else buttons &= ~(1 << index);
}

} // namespace cpcse
