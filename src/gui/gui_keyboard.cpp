// CPCSyntaxError GUI — the on-screen CPC keyboard.
//
// The machine's own keyboard, drawn as it is (from photographs of UK machines):
//  - 464: black keys on the dark grey case, ESC red, TAB / CAPS LOCK / SHIFT / CTRL / DEL
//    and COPY green, the two ENTERs blue; the cursor keys a cross round COPY above the
//    keypad, which shows plain digits.
//  - 664: the 464's shape in light grey keys, the same keys light blue, the cursor keys a
//    blue diamond pad round a grey COPY, an f0-f9 keypad.
//  - 6128: beige keys on the black case, the L-shaped RETURN, the f-keys beside DEL, the
//    cursor keys and the long ENTER on the bottom row.
//  - Plus: the 6128's keys, off-white, the named keys and the cursor keys mid grey, on
//    the cream case: the embossed name on the cover above, the badge below; the 464 Plus's
//    cover with its tape window and buttons.
// Each key is a cell of the matrix (keyboard.cpp's table); its legends are those of the CPC
// keyboard chosen (UK, French, Spanish), from the tables the PC layout types by, printed as
// the CPC prints them (a slashed zero, an arrow for ^). A key lights while the CPC sees it
// down, from whatever pressed it. A click holds it while the button is down; a right click
// latches it (SHIFT or CONTROL with another key), a second right click lets it go.
#include "gui_shell.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/keyboard.h"

namespace cpcse {

namespace {
enum class Look { K464, K664, K6128, Plus };
enum Tint { Normal, Red, Green, Blue, Dark, Light };
// a key's shape: a rectangle; an L (the 6128's RETURN: its lower half narrower); or one of
// the four parts of the 664's cursor pad (x, y, w, h are then the whole pad)
enum Shape { Rect, LShape, PadUp, PadLeft, PadRight, PadDown };

struct KeyDef {
    float x, y, w, h;
    int row, bit;
    const char* name;      // a key with a name (ESC, SHIFT...); null: its legends
    Tint tint;
    Shape shape = Rect;
    float lowerX = 0, lowerW = 0;   // LShape: the lower row's part
};

const char* const UP = "\xe2\x86\x91", * const DOWN = "\xe2\x86\x93", * const LEFT = "\xe2\x86\x90", * const RIGHT = "\xe2\x86\x92";

// The keys, in key widths (u) from the main block's top left.
std::vector<KeyDef> layoutFor(Look look, const std::string& region) {
    const bool es = region == "es";
    const bool classic = look == Look::K464 || look == Look::K664;   // the 464's shape
    const bool g = look == Look::K464, b = look == Look::K664, p = look == Look::Plus;
    // the named keys' colour: green on the 464, blue on the 664, darker on the Plus
    const Tint named = g ? Green : b ? Blue : p ? Dark : Normal;
    std::vector<KeyDef> k;
    auto key = [&](float x, float y, float w, float h, int row, int bit, const char* name = nullptr, Tint t = Normal) {
        k.push_back({ x, y, w, h, row, bit, name, t });
    };
    // row 0: ESC, 1..0, -, ^, CLR, DEL
    key(0, 0, 1, 1, 8, 2, "ESC", g ? Red : b ? Blue : p ? Dark : Normal);
    const int digits[10][2] = { {8,0},{8,1},{7,1},{7,0},{6,1},{6,0},{5,1},{5,0},{4,1},{4,0} };
    for (int i = 0; i < 10; i++) key(1.0f + i, 0, 1, 1, digits[i][0], digits[i][1]);
    key(11, 0, 1, 1, 3, 1); key(12, 0, 1, 1, 3, 0);
    key(13, 0, 1, 1, 2, 0, "CLR", p ? Dark : Normal);
    key(14, 0, classic ? 1.5f : 1.0f, 1, 9, 7, es ? "\xe2\x86\x90" "BORR" : "DEL", named);
    // row 1: TAB, Q..P (AZERTY on a French keyboard: the cells are the same), @, [
    key(0, 1, 1.5f, 1, 8, 4, "TAB", named);
    const int top[10][2] = { {8,3},{7,3},{7,2},{6,2},{6,3},{5,3},{5,2},{4,3},{4,2},{3,3} };
    for (int i = 0; i < 10; i++) key(1.5f + i, 1, 1, 1, top[i][0], top[i][1]);
    key(11.5f, 1, 1, 1, 3, 2); key(12.5f, 1, 1, 1, 2, 1);
    // row 2: CAPS LOCK, A..L, :, ;, ]
    const float caps = classic ? 1.5f : 1.75f;
    key(0, 2, caps, 1, 8, 6, es ? "FIJA\nMAYS" : "CAPS\nLOCK", named);
    const int home[9][2] = { {8,5},{7,4},{7,5},{6,5},{6,4},{5,4},{5,5},{4,5},{4,4} };
    for (int i = 0; i < 9; i++) key(caps + i, 2, 1, 1, home[i][0], home[i][1]);
    key(caps + 9, 2, 1, 1, 3, 5); key(caps + 10, 2, 1, 1, 3, 4); key(caps + 11, 2, 1, 1, 2, 3);
    // RETURN: the 464's and 664's a block two keys wide; the 6128's an L
    if (classic) key(13.5f, 1, 2, 2, 2, 2, es ? "INTRO" : "ENTER", Blue);
    else {
        k.push_back({ 13.5f, 1, 1.5f, 2, 2, 2, "RETURN", named, LShape, 13.75f, 1.25f });
    }
    // row 3: SHIFT, Z..M , . / \, SHIFT
    key(0, 3, 2.25f, 1, 2, 5, es ? "MAYS" : "SHIFT", named);
    const int bottom[11][2] = { {8,7},{7,7},{7,6},{6,7},{6,6},{5,6},{4,6},{4,7},{3,7},{3,6},{2,6} };
    for (int i = 0; i < 11; i++) key(2.25f + i, 3, 1, 1, bottom[i][0], bottom[i][1]);
    key(13.25f, 3, classic ? 2.25f : 1.75f, 1, 2, 5, es ? "MAYS" : "SHIFT", named);
    if (classic) {
        // the space bar, CTRL beside it; the cursor keys and COPY above the keypad
        key(3.25f, 4, 9, 1, 5, 7, "");
        key(12.25f, 4, 1, 1, 2, 7, "CTRL", named);
        const float kx = 16.6f;
        if (g) {
            key(kx + 1, -2.3f, 1, 1, 0, 0, UP);
            key(kx, -1.3f, 1, 1, 1, 0, LEFT); key(kx + 1, -1.3f, 1, 1, 1, 1, es ? "COPIA" : "COPY", Green); key(kx + 2, -1.3f, 1, 1, 0, 1, RIGHT);
            key(kx + 1, -0.3f, 1, 1, 0, 2, DOWN);
        } else {
            // the 664's pad: four blue parts round COPY
            const float py = -1.75f, ph = 2.55f;
            k.push_back({ kx, py, 3, ph, 0, 0, UP, Blue, PadUp });
            k.push_back({ kx, py, 3, ph, 1, 0, LEFT, Blue, PadLeft });
            k.push_back({ kx, py, 3, ph, 0, 1, RIGHT, Blue, PadRight });
            k.push_back({ kx, py, 3, ph, 0, 2, DOWN, Blue, PadDown });
            key(kx + 1, py + (ph - 0.9f) / 2, 1, 0.9f, 1, 1, es ? "COPIA" : "COPY");
        }
        // the keypad: plain digits on the 464, f0-f9 on the 664; the zero slashed
        const char* names464[12] = { "7", "8", "9", "4", "5", "6", "1", "2", "3", "\xc3\x98", ".", nullptr };
        const char* names664[12] = { "f7", "f8", "f9", "f4", "f5", "f6", "f1", "f2", "f3", "f\xc3\x98", ".", nullptr };
        const int cells[12][2] = { {1,2},{1,3},{0,3},{2,4},{1,4},{0,4},{1,5},{1,6},{0,5},{1,7},{0,7},{0,6} };
        for (int i = 0; i < 11; i++) key(kx + i % 3, 1.0f + i / 3, 1, 1, cells[i][0], cells[i][1], g ? names464[i] : names664[i]);
        key(kx + 2, 4, 1, 1, 0, 6, es ? "INTRO" : "ENTER", Blue);
    } else {
        // 6128 / Plus: CONTROL, COPY, the space bar, the long ENTER; the f-keys beside DEL,
        // the cursor keys round f0
        key(0, 4, 2.25f, 1, 2, 7, "CONTROL", named);
        key(2.25f, 4, 1.75f, 1, 1, 1, es ? "COPIA" : "COPY", named);
        key(4, 4, 8, 1, 5, 7, "");
        key(12, 4, 3, 1, 0, 6, es ? "INTRO" : "ENTER", named);
        const float kx = 15.0f;
        key(kx, 0, 1, 1, 1, 2, "f7"); key(kx + 1, 0, 1, 1, 1, 3, "f8"); key(kx + 2, 0, 1, 1, 0, 3, "f9");
        key(kx, 1, 1, 1, 2, 4, "f4"); key(kx + 1, 1, 1, 1, 1, 4, "f5"); key(kx + 2, 1, 1, 1, 0, 4, "f6");
        key(kx, 2, 1, 1, 1, 5, "f1"); key(kx + 1, 2, 1, 1, 1, 6, "f2"); key(kx + 2, 2, 1, 1, 0, 5, "f3");
        const Tint arrows = p ? Dark : Normal;
        key(kx, 3, 1, 1, 1, 7, "f\xc3\x98"); key(kx + 1, 3, 1, 1, 0, 0, UP, arrows); key(kx + 2, 3, 1, 1, 0, 7, ".");
        key(kx, 4, 1, 1, 1, 0, LEFT, arrows); key(kx + 1, 4, 1, 1, 0, 2, DOWN, arrows); key(kx + 2, 4, 1, 1, 0, 1, RIGHT, arrows);
    }
    return k;
}

ImU32 rgb(int v, int a = 255) { return IM_COL32(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff, a); }
int shade(int c, float f) {
    const int r = std::clamp((int)((c >> 16 & 0xff) * f), 0, 255), g = std::clamp((int)((c >> 8 & 0xff) * f), 0, 255), b = std::clamp((int)((c & 0xff) * f), 0, 255);
    return r << 16 | g << 8 | b;
}
// the 664 pad's COPY key: its part of the pad, in pad units (0..1)
constexpr float PAD_COPY_X0 = 1.0f / 3, PAD_COPY_X1 = 2.0f / 3;
} // namespace

void GuiShell::windowCpcKeyboard() {
    if (!panelOpen("CPC keyboard")) return;
    if (!ImGui::Begin("CPC keyboard", &panelOpen("CPC keyboard"), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    struct EndWindow { ~EndWindow() { ImGui::End(); } } endWindow;
    KeyboardMatrix* kb = host.emu ? host.emu->keyboard : nullptr;
    const std::string model = host.currentModel >= 0 ? host.models[(size_t)host.currentModel].id : "";
    if (!kb || model == "gx4000") {
        keyboardLatched.clear(); keyboardHeld = -1;
        textWrappedDisabled(model == "gx4000" ? "A GX4000 has no keyboard." : "No machine.");
        return;
    }
    Look look = model == "cpc464" ? Look::K464 : model == "cpc664" ? Look::K664 : (model == "cpc464plus" || model == "cpc6128plus") ? Look::Plus : Look::K6128;
    if (keyboardLook > 0) look = (Look)(keyboardLook - 1);
    const std::string region = host.keyboardRegion;
    const std::vector<KeyDef> keys = layoutFor(look, region);
    // the look's colours: the case, the keys, their legends, and the coloured keys
    struct Palette { int body, key, legend, red, green, blue, dark, light, colouredLegend; };
    static const Palette palettes[4] = {
        { 0x3d3e41, 0x2a2b2e, 0xeeeeee, 0xc4272b, 0x3a9a48, 0x3d6ea6, 0x2a2b2e, 0x3d6ea6, 0xf4f4f4 },   // 464
        { 0x2e2a29, 0xd9dcd7, 0x3b3d40, 0xd9dcd7, 0xd9dcd7, 0x8ccbe6, 0xd9dcd7, 0xd9dcd7, 0x2c3c48 },   // 664
        { 0x2a2b2d, 0xdcd9cf, 0x2b2a27, 0xdcd9cf, 0xdcd9cf, 0xdcd9cf, 0xdcd9cf, 0xdcd9cf, 0x2b2a27 },   // 6128
        { 0xe5e1d5, 0xedeae1, 0x3b3a37, 0xedeae1, 0xedeae1, 0xedeae1, 0xc5c3bc, 0xc5c3bc, 0x3b3a37 },   // Plus
    };
    const Palette& pal = palettes[(int)look];
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const KeyDef& d : keys) { minX = std::min(minX, d.x); minY = std::min(minY, d.y); maxX = std::max(maxX, d.x + d.w); maxY = std::max(maxY, d.y + d.h); }
    const bool classicLook = look == Look::K464 || look == Look::K664;
    // room for the name above the keys (the Plus: its cover; and its badge below)
    minY = std::min(minY, classicLook ? -2.3f : look == Look::Plus ? -3.6f : -1.5f);
    if (look == Look::Plus) maxY = std::max(maxY, 6.3f);
    {
        FlowRow row;
        static const char* looks[] = { "As the machine", "CPC 464", "CPC 664", "CPC 6128", "Plus" };
        row.item(ImGui::GetFontSize() * 9);
        ImGui::Combo("##kblook", &keyboardLook, looks, 5);
        char what[96];
        std::snprintf(what, sizeof what, "%s keyboard. Right click: hold a key down.", region == "fr" ? "French" : region == "es" ? "Spanish" : "UK");
        row.textDisabled(what);
        if (!keyboardLatched.empty() && row.smallButton("Let go")) {
            for (int cell : keyboardLatched) kb->setMatrixDirect(cell >> 3, cell & 7, false);
            keyboardLatched.clear();
        }
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float margin = 0.45f;                         // the case round the keys, in keys
    const float spanX = maxX - minX + margin * 2, spanY = maxY - minY + margin * 2;
    const float u = std::max(8.0f, std::min(avail.x / spanX, avail.y / spanY));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##keyboard", ImVec2(spanX * u, spanY * u), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + spanX * u, origin.y + spanY * u), rgb(pal.body), u * 0.25f);
    auto toScreen = [&](float x, float y) { return ImVec2(origin.x + (x - minX + margin) * u, origin.y + (y - minY + margin) * u); };
    // which key the mouse is over
    const ImVec2 m = ImGui::GetIO().MousePos;
    auto contains = [&](const KeyDef& d) {
        const ImVec2 a = toScreen(d.x, d.y), z = toScreen(d.x + d.w, d.y + d.h);
        if (m.x < a.x || m.x >= z.x || m.y < a.y || m.y >= z.y) return false;
        if (d.shape == LShape && m.y >= a.y + u) {
            const ImVec2 la = toScreen(d.lowerX, d.y + 1);
            return m.x >= la.x && m.x < la.x + d.lowerW * u;
        }
        if (d.shape >= PadUp) {
            const float fx = (m.x - a.x) / (z.x - a.x), fy = (m.y - a.y) / (z.y - a.y);
            const float dx = fx - 0.5f, dy = fy - 0.5f;
            if (fx >= PAD_COPY_X0 && fx < PAD_COPY_X1 && std::fabs(dy) < 0.9f / d.h / 2) return false;   // COPY's
            const bool vertical = std::fabs(dy) * d.h > std::fabs(dx) * d.w;
            return d.shape == PadUp ? vertical && dy < 0 : d.shape == PadDown ? vertical && dy > 0 : d.shape == PadLeft ? !vertical && dx < 0 : !vertical && dx > 0;
        }
        return true;
    };
    int over = -1;
    if (hovered) for (size_t i = 0; i < keys.size(); i++) if (contains(keys[i])) over = (int)i;
    // the mouse: held while the left button is down, latched by the right
    const bool blocked = host.liveInputBlocked();
    if (!blocked && over >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        keyboardHeld = keys[(size_t)over].row << 3 | keys[(size_t)over].bit;
        kb->setMatrixDirect(keyboardHeld >> 3, keyboardHeld & 7, true);
    }
    if (keyboardHeld >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!keyboardLatched.count(keyboardHeld)) kb->setMatrixDirect(keyboardHeld >> 3, keyboardHeld & 7, false);
        keyboardHeld = -1;
    }
    if (!blocked && over >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int cell = keys[(size_t)over].row << 3 | keys[(size_t)over].bit;
        const bool latch = !keyboardLatched.count(cell);
        if (latch) keyboardLatched.insert(cell); else keyboardLatched.erase(cell);
        kb->setMatrixDirect(cell >> 3, cell & 7, latch);
    }
    ImFont* font = ImGui::GetFont();
    auto text = [&](float size, ImVec2 at, int ink, const char* s) { dl->AddText(font, size, at, rgb(ink), s); };
    auto centred = [&](float size, ImVec2 t0, ImVec2 t1, int ink, const char* s) {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        text(size, ImVec2((t0.x + t1.x - ts.x) / 2, (t0.y + t1.y - ts.y) / 2), ink, s);
    };
    // the machine's name above the keys, as on its case
    auto colourBars = [&](float x, float y) {   // the three colour blocks of the CPC logo
        const int bars[3] = { 0xd8332e, 0x2f9c4a, 0x2f63b8 };
        for (int i = 0; i < 3; i++) {
            const ImVec2 a = toScreen(x + i * 0.32f, y), z = toScreen(x + i * 0.32f + 0.28f, y + 0.26f);
            dl->AddRectFilled(a, z, rgb(bars[i]));
            dl->AddRect(a, z, rgb(0xe8e8e8), 0, 0, std::max(1.0f, u * 0.02f));
        }
    };
    if (classicLook) {
        const bool k464 = look == Look::K464;
        const ImVec2 a = toScreen(0, -2.15f), z = toScreen(15.5f, -0.55f);
        if (k464) dl->AddRectFilled(a, z, rgb(0x55575b), u * 0.05f);
        const ImVec2 la = toScreen(0.15f, -1.75f), lz = toScreen(2.35f, -1.05f);
        if (k464) dl->AddRectFilled(la, lz, rgb(0x8b8d91), u * 0.04f);
        centred(u * 0.36f, la, lz, k464 ? 0x3a3b3e : 0xe7d7a8, "AMSTRAD");
        text(u * 0.2f, toScreen(2.7f, -1.5f), k464 ? 0xdedede : 0x9a948a, "64K COLOUR PERSONAL COMPUTER");
        text(u * 0.5f, toScreen(10.3f, -1.68f), k464 ? 0xdedede : 0xb9b3a8, k464 ? "CPC464" : "CPC664");
        colourBars(12.5f, -1.48f);
        text(u * 0.16f, toScreen(13.5f, -1.47f), k464 ? 0xdedede : 0x9a948a, "COLOUR");
        dl->AddCircleFilled(toScreen(14.75f, -1.9f), u * 0.08f, rgb(0x8e1c1c));
        text(u * 0.13f, toScreen(14.35f, -1.98f), 0xb0b0b0, "ON");
    } else {
        // the keys sit in a recess of the case; the name on a plate above it
        const ImVec2 ra = toScreen(-0.3f, -0.3f), rz = toScreen(18.3f, 5.3f);
        dl->AddRectFilled(ra, rz, rgb(shade(pal.body, look == Look::Plus ? 0.93f : 0.8f)), u * 0.15f);
        if (look == Look::K6128) {
            const ImVec2 pa = toScreen(0.0f, -1.35f), pz = toScreen(6.4f, -0.65f);
            dl->AddRectFilled(pa, pz, rgb(0x1f2022));
            dl->AddRect(pa, pz, rgb(0x8a8a8a), 0, 0, std::max(1.0f, u * 0.025f));
            text(u * 0.3f, toScreen(0.15f, -1.2f), 0xf0f0f0, "AMSTRAD");
            text(u * 0.24f, toScreen(1.8f, -1.17f), 0xf0f0f0, "128k Colour Personal Computer");
            // the rainbow stripes and the power light
            for (int i = 0; i < 3; i++) {
                const int c[3] = { 0xd8332e, 0x2f9c4a, 0x2f63b8 };
                const ImVec2 s0 = toScreen(15.2f + i * 0.22f, -0.75f), s1 = toScreen(15.42f + i * 0.22f, -1.2f);
                dl->AddQuadFilled(s0, ImVec2(s0.x + u * 0.12f, s0.y), ImVec2(s1.x + u * 0.12f, s1.y), s1, rgb(c[i]));
            }
            dl->AddRectFilled(toScreen(16.25f, -1.12f), toScreen(16.55f, -1.0f), rgb(0x7a1a1a));
            text(u * 0.14f, toScreen(16.6f, -1.15f), 0xd0d0d0, "ON");
        } else {
            // the cover over the keys: its two panels, the groove across them, the name
            // embossed (a highlight above, a shadow below the letters)
            const bool tape = model == "cpc464plus";    // the 464 Plus's cover holds its tape deck
            const ImVec2 ca = toScreen(0.6f, -3.45f), cz = toScreen(17.7f, -0.55f);
            dl->AddRectFilled(ca, cz, rgb(0xe9e6dc), u * 0.12f);
            dl->AddRect(ca, cz, rgb(0xcfcbbf), u * 0.12f, 0, std::max(1.0f, u * 0.03f));
            const ImVec2 split0 = toScreen(10.0f, -3.45f), split1 = toScreen(10.0f, -0.55f);
            dl->AddLine(split0, split1, rgb(0xcfcbbf), std::max(1.0f, u * 0.03f));
            dl->AddLine(toScreen(5.8f, -3.45f), toScreen(3.6f, -0.55f), rgb(0xd6d2c6), std::max(1.0f, u * 0.04f));
            auto emboss = [&](float size, float x, float y, const char* s) {
                text(size, toScreen(x - 0.03f, y - 0.03f), 0xf8f6f0, s);
                text(size, toScreen(x + 0.03f, y + 0.03f), 0xbdb8aa, s);
                text(size, toScreen(x, y), 0xe3dfd3, s);
            };
            emboss(u * 0.9f, 1.4f, -2.75f, "AMSTRAD");
            emboss(u * 0.8f, 10.6f, -2.85f, tape ? "464" : "6128");
            const float number = font->CalcTextSizeA(u * 0.8f, FLT_MAX, 0.0f, tape ? "464" : "6128").x / u;
            emboss(u * 0.35f, 10.7f + number, -2.85f, "plus");   // raised, close after the number
            if (tape) {
                // the cassette window and the deck's six keys
                dl->AddRectFilled(toScreen(13.0f, -3.25f), toScreen(17.1f, -2.7f), rgb(0xcfcbc0), u * 0.06f);
                dl->AddRectFilled(toScreen(13.2f, -3.12f), toScreen(16.9f, -2.83f), rgb(0x55534e), u * 0.04f);
                for (int i = 0; i < 6; i++) {
                    const ImVec2 b0 = toScreen(11.6f + i * 0.85f, -1.75f), b1 = toScreen(12.35f + i * 0.85f, -0.85f);
                    dl->AddRectFilled(b0, b1, rgb(i == 0 ? 0xd5d2c9 : 0xedeae1), u * 0.05f);
                    dl->AddRect(b0, b1, rgb(0xbdb8aa), u * 0.05f, 0, std::max(1.0f, u * 0.025f));
                }
            }
            dl->AddRectFilled(toScreen(1.0f, -3.25f), toScreen(1.25f, -3.18f), rgb(0x3a3a3a));   // the power light
            // the vents along the case's top edge: slanted slots
            for (float x = 1.0f; x < 17.2f; x += 0.16f)
                dl->AddLine(toScreen(x, -3.58f), toScreen(x + 0.1f, -3.47f), rgb(0xc4c0b4), std::max(1.0f, u * 0.03f));
            // the badge under the space bar: AMSTRAD in white on grey, the red stroke behind
            const ImVec2 ba = toScreen(7.9f, 5.55f), bz = toScreen(10.4f, 6.15f);
            dl->AddRectFilled(ba, bz, rgb(0x9a9a9c));
            dl->AddQuadFilled(toScreen(9.35f, 5.55f), toScreen(9.6f, 5.55f), toScreen(9.15f, 6.15f), toScreen(8.9f, 6.15f), rgb(0xd03030));
            centred(u * 0.3f, ba, bz, 0xffffff, "AMSTRAD");
        }
    }
    // the 664's pad under its keys
    for (const KeyDef& d : keys) {
        if (d.shape != PadUp) continue;
        const ImVec2 a = toScreen(d.x, d.y), z = toScreen(d.x + d.w, d.y + d.h);
        dl->AddRectFilled(ImVec2(a.x + u * 0.04f, a.y + u * 0.04f), ImVec2(z.x - u * 0.04f, z.y - u * 0.04f), rgb(shade(pal.blue, 0.6f)), u * 0.3f);
    }
    for (size_t i = 0; i < keys.size(); i++) {
        const KeyDef& d = keys[i];
        const int cell = d.row << 3 | d.bit;
        const bool down = (kb->rows[(size_t)d.row] >> d.bit & 1) == 0;
        const bool hot = (int)i == over;
        int face = pal.key;
        switch (d.tint) {
            case Red: face = pal.red; break;
            case Green: face = pal.green; break;
            case Blue: face = pal.blue; break;
            case Dark: face = pal.dark; break;
            case Light: face = pal.light; break;
            default: break;
        }
        const int ink = d.tint == Red || d.tint == Green || (d.tint == Blue && look == Look::K464) ? 0xf4f4f4
                      : d.tint == Blue ? pal.colouredLegend : pal.legend;
        const int top = shade(face, down ? 0.82f : hot ? 1.1f : 1.0f);
        const float gap = u * 0.06f, r = u * 0.1f, press = down ? u * 0.05f : 0.0f, inset = u * 0.09f;
        const ImVec2 a = toScreen(d.x, d.y), z = toScreen(d.x + d.w, d.y + d.h);
        if (d.shape >= PadUp) {
            // a part of the pad: a quad from the pad's edge to COPY's
            const ImVec2 c0(a.x + (z.x - a.x) * PAD_COPY_X0, (a.y + z.y) / 2 - u * 0.45f), c1(a.x + (z.x - a.x) * PAD_COPY_X1, (a.y + z.y) / 2 + u * 0.45f);
            const float e = u * 0.1f;
            ImVec2 q[4];
            if (d.shape == PadUp) { q[0] = ImVec2(a.x + e, a.y + e); q[1] = ImVec2(z.x - e, a.y + e); q[2] = ImVec2(c1.x, c0.y); q[3] = c0; }
            else if (d.shape == PadDown) { q[0] = ImVec2(z.x - e, z.y - e); q[1] = ImVec2(a.x + e, z.y - e); q[2] = ImVec2(c0.x, c1.y); q[3] = c1; }
            else if (d.shape == PadLeft) { q[0] = ImVec2(a.x + e, z.y - e); q[1] = ImVec2(a.x + e, a.y + e); q[2] = c0; q[3] = ImVec2(c0.x, c1.y); }
            else { q[0] = ImVec2(z.x - e, a.y + e); q[1] = ImVec2(z.x - e, z.y - e); q[2] = c1; q[3] = ImVec2(c1.x, c0.y); }
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], rgb(top));
            dl->AddQuad(q[0], q[1], q[2], q[3], rgb(shade(face, 0.55f)), std::max(1.0f, u * 0.03f));
            if (down) dl->AddQuad(q[0], q[1], q[2], q[3], ImGui::GetColorU32(kAccent), std::max(1.5f, u * 0.05f));
            // its chevron, as printed
            const ImVec2 mid((q[0].x + q[1].x + q[2].x + q[3].x) / 4, (q[0].y + q[1].y + q[2].y + q[3].y) / 4);
            const float s = u * 0.16f;
            ImVec2 v[3];
            if (d.shape == PadUp) { v[0] = ImVec2(mid.x - s, mid.y + s * 0.5f); v[1] = ImVec2(mid.x, mid.y - s * 0.5f); v[2] = ImVec2(mid.x + s, mid.y + s * 0.5f); }
            else if (d.shape == PadDown) { v[0] = ImVec2(mid.x - s, mid.y - s * 0.5f); v[1] = ImVec2(mid.x, mid.y + s * 0.5f); v[2] = ImVec2(mid.x + s, mid.y - s * 0.5f); }
            else if (d.shape == PadLeft) { v[0] = ImVec2(mid.x + s * 0.5f, mid.y - s); v[1] = ImVec2(mid.x - s * 0.5f, mid.y); v[2] = ImVec2(mid.x + s * 0.5f, mid.y + s); }
            else { v[0] = ImVec2(mid.x - s * 0.5f, mid.y - s); v[1] = ImVec2(mid.x + s * 0.5f, mid.y); v[2] = ImVec2(mid.x - s * 0.5f, mid.y + s); }
            dl->AddPolyline(v, 3, rgb(pal.colouredLegend), 0, std::max(1.0f, u * 0.035f));
            continue;
        }
        // the skirt, then the top face (lower while the key is down)
        const ImVec2 k0(a.x + gap, a.y + gap), k1(z.x - gap, z.y - gap);
        auto keyRect = [&](ImVec2 p0, ImVec2 p1, ImDrawFlags corners) {
            dl->AddRectFilled(ImVec2(p0.x, p0.y + press), p1, rgb(shade(face, 0.62f)), r, corners);
        };
        auto faceRect = [&](ImVec2 p0, ImVec2 p1, ImDrawFlags corners) {
            dl->AddRectFilled(ImVec2(p0.x + inset, p0.y + inset * 0.55f + press), ImVec2(p1.x - inset, p1.y - inset * 1.3f + press * 0.5f), rgb(top), r, corners);
        };
        ImVec2 t0(k0.x + inset, k0.y + inset * 0.55f + press), t1(k1.x - inset, k1.y - inset * 1.3f + press * 0.5f);
        if (d.shape == LShape) {
            // the upper row whole, the lower row's narrower part under it
            const ImVec2 l0(toScreen(d.lowerX, d.y + 1).x + gap, k0.y + u);
            keyRect(k0, ImVec2(k1.x, k0.y + u + r), ImDrawFlags_RoundCornersTop | ImDrawFlags_RoundCornersBottomLeft);
            keyRect(ImVec2(l0.x, k0.y + u - r), k1, ImDrawFlags_RoundCornersBottom);
            faceRect(k0, ImVec2(k1.x, k0.y + u + inset * 2), ImDrawFlags_RoundCornersTop | ImDrawFlags_RoundCornersBottomLeft);
            faceRect(ImVec2(l0.x, k0.y + u - inset), k1, ImDrawFlags_RoundCornersBottom);
            t0 = ImVec2(l0.x + inset, k0.y + u); t1 = ImVec2(k1.x - inset, k1.y - inset);
        } else {
            keyRect(k0, k1, ImDrawFlags_RoundCornersAll);
            faceRect(k0, k1, ImDrawFlags_RoundCornersAll);
        }
        if (down) dl->AddRect(k0, k1, ImGui::GetColorU32(kAccent), r, 0, std::max(1.5f, u * 0.05f));
        if (keyboardLatched.count(cell)) dl->AddCircleFilled(ImVec2(k1.x - u * 0.17f, k0.y + u * 0.17f), u * 0.06f, ImGui::GetColorU32(kAccent));
        // the legends
        if (d.name) {
            if (!*d.name) continue;          // the space bar
            // arrows and the keypad's legends large; the named keys' words small, as printed
            const bool arrow = (unsigned char)d.name[0] == 0xe2 && std::strlen(d.name) == 3;
            const bool keypad = std::strlen(d.name) == 1 || (d.name[0] == 'f' && std::strlen(d.name) <= 3);
            centred(u * (arrow ? 0.44f : keypad ? 0.36f : 0.19f), t0, t1, ink, d.name);
            continue;
        }
        std::array<std::string, 2> legend = cpcKeyLegend(d.row, d.bit, region);
        // as the CPC prints them: a slashed zero, an arrow on the ^ key
        for (std::string& s : legend) {
            if (s == "0") s = "\xc3\x98";
            else if (s == "^" && d.row == 3 && d.bit == 0) s = UP;   // the UK and Spanish ^ key (the French ^ is a dead key, printed so)
        }
        if (legend[1].empty()) centred(u * 0.38f, t0, t1, ink, legend[0].c_str());
        else {
            // SHIFT's above, the key's own below, as on the keycap
            const float size = u * 0.27f;
            text(size, ImVec2(t0.x + u * 0.1f, t0.y + u * 0.01f), ink, legend[1].c_str());
            text(size, ImVec2(t0.x + u * 0.1f, t1.y - size - u * 0.01f), ink, legend[0].c_str());
        }
    }
    if (over >= 0) {
        const KeyDef& d = keys[(size_t)over];
        ImGui::SetTooltip("Matrix row %d, bit %d%s", d.row, d.bit, blocked ? "\n(a movie is playing: the keys are its own)" : "");
    }
}

} // namespace cpcse
