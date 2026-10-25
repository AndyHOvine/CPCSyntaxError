// CPCSyntaxError — CPC keyboard matrix.
// Maps host keys, joystick and lightgun signals to CPC matrix lines.
#pragma once
#include "common.h"
#include "input_event.h"

namespace cpcse {

using KeyCell = std::array<int, 2>;   // [row, bit]
using CharCell = std::array<int, 3>;  // [row, bit, shift]

std::string normalizeCpcKeyboardRegion(const std::string& region = "uk");
std::optional<CharCell> cpcCharacterMapping(const std::string& character, const std::string& code, const std::string& region = "uk");
// What is printed on the key at matrix row/bit of this CPC keyboard (uk, fr, es): [0] what
// it types alone, [1] with SHIFT ("" when the key shows one legend, as a letter does). From
// the tables the PC layout types by; empty for the keys with names (ESC, SHIFT...).
std::array<std::string, 2> cpcKeyLegend(int row, int bit, const std::string& region = "uk");

class KeyboardMatrix {
public:
    std::array<uint8_t, 10> rows{};
    bool joystickEnabled = true;
    std::string region = "uk";
    std::unordered_set<std::string> pressedKeys;
    std::unordered_map<std::string, CharCell> pressedSymbols;
    std::unordered_set<int> directBits;
    std::unordered_map<std::string, std::string> codeToEffective;

    int keyboardJoystick = 0xff, gamepadJoystick = 0xff, lightgunJoystick = 0xff;
    // Second joystick (CPC keyboard line 6 / GX4000 dual joystick, player 2).
    int keyboardJoystick1 = 0xff, gamepadJoystick1 = 0xff;
    int selectedRow = 0;
    std::string lightgunMode = "trojan";
    bool lightgunEnabled = false;
    bool lightgunBeamHit = false;
    bool lightgunTrigger = false;
    bool gunstickTriggerPending = false;
    int gunstickReadCounter = 0;

    std::function<void(const std::string& mode)> onLightgunRead;

    // THE AMX MOUSE on the joystick port (CPCWiki "AMX Mouse"). It has no motion counters:
    // a movement is a LOW pulse on a direction line of row 9 (bit 0 up, 1 down, 2 left, 3
    // right), one pulse a "mickey"; the buttons are row 9 bits 4 (left), 5 (right) and 6
    // (middle), LOW when pressed. "The value updates when the row is deselected": each time
    // the CPC selects another row after row 9 the interface shows the next step. Its two
    // dual flip-flops (MC14013) hold one movement each way between steps, so at most one
    // mickey a step on each axis ("maximum is 300 mickeys per second" at AMX Art's 300 Hz).
    bool amxEnabled = false;
    int amxMoveX = 0, amxMoveY = 0;    // movement held for the next step: -1, 0 or 1
    int amxButtons = 0;                // bit 0 left, 1 right, 2 middle held
    int amxLines = 0xff;               // the direction lines as the last step left them
    void amxMove(int dx, int dy);
    void amxButton(int index, bool pressed);

    KeyboardMatrix();
    void reset();
    std::string setRegion(const std::string& region = "uk");
    void setJoystickEnabled(bool enabled);
    void rebuildKeyboardState();
    void setMatrixDirect(int row, int bit, bool pressed);
    void applyDirectBits();
    bool setKey(const std::string& code, bool pressed, const std::string& character = "", int location = 0);
    void setJoystick(int bit, bool pressed, const std::string& source = "keyboard", int player = 0);
    void setLightgunEnabled(bool enabled);
    void setLightgunType(const std::string& type = "trojan");
    void setLightgunBeamHit(bool hit);
    void setLightgunTrigger(bool pressed);
    void selectRow(int row);
    int read() { return read(selectedRow); }
    int read(int row);
    // Diagnostic: every matrix row the machine reads, with what it saw. Unset in normal
    // use. The SHAKER runner hangs a key trace off it (SHAKER_KEY_TRACE) to tell a
    // program that raced past a held key from one whose key was never pressed.
    std::function<void(int row, int value)> onRowRead;
    // The timeline's log (input_event.h): each input as it comes, before it takes effect.
    // A joystick call that changes nothing is not passed on (the gamepad repeats them).
    std::function<void(const InputEvent&)> onInput;
};

} // namespace cpcse
