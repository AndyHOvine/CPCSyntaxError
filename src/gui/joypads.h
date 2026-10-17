// CPCSyntaxError — the host's joysticks and joypads as the CPC's two joysticks.
//
// Every connected device is opened as an SDL joystick (a game controller SDL knows the
// layout of, or any other stick). Each of the CPC's joysticks (1 and 2) takes a device:
// "auto" (the first and the second connected, past any chosen for the other one), "none",
// or one by its GUID. Each device has its own map: for each of the CPC joystick's six
// lines (up, down, left, right, fire 1, fire 2) the inputs that press it, any of buttons,
// axis directions and hat directions. A device SDL has a game-controller layout for starts
// from it (its d-pad and left stick, A and B); any other from hat 0, axes 0/1, buttons 0/1.
// Header-only: EmuHost holds one.
#pragma once
#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace cpcse {

struct PadBinding {
    enum Type : uint8_t { Button, AxisPlus, AxisMinus, Hat };
    Type type = Button;
    int index = 0;
    int hat = 0;                     // SDL_HAT_UP / _RIGHT / _DOWN / _LEFT, for a hat
    bool operator==(const PadBinding& o) const { return type == o.type && index == o.index && (type != Hat || hat == o.hat); }
    // "b3", "a1+", "a1-", "h0.1": what the ini keeps
    std::string code() const {
        switch (type) {
            case Button: return "b" + std::to_string(index);
            case AxisPlus: return "a" + std::to_string(index) + "+";
            case AxisMinus: return "a" + std::to_string(index) + "-";
            default: return "h" + std::to_string(index) + "." + std::to_string(hat);
        }
    }
    static bool parse(const std::string& s, PadBinding& out) {
        if (s.size() < 2) return false;
        const int n = std::atoi(s.c_str() + 1);
        if (s[0] == 'b') { out = { Button, n, 0 }; return true; }
        if (s[0] == 'a') { out = { s.back() == '-' ? AxisMinus : AxisPlus, n, 0 }; return true; }
        if (s[0] == 'h') { const size_t dot = s.find('.'); if (dot == std::string::npos) return false; out = { Hat, n, std::atoi(s.c_str() + dot + 1) }; return true; }
        return false;
    }
    // what the settings show
    std::string text() const {
        switch (type) {
            case Button: return "button " + std::to_string(index);
            case AxisPlus: return "axis " + std::to_string(index) + " +";
            case AxisMinus: return "axis " + std::to_string(index) + " -";
            default: {
                const char* d = hat == SDL_HAT_UP ? "up" : hat == SDL_HAT_DOWN ? "down" : hat == SDL_HAT_LEFT ? "left" : hat == SDL_HAT_RIGHT ? "right" : "?";
                return "hat " + std::to_string(index) + " " + d;
            }
        }
    }
};

constexpr int PAD_LINES = 6;   // the CPC joystick's lines, in its bit order
inline const char* padLineName(int line) {
    static const char* names[PAD_LINES] = { "Up", "Down", "Left", "Right", "Fire 1", "Fire 2" };
    return line >= 0 && line < PAD_LINES ? names[line] : "?";
}

struct PadMap {
    std::vector<PadBinding> line[PAD_LINES];
    // "up=h0.1,a1-;down=...": one line for the ini
    std::string code() const {
        static const char* keys[PAD_LINES] = { "up", "down", "left", "right", "fire1", "fire2" };
        std::string s;
        for (int i = 0; i < PAD_LINES; i++) {
            if (i) s += ';';
            s += keys[i]; s += '=';
            for (size_t k = 0; k < line[i].size(); k++) { if (k) s += ','; s += line[i][k].code(); }
        }
        return s;
    }
    static bool parse(const std::string& s, PadMap& out) {
        static const char* keys[PAD_LINES] = { "up", "down", "left", "right", "fire1", "fire2" };
        PadMap m;
        std::stringstream all(s);
        std::string part;
        bool any = false;
        while (std::getline(all, part, ';')) {
            const size_t eq = part.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = part.substr(0, eq);
            for (int i = 0; i < PAD_LINES; i++) {
                if (key != keys[i]) continue;
                std::stringstream list(part.substr(eq + 1));
                std::string b;
                while (std::getline(list, b, ',')) { PadBinding pb; if (PadBinding::parse(b, pb)) m.line[i].push_back(pb); }
                any = true;
            }
        }
        if (any) out = m;
        return any;
    }
};

struct PadDevice {
    SDL_Joystick* joy = nullptr;
    SDL_GameController* controller = nullptr;   // when SDL knows its layout
    std::string name, guid;
};

class JoypadManager {
public:
    std::vector<PadDevice> devices;
    std::string port[2] = { "auto", "auto" };   // CPC joystick 1 and 2: a GUID, "auto" or "none"
    std::map<std::string, PadMap> maps;          // per device GUID; one not here has its default
    // capture ("press to assign"): the port and line it fills, and the device's inputs at
    // its start (an input counts when it moves away from there)
    int capturePort = -1, captureLine = -1;
    bool captureAdd = false;
    std::vector<uint8_t> captureButtons, captureHats;
    std::vector<int16_t> captureAxes;

    ~JoypadManager() { close(); }
    void close() {
        for (PadDevice& d : devices) { if (d.controller) SDL_GameControllerClose(d.controller); else if (d.joy) SDL_JoystickClose(d.joy); }
        devices.clear();
    }
    // every connected device, opened again (at the start, and when one comes or goes)
    void open() {
        close();
        if (!SDL_WasInit(SDL_INIT_JOYSTICK)) return;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            PadDevice d;
            if (SDL_IsGameController(i)) d.controller = SDL_GameControllerOpen(i);
            d.joy = d.controller ? SDL_GameControllerGetJoystick(d.controller) : SDL_JoystickOpen(i);
            if (!d.joy) continue;
            const char* n = SDL_JoystickName(d.joy);
            d.name = n ? n : "Joystick";
            char g[64];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(d.joy), g, sizeof g);
            d.guid = g;
            devices.push_back(d);
        }
        capturePort = -1;
    }
    // the device a CPC joystick takes: its own, or "auto" -- the first connected (port 1) or
    // the second (port 2), past any device the other port has by name
    int deviceFor(int p) const {
        if (p < 0 || p > 1 || port[p] == "none") return -1;
        if (port[p] != "auto") {
            for (int i = 0; i < (int)devices.size(); i++) if (devices[(size_t)i].guid == port[p]) return i;
            return -1;
        }
        std::vector<int> free;
        for (int i = 0; i < (int)devices.size(); i++) if (devices[(size_t)i].guid != port[1 - p]) free.push_back(i);
        const size_t want = port[1 - p] == "auto" ? (size_t)p : 0;   // both automatic: the first and the second
        return want < free.size() ? free[want] : -1;
    }
    PadMap defaultMap(const PadDevice& d) const {
        PadMap m;
        if (d.controller) {
            auto add = [&](int line, SDL_GameControllerButtonBind b, PadBinding::Type axisDir) {
                if (b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) m.line[line].push_back({ PadBinding::Button, b.value.button, 0 });
                else if (b.bindType == SDL_CONTROLLER_BINDTYPE_HAT) m.line[line].push_back({ PadBinding::Hat, b.value.hat.hat, b.value.hat.hat_mask });
                else if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) m.line[line].push_back({ axisDir, b.value.axis, 0 });
            };
            add(0, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_DPAD_UP), PadBinding::AxisMinus);
            add(1, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN), PadBinding::AxisPlus);
            add(2, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT), PadBinding::AxisMinus);
            add(3, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT), PadBinding::AxisPlus);
            const SDL_GameControllerButtonBind x = SDL_GameControllerGetBindForAxis(d.controller, SDL_CONTROLLER_AXIS_LEFTX);
            const SDL_GameControllerButtonBind y = SDL_GameControllerGetBindForAxis(d.controller, SDL_CONTROLLER_AXIS_LEFTY);
            if (y.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) { m.line[0].push_back({ PadBinding::AxisMinus, y.value.axis, 0 }); m.line[1].push_back({ PadBinding::AxisPlus, y.value.axis, 0 }); }
            if (x.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) { m.line[2].push_back({ PadBinding::AxisMinus, x.value.axis, 0 }); m.line[3].push_back({ PadBinding::AxisPlus, x.value.axis, 0 }); }
            add(4, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_A), PadBinding::AxisPlus);
            add(5, SDL_GameControllerGetBindForButton(d.controller, SDL_CONTROLLER_BUTTON_B), PadBinding::AxisPlus);
            return m;
        }
        // A device SDL does not know: its hat when it has one, and no axes then -- a wheel's
        // pedals and a throttle rest at one end (the G920's axes 1-3 read +32767 released), so
        // an axis taken as a direction would hold it. Only a stick with no hat gets axes 0/1.
        if (SDL_JoystickNumHats(d.joy) > 0) {
            m.line[0].push_back({ PadBinding::Hat, 0, SDL_HAT_UP }); m.line[1].push_back({ PadBinding::Hat, 0, SDL_HAT_DOWN });
            m.line[2].push_back({ PadBinding::Hat, 0, SDL_HAT_LEFT }); m.line[3].push_back({ PadBinding::Hat, 0, SDL_HAT_RIGHT });
        } else if (SDL_JoystickNumAxes(d.joy) >= 2) {
            m.line[0].push_back({ PadBinding::AxisMinus, 1, 0 }); m.line[1].push_back({ PadBinding::AxisPlus, 1, 0 });
            m.line[2].push_back({ PadBinding::AxisMinus, 0, 0 }); m.line[3].push_back({ PadBinding::AxisPlus, 0, 0 });
        }
        if (SDL_JoystickNumButtons(d.joy) > 0) m.line[4].push_back({ PadBinding::Button, 0, 0 });
        if (SDL_JoystickNumButtons(d.joy) > 1) m.line[5].push_back({ PadBinding::Button, 1, 0 });
        return m;
    }
    PadMap& mapFor(const PadDevice& d) {
        auto it = maps.find(d.guid);
        if (it == maps.end()) it = maps.emplace(d.guid, defaultMap(d)).first;
        return it->second;
    }
    static bool held(const PadDevice& d, const PadBinding& b) {
        switch (b.type) {
            case PadBinding::Button: return SDL_JoystickGetButton(d.joy, b.index) != 0;
            case PadBinding::AxisPlus: return SDL_JoystickGetAxis(d.joy, b.index) > 16384;
            case PadBinding::AxisMinus: return SDL_JoystickGetAxis(d.joy, b.index) < -16384;
            default: return (SDL_JoystickGetHat(d.joy, b.index) & b.hat) != 0;
        }
    }
    // the CPC joystick's lines this port presses now (bit n = line n)
    int lines(int p) {
        const int i = deviceFor(p);
        if (i < 0) return 0;
        const PadDevice& d = devices[(size_t)i];
        const PadMap& m = mapFor(d);
        int bits = 0;
        for (int l = 0; l < PAD_LINES; l++) for (const PadBinding& b : m.line[l]) if (held(d, b)) { bits |= 1 << l; break; }
        return bits;
    }
    // ---- press to assign
    void captureStart(int p, int line, bool add) {
        const int i = deviceFor(p);
        if (i < 0) return;
        const PadDevice& d = devices[(size_t)i];
        capturePort = p; captureLine = line; captureAdd = add;
        captureButtons.assign((size_t)SDL_JoystickNumButtons(d.joy), 0);
        for (size_t k = 0; k < captureButtons.size(); k++) captureButtons[k] = SDL_JoystickGetButton(d.joy, (int)k);
        captureHats.assign((size_t)SDL_JoystickNumHats(d.joy), 0);
        for (size_t k = 0; k < captureHats.size(); k++) captureHats[k] = SDL_JoystickGetHat(d.joy, (int)k);
        captureAxes.assign((size_t)SDL_JoystickNumAxes(d.joy), 0);
        for (size_t k = 0; k < captureAxes.size(); k++) captureAxes[k] = SDL_JoystickGetAxis(d.joy, (int)k);
    }
    // the first input that moved since the capture began: in the map; true when it is done
    bool captureStep() {
        if (capturePort < 0) return false;
        const int i = deviceFor(capturePort);
        if (i < 0) { capturePort = -1; return false; }
        const PadDevice& d = devices[(size_t)i];
        PadBinding got;
        bool found = false;
        for (size_t k = 0; k < captureButtons.size() && !found; k++)
            if (SDL_JoystickGetButton(d.joy, (int)k) && !captureButtons[k]) { got = { PadBinding::Button, (int)k, 0 }; found = true; }
        for (size_t k = 0; k < captureHats.size() && !found; k++) {
            const int h = SDL_JoystickGetHat(d.joy, (int)k);
            for (int dir : { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT })
                if ((h & dir) && !(captureHats[k] & dir)) { got = { PadBinding::Hat, (int)k, dir }; found = true; break; }
        }
        for (size_t k = 0; k < captureAxes.size() && !found; k++) {
            const int v = SDL_JoystickGetAxis(d.joy, (int)k), from = captureAxes[k];
            if (v - from > 20000) { got = { PadBinding::AxisPlus, (int)k, 0 }; found = true; }
            else if (from - v > 20000) { got = { PadBinding::AxisMinus, (int)k, 0 }; found = true; }
        }
        if (!found) return false;
        std::vector<PadBinding>& list = mapFor(d).line[captureLine];
        if (!captureAdd) list.clear();
        bool have = false;
        for (const PadBinding& b : list) have = have || b == got;
        if (!have) list.push_back(got);
        capturePort = -1;
        return true;
    }
    // ---- the ini: joy1, joy2, and joymap.<guid> for each device given a map
    void load(const std::map<std::string, std::string>& ini) {
        auto get = [&](const char* k) { auto it = ini.find(k); return it != ini.end() ? it->second : std::string("auto"); };
        port[0] = get("joy1"); port[1] = get("joy2");
        analogue[0] = get("joy1analogue") == "1"; analogue[1] = get("joy2analogue") == "1";
        for (const auto& kv : ini) {
            if (kv.first.rfind("joymap2.", 0) != 0) continue;
            PadMap m;
            if (PadMap::parse(kv.second, m)) maps[kv.first.substr(8)] = m;
        }
    }
    // a map kept only when it is not the device's default (a default made by an older
    // version is then never stuck in the ini)
    void save(std::ostream& out) const {
        out << "joy1=" << port[0] << "\n" << "joy2=" << port[1] << "\n";
        out << "joy1analogue=" << (analogue[0] ? 1 : 0) << "\n" << "joy2analogue=" << (analogue[1] ? 1 : 0) << "\n";
        for (const auto& kv : maps) {
            bool isDefault = false;
            for (const PadDevice& d : devices) if (d.guid == kv.first) isDefault = defaultMap(d).code() == kv.second.code();
            if (!isDefault) out << "joymap2." << kv.first << "=" << kv.second.code() << "\n";
        }
    }
    // THE PLUS ANALOGUE PORT: a CPC joystick in analogue mode gives its stick to the ASIC's
    // ADC (joystick 1 channels 0/1, joystick 2 channels 2/3: X then Y, 0 left/top to 63
    // right/bottom); its fire buttons stay the digital lines. The stick: the map's
    // stickX/stickY, else a known joypad's left stick, else axes 0/1.
    bool analogue[2] = { false, false };
    void stickAxes(const PadDevice& d, int& x, int& y) const {
        x = 0; y = 1;
        if (d.controller) {
            const SDL_GameControllerButtonBind bx = SDL_GameControllerGetBindForAxis(d.controller, SDL_CONTROLLER_AXIS_LEFTX);
            const SDL_GameControllerButtonBind by = SDL_GameControllerGetBindForAxis(d.controller, SDL_CONTROLLER_AXIS_LEFTY);
            if (bx.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) x = bx.value.axis;
            if (by.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) y = by.value.axis;
        }
    }
    // the stick as the ADC reads it (0..63), or false when the port has no device
    bool stick(int p, int& x, int& y) {
        const int i = deviceFor(p);
        if (i < 0) return false;
        const PadDevice& d = devices[(size_t)i];
        int ax = 0, ay = 1;
        stickAxes(d, ax, ay);
        auto adc = [](int v) { return std::clamp((v + 32768) * 64 / 65536, 0, 63); };
        x = adc(SDL_JoystickGetAxis(d.joy, ax));
        y = adc(SDL_JoystickGetAxis(d.joy, ay));
        return true;
    }
};

} // namespace cpcse
