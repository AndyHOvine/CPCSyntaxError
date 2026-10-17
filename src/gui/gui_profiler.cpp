// CPCSyntaxError GUI — the Profiler window (core/profiler.h).
//
// Record while the program does what is to be measured, then read where the time went:
// by routine (what a CALL, an RST or an interrupt went to: its own time and its time with
// what it called), by instruction, and as a map of the 64K. Its time also shows in the
// Debugger's disassembly, a bar on each line. It records only while it is open and
// recording; closed, the machine does not see it.
#include "gui_shell.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/emulator.h"
#include "core/memory.h"
#include "core/z80.h"

namespace cpcse {

namespace {
constexpr double CYCLES_PER_FRAME = 19968.0 * 4.0;   // one CPC frame, in machine cycles
}

void GuiShell::windowProfiler() {
    bool& open = panelOpen("Profiler");
    // the machine sees the profiler only while this window is open and recording
    if (host.emu) host.emu->profiler = open && profilerRecording && host.booted() ? &profiler : nullptr;
    if (!open) return;
    if (!ImGui::Begin("Profiler", &open)) { ImGui::End(); return; }
    struct EndWindow { ~EndWindow() { ImGui::End(); } } endWindow;   // after the wrap guard below ends
    WrapText wrapText;
    if (profiler.count.empty()) profiler.clear();
    const CodeProfiler& p = profiler;
    {
        FlowRow row;
        if (row.button(profilerRecording ? "Stop" : "Record")) profilerRecording = !profilerRecording;
        if (row.button("Clear")) profiler.clear();
        const double frames = p.totalCycles / CYCLES_PER_FRAME;
        char what[128];
        std::snprintf(what, sizeof what, "%s%.1f s (%.0f frames), %llu instructions", profilerRecording ? "recording: " : "",
                      frames * 0.019968, frames, (unsigned long long)p.instructions);
        row.textDisabled(what);
    }
    if (p.totalCycles == 0) {
        textWrappedDisabled("Press Record, let the program run, then Stop. The time goes to the routine it ran in "
                            "(entered by a CALL, an RST or an interrupt), to each instruction, and to each address.");
        return;
    }
    const double total = (double)p.totalCycles;
    const double frames = total / CYCLES_PER_FRAME;
    // a share of the time, and the microseconds of a frame it is
    auto share = [&](uint64_t c) { return 100.0 * (double)c / total; };
    auto perFrame = [&](uint64_t c) { return (double)c / 4.0 / std::max(frames, 1e-9); };
    auto name = [&](int a) {
        char b[80];
        if (a == CodeProfiler::TOP) return std::string("(no call seen)");
        const auto label = debugger.labelAt.find(a);
        if (label != debugger.labelAt.end()) std::snprintf(b, sizeof b, "&%04X %s", a, label->second.c_str());
        else std::snprintf(b, sizeof b, "&%04X", a);
        return std::string(b);
    };
    if (ImGui::BeginTabBar("##proftabs")) {
        if (ImGui::BeginTabItem("Routines")) {
            struct Row { int entry; uint64_t calls, self, incl; bool interrupt; };
            std::vector<Row> rows;
            rows.reserve(p.routines.size());
            for (const auto& kv : p.routines) rows.push_back({ kv.first, kv.second.calls, kv.second.self, p.inclusiveOf(kv.first), kv.second.interrupt });
            const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                                        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##routines", 6, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Routine", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
                ImGui::TableSetupColumn("Calls");
                ImGui::TableSetupColumn("Self %", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Self us/frame", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Incl %", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Incl us/frame", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableHeadersRow();
                int column = 2; bool ascending = false;
                if (ImGuiTableSortSpecs* s = ImGui::TableGetSortSpecs(); s && s->SpecsCount > 0) {
                    column = s->Specs[0].ColumnIndex; ascending = s->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                }
                std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                    const uint64_t x = column == 1 ? a.calls : column >= 4 ? a.incl : a.self;
                    const uint64_t y = column == 1 ? b.calls : column >= 4 ? b.incl : b.self;
                    return ascending ? x < y : x > y;
                });
                ImGuiListClipper clip;
                clip.Begin((int)rows.size());
                while (clip.Step())
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                        const Row& r = rows[(size_t)i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const std::string n = name(r.entry) + (r.interrupt ? "  (interrupt)" : "");
                        ImGui::PushID(i);
                        if (ImGui::Selectable(n.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && r.entry != CodeProfiler::TOP) {
                            panelOpen("Debugger") = true;
                            showInDisassembly(r.entry);
                        }
                        if (ImGui::IsItemHovered() && r.entry != CodeProfiler::TOP) ImGui::SetTooltip("Click: show it in the disassembly");
                        ImGui::PopID();
                        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)r.calls);
                        ImGui::TableNextColumn(); ImGui::Text("%6.2f", share(r.self));
                        ImGui::TableNextColumn(); ImGui::Text("%8.1f", perFrame(r.self));
                        ImGui::TableNextColumn(); ImGui::Text("%6.2f", share(r.incl));
                        ImGui::TableNextColumn(); ImGui::Text("%8.1f", perFrame(r.incl));
                    }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Instructions")) {
            // the 300 dearest addresses
            std::vector<int> hot;
            for (int a = 0; a < 0x10000; a++) if (p.cycles[(size_t)a]) hot.push_back(a);
            const size_t n = std::min<size_t>(300, hot.size());
            std::partial_sort(hot.begin(), hot.begin() + (std::ptrdiff_t)n, hot.end(), [&](int a, int b) { return p.cycles[(size_t)a] > p.cycles[(size_t)b]; });
            hot.resize(n);
            GX4000* e = host.emu;
            auto read = [e](int a) { return e ? e->memory->readMapped(a & 0xffff) : 0; };
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##hot", 5, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("Instruction (as memory holds it now)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Times");
                ImGui::TableSetupColumn("%");
                ImGui::TableSetupColumn("us/frame");
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < hot.size(); i++) {
                    const int a = hot[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    char addr[8]; std::snprintf(addr, sizeof addr, "&%04X", a);
                    ImGui::PushID(a);
                    if (ImGui::Selectable(addr, false, ImGuiSelectableFlags_SpanAllColumns)) { panelOpen("Debugger") = true; showInDisassembly(a); }
                    ImGui::PopID();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(e ? debugger.disassembler.disassemble(read, a).mnem.c_str() : "");
                    ImGui::TableNextColumn(); ImGui::Text("%u", p.count[(size_t)a]);
                    ImGui::TableNextColumn(); ImGui::Text("%6.2f", share(p.cycles[(size_t)a]));
                    ImGui::TableNextColumn(); ImGui::Text("%8.1f", perFrame(p.cycles[(size_t)a]));
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Map")) {
            // the 64K, 256 bytes a row: black never run, dark red to yellow to white the dearest
            if (profilerMap.size() != 0x10000) profilerMap.assign(0x10000, 0);
            uint64_t most = 1;
            for (uint64_t c : p.cycles) most = std::max(most, c);
            const double top = std::log1p((double)most);
            for (size_t a = 0; a < 0x10000; a++) {
                const uint64_t c = p.cycles[a];
                if (!c) { profilerMap[a] = 0xff000000u; continue; }
                const double t = 0.15 + 0.85 * std::log1p((double)c) / top;
                const int r = (int)std::min(255.0, t * 2.2 * 255), g = (int)std::clamp((t - 0.4) * 2.0 * 255, 0.0, 255.0), b = (int)std::clamp((t - 0.8) * 5.0 * 255, 0.0, 255.0);
                profilerMap[a] = 0xff000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
            }
            if (uploadTexture) profilerTexture = uploadTexture(20, profilerMap.data(), 256, 256);
            const float side = std::max(64.0f, std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing()));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (profilerTexture) ImGui::Image((ImTextureID)(intptr_t)profilerTexture, ImVec2(side, side));
            else ImGui::Dummy(ImVec2(side, side));
            if (ImGui::IsItemHovered()) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const int x = std::clamp((int)((m.x - at.x) / side * 256), 0, 255), y = std::clamp((int)((m.y - at.y) / side * 256), 0, 255);
                const int a = y * 256 + x;
                ImGui::SetTooltip("&%04X: run %u times, %.2f%%\nClick: show it in the disassembly", a, p.count[(size_t)a], share(p.cycles[(size_t)a]));
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { panelOpen("Debugger") = true; showInDisassembly(a); }
            }
            textWrappedDisabled("&0000 top left, &FFFF bottom right, 256 bytes a row.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace cpcse
