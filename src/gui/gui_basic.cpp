// CPCSyntaxError GUI — the BASIC window. See gui_basic.h.
#include "gui_basic.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "imgui.h"
#include "emuhost.h"
#include "gui_widgets.h"
#include "core/emulator.h"
#include "core/memory.h"

namespace cpcse {

BasicWindow::BasicWindow(EmuHost& h, FileBrowser& b, SaveDialog& s) : host(h), browser(b), saver(s) {}

bool BasicWindow::fromMachine() {
    int end = 0;
    bool basic10 = false;
    if (!host.basicProgramEnd(end, basic10)) {
        message = "No BASIC program to take: BASIC is not at its Ready prompt.";
        error = true;
        return false;
    }
    const Bytes& ram = host.emu->memory->ram;
    std::string text;
    for (const BasicLine& l : basic.listProgram([&](int a) { return (int)ram[(size_t)(a & 0xffff)]; }, BASIC_PROGRAM_START))
        text += std::to_string(l.num) + " " + l.text + "\n";
    source = text;
    message = text.empty() ? "The machine has no program (BASIC " + std::string(basic10 ? "1.0" : "1.1") + ")."
                           : "Taken from the machine (BASIC " + std::string(basic10 ? "1.0" : "1.1") + ", " +
                                 std::to_string(end - BASIC_PROGRAM_START) + " bytes).";
    error = false;
    tabRequest = 0;
    return true;
}

bool BasicWindow::intoMachine(bool thenRun) {
    std::string why;
    const Bytes program = basic.tokenizeProgram(source, why);
    if (program.empty()) { message = "Not put in: " + why; error = true; return false; }
    if (!host.injectBasic(program, why)) { message = "Not put in: " + why; error = true; return false; }
    if (thenRun) host.typeText("RUN\n");
    message = "In the machine: " + std::to_string(program.size()) + " bytes" + (thenRun ? ", RUN typed." : ". Type RUN to start it.");
    error = false;
    return true;
}

void BasicWindow::newFile() {
    source.clear();
    path.clear();
    message = "A new program.";
    error = false;
}

void BasicWindow::openFile(const std::string& p) {
    std::ifstream f(std::filesystem::u8path(p), std::ios::binary);
    if (!f) { message = "Cannot open " + p; error = true; return; }
    std::stringstream ss;
    ss << f.rdbuf();
    source = ss.str();
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    path = p;
    message = "Opened " + p;
    error = false;
    tabRequest = 0;
}

void BasicWindow::saveFile(const std::string& p) {
    std::string why;
    if (!writeFileSafely(p, source.data(), source.size(), &why)) { message = "Not saved: " + why; error = true; return; }
    path = p;
    message = "Saved " + p;
    error = false;
}

void BasicWindow::requestOpen() {
    const std::string dir = path.empty() ? host.romDir : std::filesystem::u8path(path).parent_path().u8string();
    browser.open("Open a BASIC program (text)", dir, { ".bas", ".txt" }, [this](const std::string& p) { openFile(p); });
}

void BasicWindow::requestSave(bool saveAs) {
    if (!saveAs && !path.empty()) { saveFile(path); return; }
    const std::string dir = path.empty() ? host.romDir : std::filesystem::u8path(path).parent_path().u8string();
    const std::string name = path.empty() ? std::string("program.bas") : std::filesystem::u8path(path).filename().u8string();
    saver.open("Save the BASIC program (text)", dir, name, [this](const std::string& p) { saveFile(p); });
}

void BasicWindow::loadSettings(const std::map<std::string, std::string>& ini) {
    auto it = ini.find("basic_path");
    if (it != ini.end()) path = it->second;
}

void BasicWindow::saveSettings(std::ostream& out) const {
    if (!path.empty()) out << "basic_path=" << path << "\n";
}

static int sourceCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize((size_t)data->BufTextLen);
        data->Buf = s->data();
    }
    return 0;
}

void BasicWindow::toolbar() {
    FlowRow row;
    if (row.button("New")) newFile();
    if (row.button("Open...")) requestOpen();
    if (row.button("Save...")) requestSave(true);
    row.spacing = 16.0f;
    const bool ready = host.booted();
    ImGui::BeginDisabled(!ready);
    if (row.button("From the machine")) fromMachine();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The program in the machine's memory, listed into the editor.");
    row.spacing = -1.0f;
    if (row.button("Into the machine")) intoMachine(false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The editor's program into the machine's memory, at once, as if it had been typed.\n"
                          "It takes the place of the program there. BASIC must be at its Ready prompt.");
    if (row.button("Into the machine and RUN")) intoMachine(true);
    ImGui::EndDisabled();
}

void BasicWindow::editorTab() {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize;
    ImGui::InputTextMultiline("##basicsrc", source.data(), source.capacity() + 1, ImVec2(-1, -ImGui::GetFrameHeightWithSpacing()),
                              flags, sourceCallback, &source);
    focused = focused || ImGui::IsItemActive();
    ImGui::TextDisabled("One numbered line a line; keywords in any case. Lines are sorted by number.");
}

void BasicWindow::memoryTab() {
    int end = 0;
    bool basic10 = false;
    if (!host.booted() || !host.basicProgramEnd(end, basic10)) {
        ImGui::TextDisabled("BASIC is not at its Ready prompt: no program to list.");
        return;
    }
    const Bytes& ram = host.emu->memory->ram;
    const std::vector<BasicLine> lines = basic.listProgram([&](int a) { return (int)ram[(size_t)(a & 0xffff)]; }, BASIC_PROGRAM_START);
    ImGui::TextDisabled("BASIC %s, %d bytes, %zu lines (live).", basic10 ? "1.0" : "1.1", end - BASIC_PROGRAM_START, lines.size());
    if (ImGui::BeginTable("##basiclist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("at", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("&0000").x);
        ImGui::TableSetupColumn("line", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("65535").x);
        ImGui::TableSetupColumn("text");
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)lines.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const BasicLine& l = lines[(size_t)i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextDisabled("&%04X", l.addr);
                ImGui::TableNextColumn(); ImGui::Text("%d", l.num);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(l.text.c_str());
            }
        ImGui::EndTable();
    }
}

void BasicWindow::draw(bool* open) {
    focused = false;
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("BASIC", open)) { ImGui::End(); return; }
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    {
        WrapText wrapText;                     // text wraps at the window's edge
        toolbar();
        if (error) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", message.c_str());
        else ImGui::TextDisabled("%s", message.c_str());
        if (ImGui::BeginTabBar("##basictabs")) {
            if (ImGui::BeginTabItem("Editor", nullptr, tabRequest == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { editorTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("In memory", nullptr, tabRequest == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { memoryTab(); ImGui::EndTabItem(); }
            tabRequest = -1;
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace cpcse
