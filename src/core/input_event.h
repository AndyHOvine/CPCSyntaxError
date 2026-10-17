// CPCSyntaxError — one input to the machine, as the timeline logs it (reverse debugging,
// input movies). The machine is the same every run from the same state with the same
// inputs, so a log of these, each stamped with the instruction it came before, replays a
// run exactly.
#pragma once
#include "common.h"

namespace cpcse {

struct InputEvent {
    enum Kind : uint8_t {
        Key,              // KeyboardMatrix::setKey(code, a, text, b)
        Joystick,         // KeyboardMatrix::setJoystick(a, b, code, c)
        MatrixDirect,     // KeyboardMatrix::setMatrixDirect(a, b, c)
        LightgunTrigger,  // KeyboardMatrix::setLightgunTrigger(a)
        LightgunAim,      // EmuHost::lightgunAim(x, y, a)
        MouseMove,        // EmuHost::mouseMove(x, y)
        MouseButton,      // EmuHost::mouseButton(a, b)
        MouseScroll,      // EmuHost::mouseScroll(x)
        Cheats,           // EmuHost: the cheat list's freezes applied
        TapeDeck,         // EmuHost: a tape button (a: DECK_PLAY.., b: the block for DECK_SEEK)
        Reset,            // EmuHost::reset: the machine's reset button
        Analogue,         // EmuHost::setAnalogue(a, b): a Plus analogue channel
    };
    enum TapeButton { DECK_PLAY, DECK_PAUSE, DECK_REWIND, DECK_REWIND_BLOCK, DECK_FAST_FORWARD, DECK_SEEK, DECK_STOP };
    uint64_t at = 0;      // GX4000::instructions when it came: it goes in before that instruction
    Kind kind = Key;
    int a = 0, b = 0, c = 0;
    double x = 0, y = 0;
    std::string code, text;
};

} // namespace cpcse
