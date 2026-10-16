// CPCSyntaxError — the Logic analyser window. See gui_logic.h and core/logic_analyser.h.
#include "gui_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "emuhost.h"
#include "gui_debugger.h"
#include "gui_widgets.h"
#include "core/emulator.h"
#include "core/logic_analyser.h"
#include "core/memory.h"

namespace cpcse {

namespace {

enum class Kind { Digital, Bus, Analog };
struct Row { const char* name; Kind kind; uint16_t flag; int field; bool byDefault; const char* tip; };
// field: 0 mode, 1 C0, 2 C9, 3 C4, 4 R52, 5 MA, 6 PC; for Analog the AY channel
const Row ROWS[] = {
    { "HSYNC", Kind::Digital, LaSample::HSYNC, 0, true, "The CRTC's HSYNC pin" },
    { "VSYNC", Kind::Digital, LaSample::VSYNC, 0, true, "The CRTC's VSYNC pin" },
    { "C-SYNC", Kind::Digital, LaSample::CSYNC, 0, true, "The GATE ARRAY's composite sync, to the monitor" },
    { "DISPEN", Kind::Digital, LaSample::DISPEN, 0, true, "The CRTC's display enable" },
    { "INT", Kind::Digital, LaSample::INT, 0, true, "The Z80's INT line" },
    { "HALT", Kind::Digital, LaSample::HALT, 0, false, "The Z80 is in a HALT" },
    { "MOTOR", Kind::Digital, LaSample::MOTOR, 0, false, "The cassette motor relay" },
    { "TAPE", Kind::Digital, LaSample::TAPE, 0, false, "The cassette input (PPI port B bit 7)" },
    { "FDC INT", Kind::Digital, LaSample::FDCINT, 0, false, "The disc controller has an interrupt pending" },
    { "MODE", Kind::Bus, 0, 0, true, "The GATE ARRAY's graphic mode" },
    { "C0", Kind::Bus, 0, 1, true, "The CRTC's character counter" },
    { "C9", Kind::Bus, 0, 2, true, "The CRTC's raster counter" },
    { "C4", Kind::Bus, 0, 3, true, "The CRTC's row counter" },
    { "R52", Kind::Bus, 0, 4, false, "The GATE ARRAY's interrupt counter" },
    { "MA", Kind::Bus, 0, 5, false, "The CRTC's memory address (the start of its row)" },
    { "PC", Kind::Bus, 0, 6, true, "The address of the instruction the Z80 is in" },
    { "AY A", Kind::Analog, 0, 0, true, "The AY's channel A output (0 to 15)" },
    { "AY B", Kind::Analog, 0, 1, true, "The AY's channel B output (0 to 15)" },
    { "AY C", Kind::Analog, 0, 2, true, "The AY's channel C output (0 to 15)" },
};
constexpr int ROW_COUNT = (int)(sizeof ROWS / sizeof ROWS[0]);

int busValue(const LaSample& s, int field) {
    switch (field) {
        case 0: return s.mode;
        case 1: return s.c0;
        case 2: return s.c9;
        case 3: return s.c4;
        case 4: return s.r52;
        case 5: return s.ma;
        default: return s.pc;
    }
}
bool busWide(int field) { return field >= 5; }   // MA and PC: four hex digits

const char* TRIGGERS[] = { "No trigger", "VSYNC rises", "HSYNC rises", "INT rises", "the mode changes", "the PC is at" };

}   // namespace

LogicAnalyserWindow::LogicAnalyserWindow(EmuHost& h, Debugger& d) : host(h), debugger(d) {
    for (int i = 0; i < ROW_COUNT; i++) rows[i] = ROWS[i].byDefault;
}

void LogicAnalyserWindow::draw(bool* open) {
    focused = false;
    if (!ImGui::Begin("Logic analyser", open)) { ImGui::End(); return; }
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    GX4000* e = host.emu;
    if (!e) { ImGui::TextDisabled("No machine"); ImGui::End(); return; }
    LogicAnalyser& la = e->analyser;

    // ---- the toolbar, on as many rows as the width needs
    {
        FlowRow bar;
        bar.checkbox("Record", &la.enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sample the signals once a microsecond (one CRTC character).\n"
                                                      "It only reads the machine: it runs the same either way.");
        if (bar.button("Clear")) { la.clear(); markA = markB = -1; rightEdge = -1; }
        if (bar.button(rightEdge < 0 ? "Newest: on" : "Newest")) rightEdge = -1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the newest sample at the right edge");
        bar.field(150.0f, "##zoom");
        float zoom = (float)std::log2(usPerPixel);
        char zl[32];
        std::snprintf(zl, sizeof zl, usPerPixel >= 1 ? "%.0f us a pixel" : "%.2f us a pixel", usPerPixel);
        if (ImGui::SliderFloat("##zoom", &zoom, -4.0f, 9.0f, zl)) usPerPixel = std::pow(2.0, (double)zoom);
    }
    {
        FlowRow trig;
        trig.textDisabled("Stop the CPC when");
        trig.field(150.0f, "##trigger");
        triggerIndex = (int)la.trigger;
        if (ImGui::Combo("##trigger", &triggerIndex, TRIGGERS, IM_ARRAYSIZE(TRIGGERS))) {
            la.trigger = (LogicAnalyser::Trigger)triggerIndex;
            la.triggerArmed = la.trigger != LogicAnalyser::Trigger::None;
            if (la.triggerArmed) la.enabled = true;
        }
        if (la.trigger == LogicAnalyser::Trigger::PcEquals) {
            trig.field(60.0f, "##trigpc");
            if (ImGui::InputText("##trigpc", triggerPcText, sizeof triggerPcText, ImGuiInputTextFlags_CharsHexadecimal))
                la.triggerPc = (int)std::strtol(triggerPcText, nullptr, 16) & 0xffff;
        }
        if (la.trigger != LogicAnalyser::Trigger::None) {
            if (trig.button(la.triggerArmed ? "Armed##arm" : "Arm##arm")) { la.triggerArmed = true; la.enabled = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fires once: the CPC stops on the sample, and the view goes to it");
        }
        if (trig.button("Traces...")) ImGui::OpenPopup("##la_rows");
        if (ImGui::BeginPopup("##la_rows")) {
            for (int i = 0; i < ROW_COUNT; i++) ImGui::Checkbox(ROWS[i].name, &rows[i]);
            ImGui::EndPopup();
        }
    }
    const uint64_t total = la.total(), oldest = la.oldest();
    if (total == 0) {
        ImGui::TextWrapped(la.enabled ? "Recording: the CPC is paused, or has not run yet." : "Turn Record on to sample the signals.");
        ImGui::End();
        return;
    }
    // a trigger that fired since: the view goes to it, the marker on it
    if (la.triggeredAt >= 0 && la.triggeredAt != shownTrigger) {
        shownTrigger = la.triggeredAt;
        markA = la.triggeredAt;
        const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x - 80.0f);
        rightEdge = la.triggeredAt + (int64_t)(width * usPerPixel / 2);
        if (host.paused) host.breakReason = "Logic analyser trigger";
    }

    // ---- the traces
    const float lineH = ImGui::GetTextLineHeight();
    const float rowH = lineH + 6.0f, analogH = lineH * 2.0f + 4.0f, rulerH = lineH + 6.0f;
    float height = rulerH;
    for (int i = 0; i < ROW_COUNT; i++) if (rows[i]) height += ROWS[i].kind == Kind::Analog ? analogH : rowH;
    ImGui::BeginChild("##la_canvas", ImVec2(0, std::min(height + 4.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y - lineH * 2.5f))),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float labelW = ImGui::CalcTextSize("FDC INT ").x + 6.0f;
    const float x0 = origin.x + labelW, x1 = origin.x + std::max(labelW + 40.0f, ImGui::GetContentRegionAvail().x);
    const float span = x1 - x0;
    int64_t right = rightEdge < 0 ? (int64_t)total - 1 : rightEdge;
    auto sampleAtX = [&](float x) { return right - (int64_t)std::floor((x1 - x) * usPerPixel); };
    auto xOf = [&](int64_t n) { return x1 - (float)((double)(right - n) / usPerPixel); };
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 cTrace = IM_COL32(110, 220, 120, 255), cBus = IM_COL32(120, 180, 240, 255), cGrid = IM_COL32(255, 255, 255, 28);
    const ImU32 cA = ImGui::GetColorU32(ImVec4(1.0f, 0.75f, 0.2f, 1.0f)), cB = IM_COL32(90, 210, 230, 255);
    // the time reference: the VSYNC rise at or before the left edge (else the oldest sample)
    int64_t left = std::max((int64_t)oldest, sampleAtX(x0));
    int64_t t0 = left;
    for (int64_t n = left; n > (int64_t)oldest && n > left - 40000; n--)
        if ((la.at((uint64_t)n).flags & LaSample::VSYNC) && !(la.at((uint64_t)n - 1).flags & LaSample::VSYNC)) { t0 = n; break; }
    if (markA >= 0) t0 = markA;
    // the ruler: ticks every 1, 2, 5 x 10^k usec, at least 70 pixels apart
    {
        static const double STEPS[3] = { 2.0, 2.5, 2.0 };   // 1, 2, 5, 10, 20, 50 ...
        double step = 1;
        for (int k = 0; step / usPerPixel < 70.0; k++) step *= STEPS[k % 3];
        const int64_t first = (int64_t)std::ceil((double)(sampleAtX(x0) - t0) / step);
        for (int64_t k = first;; k++) {
            const int64_t n = t0 + (int64_t)std::llround((double)k * step);
            const float x = xOf(n);
            if (x > x1) break;
            if (x < x0) continue;
            dl->AddLine(ImVec2(x, origin.y + rulerH - 4), ImVec2(x, origin.y + height), cGrid);
            char t[32];
            std::snprintf(t, sizeof t, "%+lld", (long long)(n - t0));
            dl->AddText(ImVec2(x + 2, origin.y), cDim, t);
        }
        dl->AddText(ImVec2(origin.x, origin.y), cDim, markA >= 0 ? "us (A)" : "us");
    }
    // the rows
    float y = origin.y + rulerH;
    for (int r = 0; r < ROW_COUNT; r++) {
        if (!rows[r]) continue;
        const Row& row = ROWS[r];
        const float h = row.kind == Kind::Analog ? analogH : rowH;
        const float top = y + 3, bottom = y + h - 3;
        dl->AddText(ImVec2(origin.x, y + (h - lineH) * 0.5f), cText, row.name);
        // column by column: the samples each pixel covers
        float runStart = x0;
        int runValue = -1;
        bool runBusy = false;
        auto flushBus = [&](float xEnd) {
            if (runValue < 0 || xEnd <= runStart) return;
            if (runBusy) dl->AddRectFilled(ImVec2(runStart, top), ImVec2(xEnd, bottom), IM_COL32(120, 180, 240, 90));
            else {
                dl->AddLine(ImVec2(runStart + 1, top), ImVec2(xEnd - 1, top), cBus);
                dl->AddLine(ImVec2(runStart + 1, bottom), ImVec2(xEnd - 1, bottom), cBus);
                dl->AddLine(ImVec2(runStart, (top + bottom) * 0.5f), ImVec2(runStart + 1, top), cBus);
                dl->AddLine(ImVec2(runStart, (top + bottom) * 0.5f), ImVec2(runStart + 1, bottom), cBus);
                char v[8];
                std::snprintf(v, sizeof v, busWide(row.field) ? "%04X" : "%02X", runValue);
                const float tw = ImGui::CalcTextSize(v).x;
                if (xEnd - runStart > tw + 6) dl->AddText(ImVec2((runStart + xEnd - tw) * 0.5f, top + (bottom - top - lineH) * 0.5f), cText, v);
            }
        };
        float prevLevelY = -1;
        for (float x = x0; x < x1; x += 1.0f) {
            int64_t a = sampleAtX(x), b = sampleAtX(x + 1.0f);
            if (b < a) b = a;
            if (b < (int64_t)oldest || a >= (int64_t)total) { if (row.kind == Kind::Bus) { flushBus(x); runValue = -1; } prevLevelY = -1; continue; }
            a = std::max(a, (int64_t)oldest); b = std::min(b, (int64_t)total - 1);
            if (row.kind == Kind::Digital) {
                bool hi = false, lo = false;
                for (int64_t n = a; n <= b && !(hi && lo); n++) ((la.at((uint64_t)n).flags & row.flag) ? hi : lo) = true;
                if (hi && lo) { dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), cTrace); prevLevelY = -1; continue; }
                const float ly = hi ? top : bottom;
                if (prevLevelY >= 0 && prevLevelY != ly) dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), cTrace);
                dl->AddLine(ImVec2(x, ly), ImVec2(x + 1.0f, ly), cTrace);
                prevLevelY = ly;
            } else if (row.kind == Kind::Analog) {
                int mn = 15, mx = 0;
                for (int64_t n = a; n <= b; n++) { const int v = la.at((uint64_t)n).ay[row.field]; mn = std::min(mn, v); mx = std::max(mx, v); }
                auto yv = [&](int v) { return bottom - (bottom - top) * (float)v / 15.0f; };
                dl->AddLine(ImVec2(x + 0.5f, yv(mx)), ImVec2(x + 0.5f, yv(mn) + 1.0f), cTrace);
            } else {
                const int v = busValue(la.at((uint64_t)a), row.field);
                bool changes = false;
                for (int64_t n = a + 1; n <= b && !changes; n++) changes = busValue(la.at((uint64_t)n), row.field) != v;
                if (changes) {
                    if (!runBusy) { flushBus(x); runStart = x; runBusy = true; runValue = v; }
                } else if (runBusy || v != runValue) {
                    flushBus(x); runStart = x; runBusy = false; runValue = v;
                }
            }
        }
        if (row.kind == Kind::Bus) flushBus(x1);
        dl->AddLine(ImVec2(origin.x, y + h), ImVec2(x1, y + h), cGrid);
        if (ImGui::IsMouseHoveringRect(ImVec2(origin.x, y), ImVec2(x0, y + h))) ImGui::SetTooltip("%s", row.tip);
        y += h;
    }
    // the markers
    for (int k = 0; k < 2; k++) {
        const int64_t m = k == 0 ? markA : markB;
        if (m < 0) continue;
        const float x = xOf(m);
        if (x >= x0 && x <= x1) dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), k == 0 ? cA : cB, 1.5f);
    }
    // the mouse: hover = the cursor and its values; wheel = zoom; drag = move; click = A,
    // Shift+click = B
    ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
    ImGui::InvisibleButton("##la_area", ImVec2(std::max(1.0f, span), std::max(1.0f, height)));
    if (ImGui::IsItemHovered()) {
        const ImGuiIO& io = ImGui::GetIO();
        const float mx = io.MousePos.x;
        const int64_t n = sampleAtX(mx);
        if (io.MouseWheel != 0) {
            const double next = std::clamp(usPerPixel * (io.MouseWheel > 0 ? 0.8 : 1.25), 1.0 / 16, 512.0);
            rightEdge = n + (int64_t)std::llround((x1 - mx) * next);
            usPerPixel = next;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
            rightEdge = right - (int64_t)std::llround(io.MouseDelta.x * usPerPixel);
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && io.MouseDragMaxDistanceSqr[0] < 9.0f) {
            (io.KeyShift ? markB : markA) = n;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) markA = markB = -1;
        if (la.has((uint64_t)std::max<int64_t>(0, n)) && n >= 0) {
            dl->AddLine(ImVec2(mx, origin.y), ImVec2(mx, origin.y + height), IM_COL32(255, 255, 255, 110));
            const LaSample& s = la.at((uint64_t)n);
            auto read = [e](int addr) { return e->memory->readMapped(addr & 0xffff); };
            const DisasmResult d = debugger.disassembler.disassemble(read, s.pc);
            ImGui::BeginTooltip();
            ImGui::Text("%+lld us%s", (long long)(n - t0), markA >= 0 ? " from A" : " from VSYNC");
            ImGui::Text("C0 %02X  C9 %02X  C4 %02X  MA %04X  R52 %d  mode %d", s.c0, s.c9, s.c4, s.ma, s.r52, s.mode);
            std::string on;
            for (int r = 0; r < 9; r++) if (s.flags & ROWS[r].flag) { on += ROWS[r].name; on += ' '; }
            ImGui::Text("high: %s", on.empty() ? "(none)" : on.c_str());
            ImGui::Text("AY  A %d  B %d  C %d", s.ay[0], s.ay[1], s.ay[2]);
            ImGui::Text("PC &%04X  %s", s.pc, d.mnem.c_str());
            ImGui::TextDisabled("(the instruction as memory holds it now)");
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    // ---- below the traces: what the markers measure
    {
        char info[160];
        const double ms = (double)(total - oldest) / 1000.0;
        if (markA >= 0 && markB >= 0)
            std::snprintf(info, sizeof info, "A to B: %lld us (%.2f lines of 64 us)   |   %.1f ms recorded",
                          (long long)(markB - markA), (double)(markB - markA) / 64.0, ms);
        else std::snprintf(info, sizeof info, "%.1f ms recorded. Click: marker A. Shift+click: marker B. Right-click: no markers. Wheel: zoom. Drag: move.", ms);
        ImGui::TextDisabled("%s", info);
    }
    ImGui::End();
}

} // namespace cpcse
