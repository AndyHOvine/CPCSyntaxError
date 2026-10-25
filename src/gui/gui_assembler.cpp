// CPCSyntaxError GUI — the RASM editor window. See gui_assembler.h.
#include "gui_assembler.h"

#include "imgui.h"
#include "imgui_internal.h"   // the editor's child window, for the line-number gutter

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "emuhost.h"
#include "gui_debugger.h"
#include "gui_widgets.h"
#include "rasm_bridge.h"
#include "core/emulator.h"
#include "core/memory.h"
#include "core/z80.h"

extern "C" {
// Only the two result structures are needed from rasm.h; its s_parameter stays in C.
struct s_debug_error { char* filename; int line; char* msg; int lenmsg, lenfilename; };
struct s_debug_symbol { char* name; int v; };
struct s_rasm_info {
    struct s_debug_error* error; int nberror, maxerror, warnerr;
    struct s_debug_symbol* symbol; int nbsymbol, maxsymbol;
    int run, start;
    unsigned char* emuram; int lenram;
    unsigned char* emurom; int lenrom;
};
}

namespace cpcse {

static const char* SAMPLE_SOURCE =
    "; CPCSyntaxError -- the built-in assembler is RASM.\n"
    "; F9 assembles into the machine's memory, Ctrl+F9 also jumps to it.\n"
    "; Labels named BRK... become breakpoints (as rasm -eb).\n"
    "\n"
    "        org #4000\n"
    "        run start\n"
    "\n"
    "start:  ld hl,message\n"
    ".next:  ld a,(hl)\n"
    "        or a\n"
    "        jr z,.done\n"
    "        call #bb5a          ; TXT OUTPUT\n"
    "        inc hl\n"
    "        jr .next\n"
    ".done:  jr .done\n"
    "\n"
    "message: defb \"Hello from RASM!\",13,10,0\n";

static const char* SYNTAX_NAMES[] = { "RASM", "Maxam (-m)", "AS80 (-ass)", "UZ80 (-uz)", "DAMS (-dams)", "Pasmo (-pasmo)" };

AssemblerWindow::AssemblerWindow(EmuHost& h, Debugger& d, FileBrowser& b, SaveDialog& s)
    : host(h), debugger(d), browser(b), saver(s) {
    newFile();
}

std::string AssemblerWindow::displayName() const {
    std::string n = path.empty() ? std::string("untitled.asm") : std::filesystem::path(path).filename().string();
    return modified ? n + " *" : n;
}

static int countLines(const std::string& s) { return 1 + (int)std::count(s.begin(), s.end(), '\n'); }

static bool readText(const std::string& p, std::string& text) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    text = ss.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return true;
}

static std::string canonical(const std::string& p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(std::filesystem::path(p), ec);
    std::string s = (ec ? std::filesystem::path(p) : c).generic_string();
#if defined(_WIN32)
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
#endif
    return s;
}

void AssemblerWindow::newFile() {
    source = SAMPLE_SOURCE;
    sourceRevision++;
    path.clear();
    modified = false;
    lineCount = countLines(source);
    tabs.assign(1, Tab{});
    activeTab = 0; selectTab = 0;
    messages.clear(); errorLines.clear();
}

// Opening a file makes it the main file: its tab first, then a tab for each file it links,
// and each of theirs.
void AssemblerWindow::openFile(const std::string& p) {
    std::string text;
    if (!readText(p, text)) { summary = "Cannot open " + p; return; }
    source = text;
    sourceRevision++;
    path = p;
    modified = false;
    lineCount = countLines(source);
    tabs.assign(1, Tab{});
    activeTab = 0; selectTab = 0;
    messages.clear(); errorLines.clear();
    openLinked(source, path, 0);
    summary = "Opened " + p;
    if (tabs.size() > 1) summary += " and " + std::to_string(tabs.size() - 1) + " linked file" + (tabs.size() == 2 ? "" : "s");
}

void AssemblerWindow::saveFile(const std::string& p) {
    std::string why;
    if (!writeFileSafely(p, source.data(), source.size(), &why)) { summary = "Not saved: " + why; return; }
    path = p;
    modified = false;
    summary = "Saved " + p;
    if (activeTab == 0) openLinked(source, path, 0);     // a new INCLUDE opens its file
}

// The files a source links: INCLUDE "name" and READ "name" (rasm's two for source; INCBIN
// is binary and gets no tab). A name is looked for beside the file that names it, then
// beside the main file, because rasm assembles in the main file's folder.
std::vector<std::string> AssemblerWindow::linkedFiles(const std::string& text, const std::string& fromPath) const {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    const fs::path fromDir = fromPath.empty() ? fs::path() : fs::path(fromPath).parent_path();
    const std::string mainPath = activeTab == 0 ? path : (tabs.empty() ? std::string() : tabs[0].path);
    const fs::path mainDir = mainPath.empty() ? fs::path() : fs::path(mainPath).parent_path();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // the code part of the line: up to a ; or // outside quotes
        std::string code;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); i++) {
            const char c = line[i];
            if (c == '"') quoted = !quoted;
            if (!quoted && (c == ';' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/'))) break;
            code += c;
        }
        std::string upper = code;
        for (char& c : upper) c = (char)std::toupper((unsigned char)c);
        for (const char* word : { "INCLUDE", "READ" }) {
            const size_t wordLength = std::string(word).size();
            size_t at = upper.find(word);
            while (at != std::string::npos) {
                const size_t end = at + wordLength;
                const bool startOk = at == 0 || !(std::isalnum((unsigned char)upper[at - 1]) || upper[at - 1] == '_' || upper[at - 1] == '.');
                const bool endOk = end >= upper.size() || std::isspace((unsigned char)upper[end]) || upper[end] == '"';
                if (startOk && endOk) {
                    const size_t q1 = code.find('"', end);
                    const size_t q2 = q1 == std::string::npos ? q1 : code.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        const std::string name = code.substr(q1 + 1, q2 - q1 - 1);
                        std::error_code ec;
                        fs::path candidate = fromDir / name;
                        if (!fs::exists(candidate, ec)) candidate = mainDir / name;
                        if (fs::is_regular_file(candidate, ec)) found.push_back(candidate.string());
                    }
                }
                at = upper.find(word, at + 1);
            }
        }
    }
    return found;
}

void AssemblerWindow::openLinked(const std::string& text, const std::string& fromPath, int depth) {
    if (depth > 16) return;                              // a file that includes itself
    const std::string mainPath = activeTab == 0 ? path : tabs[0].path;
    for (const std::string& file : linkedFiles(text, fromPath)) {
        if (!mainPath.empty() && canonical(file) == canonical(mainPath)) continue;
        if (tabFor(file) >= 0) continue;
        Tab tab;
        if (!readText(file, tab.source)) continue;
        tab.path = file;
        tab.lineCount = countLines(tab.source);
        tabs.push_back(tab);
        const std::string linkedText = tab.source;
        openLinked(linkedText, file, depth + 1);
    }
}

int AssemblerWindow::tabFor(const std::string& file) const {
    namespace fs = std::filesystem;
    if (file.empty()) return -1;
    std::vector<std::string> wanted = { canonical(file) };
    const std::string mainPath = activeTab == 0 ? path : tabs[0].path;
    if (!mainPath.empty() && fs::path(file).is_relative()) wanted.push_back(canonical((fs::path(mainPath).parent_path() / file).string()));
    for (size_t i = 0; i < tabs.size(); i++) {
        const std::string p = (int)i == activeTab ? path : tabs[i].path;
        if (p.empty()) continue;
        const std::string c = canonical(p);
        for (const std::string& w : wanted) if (c == w) return (int)i;
    }
    return -1;
}

void AssemblerWindow::storeActive() {
    if (activeTab < 0 || activeTab >= (int)tabs.size()) return;
    Tab& t = tabs[(size_t)activeTab];
    t.source = source; t.path = path; t.modified = modified; t.lineCount = lineCount;
}

void AssemblerWindow::showTab(int index) {
    if (index < 0 || index >= (int)tabs.size() || index == activeTab) return;
    storeActive();
    // an editor with the focus keeps its own copy of the text and would write it back over
    // the new tab's
    if (ImGui::GetCurrentContext()) ImGui::ClearActiveID();
    activeTab = index;
    const Tab& t = tabs[(size_t)index];
    source = t.source; path = t.path; modified = t.modified; lineCount = t.lineCount;
    sourceRevision++;
    refreshErrorLines();
}

// The gutter marks the errors of the tab on show.
void AssemblerWindow::refreshErrorLines() {
    errorLines.clear();
    for (const Message& m : messages) if (m.tab == activeTab && m.line > 0) errorLines.insert(m.line);
}

void AssemblerWindow::requestOpen() {
    std::string dir = path.empty() ? host.romDir : std::filesystem::path(path).parent_path().string();
    browser.open("Open source", dir, { ".asm", ".z80", ".s", ".txt", ".inc", ".mac" },
                 [this](const std::string& p) { openFile(p); });
}

void AssemblerWindow::requestSave(bool saveAs) {
    if (!saveAs && !path.empty()) { saveFile(path); return; }
    std::string dir = path.empty() ? host.romDir : std::filesystem::path(path).parent_path().string();
    std::string name = path.empty() ? std::string("program.asm") : std::filesystem::path(path).filename().string();
    saver.open("Save source", dir, name, [this](const std::string& p) { saveFile(p); });
}

void AssemblerWindow::loadSettings(const std::map<std::string, std::string>& ini) {
    auto it = ini.find("asm_syntax");
    if (it != ini.end()) syntax = std::clamp(std::atoi(it->second.c_str()), 0, 5);
    it = ini.find("asm_brk");
    if (it != ini.end()) brkLabels = std::atoi(it->second.c_str()) != 0;
    it = ini.find("asm_highlight");
    if (it != ini.end()) highlight = std::atoi(it->second.c_str()) != 0;
    for (size_t k = 0; k < colours.size(); k++) {
        it = ini.find(std::string("asm_colour_") + asmTokenKey((AsmToken)k));
        if (it == ini.end() || it->second.size() != 6) continue;
        char* end = nullptr;
        const unsigned long v = std::strtoul(it->second.c_str(), &end, 16);
        if (end && !*end) colours[k] = (uint32_t)v & 0xffffff;
    }
    it = ini.find("asm_file");
    if (it != ini.end() && !it->second.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(it->second, ec)) openFile(it->second);
    }
}

void AssemblerWindow::saveSettings(std::ostream& out) const {
    out << "asm_syntax=" << syntax << "\n";
    out << "asm_brk=" << (brkLabels ? 1 : 0) << "\n";
    out << "asm_file=" << (activeTab == 0 || tabs.empty() ? path : tabs[0].path) << "\n";   // the main file: it brings its links
    out << "asm_highlight=" << (highlight ? 1 : 0) << "\n";
    for (size_t k = 0; k < colours.size(); k++) {
        char hex[8];
        std::snprintf(hex, sizeof hex, "%06X", (unsigned)colours[k] & 0xffffff);
        out << "asm_colour_" << asmTokenKey((AsmToken)k) << "=" << hex << "\n";
    }
}

// Where each line starts, and whether it starts inside /* */ -- again only when the text
// has changed.
void AssemblerWindow::indexLines() {
    if (indexedRevision == sourceRevision) return;
    indexedRevision = sourceRevision;
    lineStarts.assign(1, 0);
    lineInComment.assign(1, 0);
    bool inComment = false;
    size_t start = 0;
    for (;;) {
        const size_t nl = source.find('\n', start);
        const std::string line = source.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        highlightAsmLine(line, inComment);
        if (nl == std::string::npos) break;
        start = nl + 1;
        lineStarts.push_back((int)start);
        lineInComment.push_back(inComment ? 1 : 0);
    }
}

// The visible lines' spans, drawn where the text box drew its (invisible) text: the same
// font, the same origin, its scroll; each span's x is the width of the line before it, as
// the box measures (tabs included).
void AssemblerWindow::drawHighlighted(const char* childName, float lineH, float editorH) {
    ImGuiWindow* child = ImGui::FindWindowByName(childName);
    if (!child) return;
    indexLines();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 frame = ImGui::GetItemRectMin();
    const ImVec2 origin(frame.x + style.FramePadding.x - child->Scroll.x, frame.y + style.FramePadding.y - child->Scroll.y);
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    ImDrawList* dl = child->DrawList;
    dl->PushClipRect(child->InnerClipRect.Min, child->InnerClipRect.Max, true);
    const int lines = (int)lineStarts.size();
    const int first = std::max(0, (int)(child->Scroll.y / lineH) - 1);
    const int last = std::min(lines, first + (int)(editorH / lineH) + 3);
    ImU32 col[(size_t)AsmToken::Count];
    for (size_t k = 0; k < colours.size(); k++) {
        const uint32_t c = colours[k];
        col[k] = IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
    }
    for (int l = first; l < last; l++) {
        const int a = lineStarts[(size_t)l];
        const int b = l + 1 < lines ? lineStarts[(size_t)l + 1] - 1 : (int)source.size();
        if (b <= a) continue;
        const std::string line = source.substr((size_t)a, (size_t)(b - a));
        bool inComment = lineInComment[(size_t)l] != 0;
        const float y = origin.y + (float)l * lineH;
        float x = origin.x;
        for (const AsmSpan& s : highlightAsmLine(line, inComment)) {
            const char* begin = line.data() + s.start;
            const char* end = begin + s.length;
            dl->AddText(font, size, ImVec2(x, y), col[(size_t)s.kind], begin, end);
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
        }
    }
    dl->PopClipRect();
}

void AssemblerWindow::coloursPopup() {
    if (!ImGui::BeginPopup("Code colours")) return;
    ImGui::Checkbox("Colour the code", &highlight);
    ImGui::Separator();
    ImGui::BeginDisabled(!highlight);
    for (size_t k = 0; k < colours.size(); k++) {
        const uint32_t c = colours[k];
        float rgb[3] = { ((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f };
        ImGui::PushID((int)k);
        if (ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_NoInputs))
            colours[k] = (uint32_t)(rgb[0] * 255.0f + 0.5f) << 16 | (uint32_t)(rgb[1] * 255.0f + 0.5f) << 8 | (uint32_t)(rgb[2] * 255.0f + 0.5f);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f), "%s", asmTokenLabel((AsmToken)k));
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::Button("Defaults")) colours = defaultAsmColours();
    ImGui::EndPopup();
}


// A message as rasm stored it: `length` bytes (it is not reliably terminated), with
// terminal colour codes, and sometimes a second line quoting the source. Kept: the
// printable text, lines joined with " | ".
static std::string cleanMessage(const char* raw, int length) {
    std::string out;
    for (int i = 0; raw && i < length && raw[i]; i++) {
        char ch = raw[i];
        if (ch == 0x1b) { while (i < length && raw[i] && raw[i] != 'm') i++; continue; }
        if (ch == '\n' || ch == '\r') { if (!out.empty() && out.back() != ' ') out += " | "; continue; }
        if ((unsigned char)ch < 0x20 || (unsigned char)ch >= 0x7f) continue;
        out += ch;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '|' || out.back() == ':')) out.pop_back();
    return out;
}

void AssemblerWindow::assemble(bool thenRun) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // rasm reads the linked files from disk: an edited tab is saved first, so it assembles
    // what is on show. The main file goes in from its tab, saved or not.
    storeActive();
    std::string notSaved;
    for (size_t i = 1; i < tabs.size(); i++) {
        Tab& t = tabs[i];
        if (!t.modified || t.path.empty()) continue;
        std::string why;
        if (writeFileSafely(t.path, t.source.data(), t.source.size(), &why)) {
            t.modified = false;
            if ((int)i == activeTab) modified = false;
        } else notSaved += " " + fs::path(t.path).filename().string();
    }
    const std::string mainSource = activeTab == 0 ? source : tabs[0].source;
    const std::string mainPath = activeTab == 0 ? path : tabs[0].path;
    // INCLUDE / INCBIN / SAVE resolve against the main file's own folder, as from the command line.
    fs::path previous = fs::current_path(ec);
    if (!mainPath.empty()) fs::current_path(fs::path(mainPath).parent_path(), ec);

    unsigned char* out = nullptr; int outLength = 0; s_rasm_info* info = nullptr; int fatal = 0;
    int ret = rasm_bridge_assemble(mainSource.data(), (int)mainSource.size(), syntax, &out, &outLength, &info, &fatal);
    fs::current_path(previous, ec);
    openLinked(mainSource, mainPath, 0);                // a file linked since it was opened

    messages.clear(); errorLines.clear(); symbols.clear();
    lastOk = false; lastStart = -1; lastRun = -1; lastLength = 0;
    if (info) {
        for (int i = 0; i < info->nberror; i++) {
            Message m;
            m.line = info->error[i].line;
            m.file = info->error[i].filename ? info->error[i].filename : "";
            m.text = cleanMessage(info->error[i].msg, info->error[i].lenmsg);
            // The editor's text is fed to rasm as a stream; its "file" is the current
            // directory. Anything else came from an INCLUDE.
            m.inEditor = m.file.empty() || m.file == "<internal>" || m.file.back() == '/' || m.file.back() == '\\';
            m.tab = m.inEditor ? 0 : tabFor(m.file);
            messages.push_back(m);
        }
        for (int i = 0; i < info->nbsymbol; i++)
            if (info->symbol[i].name) symbols.emplace_back(info->symbol[i].name, info->symbol[i].v);
        lastRun = info->run;
        lastStart = info->start;
    }
    if (fatal) {
        Message m; m.text = "rasm stopped (fatal error " + std::to_string(fatal) + ")"; messages.push_back(m);
    }
    if (!notSaved.empty()) { Message m; m.text = "Could not save before assembling:" + notSaved; messages.push_back(m); }
    refreshErrorLines();
    lastLength = outLength;

    if (ret == 0 && !fatal) {
        lastOk = true;
        char b[160];
        if (outLength > 0 && host.booted() && host.emu && host.emu->memory) {
            for (int i = 0; i < outLength; i++) host.emu->memory->write((lastStart + i) & 0xffff, out[i]);
            host.timelineEdited();
            std::snprintf(b, sizeof(b), "Assembled %d bytes, loaded at &%04X-&%04X", outLength,
                          lastStart & 0xffff, (lastStart + outLength - 1) & 0xffff);
        } else if (outLength > 0) {
            std::snprintf(b, sizeof(b), "Assembled %d bytes at &%04X (no machine to load them into)", outLength, lastStart & 0xffff);
        } else {
            std::snprintf(b, sizeof(b), "Assembled; no binary output (snapshot/cartridge builds write their files)");
        }
        summary = b;
        debugger.setSymbols(symbols);
        if (brkLabels) {
            debugger.clearAssemblerBreakpoints();
            for (const auto& s : symbols) {
                std::string u = s.first;
                for (char& c : u) c = (char)std::toupper((unsigned char)c);
                bool brk = (u.rfind("BRK", 0) == 0 && u.find('.', 3) == std::string::npos) ||
                           u.rfind("@BRK", 0) == 0 || u.find(".BRK") != std::string::npos;
                if (brk) debugger.addBreakpoint(s.second, "", true);
            }
        }
        if (thenRun && outLength > 0 && host.booted()) {
            int entry = lastRun >= 0 ? lastRun : lastStart;
            debugger.setPc(entry);
            debugger.run();
            std::snprintf(b, sizeof(b), "  -- running from &%04X", entry & 0xffff);
            summary += b;
        }
    } else {
        int errors = 0;
        for (const auto& m : messages) if (!m.text.empty()) errors++;
        summary = "Assembly failed: " + std::to_string(errors) + " message" + (errors == 1 ? "" : "s");
    }
    rasm_bridge_free(out, info);
}

int AssemblerWindow::editorCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<AssemblerWindow*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        self->source.resize((size_t)data->BufTextLen);
        data->Buf = self->source.data();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && self->gotoLine > 0) {
        int start = 0, l = 1;
        for (int i = 0; i < data->BufTextLen && l < self->gotoLine; i++) if (data->Buf[i] == '\n') { l++; start = i + 1; }
        int end = start;
        while (end < data->BufTextLen && data->Buf[end] != '\n') end++;
        data->CursorPos = start;
        data->SelectionStart = start;
        data->SelectionEnd = end;
        self->gotoLine = -1;
    }
    return 0;
}

void AssemblerWindow::draw(bool* open) {
    focused = false;
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Assembler", open)) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(0.0f);             // text wraps at the window's edge
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiIO& io = ImGui::GetIO();
    if (focused) {
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) assemble(io.KeyCtrl);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) requestSave(io.KeyShift);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) requestOpen();
    }

    // ---- toolbar
    if (ImGui::Button("New")) newFile();
    if (after().button("Open...")) requestOpen();
    if (after().button("Save")) requestSave(false);
    if (after().button("Save as...")) requestSave(true);
    after().textDisabled("|");
    if (after().button("Assemble (F9)")) assemble(false);
    if (after().button("Assemble + run (Ctrl+F9)")) assemble(true);
    after().field(130, "##syntax");
    ImGui::Combo("##syntax", &syntax, SYNTAX_NAMES, IM_ARRAYSIZE(SYNTAX_NAMES));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("rasm's compatibility switches");
    after().checkbox("BRK labels", &brkLabels);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Labels named BRK..., @BRK... or ....BRK become breakpoints (rasm -eb)");
    if (after().button("Colours...")) ImGui::OpenPopup("Code colours");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Syntax colouring: on or off, and a colour for each kind of word");
    coloursPopup();
    {
        FlowRow file;
        file.textColored(kAccent, displayName().c_str());
        char n[32];
        std::snprintf(n, sizeof n, "%d lines", lineCount);
        file.textDisabled(n);
    }

    // ---- the open files, a tab each (the first is the main file, the one assembled)
    if (selectTab >= 0) showTab(selectTab);
    if (ImGui::BeginTabBar("##asmfiles", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < (int)tabs.size(); i++) {
            const std::string& p = i == activeTab ? path : tabs[(size_t)i].path;
            const bool changed = i == activeTab ? modified : tabs[(size_t)i].modified;
            std::string label = p.empty() ? std::string("untitled.asm") : std::filesystem::path(p).filename().string();
            if (changed) label += " *";
            label += "###asmtab" + std::to_string(i);
            const ImGuiTabItemFlags flags = selectTab == i ? ImGuiTabItemFlags_SetSelected : 0;
            const bool shown = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", i == 0 ? "The main file: F9 assembles it, with the files it links"
                                               : "A linked file (INCLUDE / READ): F9 saves it if edited, then assembles the main file");
            if (shown) {
                if (i != activeTab && selectTab < 0) showTab(i);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    selectTab = -1;

    // ---- editor with a line-number gutter
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineH = ImGui::GetTextLineHeight();
    const float bottomH = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.28f);
    const float editorH = std::max(80.0f, ImGui::GetContentRegionAvail().y - bottomH);
    char digits[16]; std::snprintf(digits, sizeof(digits), "%d", std::max(lineCount, 999));
    const float gutterW = ImGui::CalcTextSize(digits).x + 12.0f;

    ImVec2 gutterPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(gutterW, editorH));
    ImGui::SameLine(0, 0);
    const ImGuiID editorId = ImGui::GetID("##asmsrc");
    char childName[512];
    std::snprintf(childName, sizeof(childName), "%s/%s_%08X", ImGui::GetCurrentWindow()->Name, "##asmsrc", editorId);
    if (gotoLine > 0) ImGui::SetKeyboardFocusHere();
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize |
                                ImGuiInputTextFlags_CallbackAlways;
    // Coloured: the box's own text invisible (its cursor and selection are not), the
    // spans drawn over it.
    if (highlight) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    if (ImGui::InputTextMultiline("##asmsrc", source.data(), source.capacity() + 1, ImVec2(-1, editorH), flags,
                                  &AssemblerWindow::editorCallback, this)) {
        modified = true;
        sourceRevision++;
        lineCount = countLines(source);
    }
    if (highlight) ImGui::PopStyleColor();
    if (highlight) drawHighlighted(childName, lineH, editorH);
    float scrollY = 0.0f;
    if (ImGuiWindow* child = ImGui::FindWindowByName(childName)) scrollY = child->Scroll.y;
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 g0 = gutterPos, g1(gutterPos.x + gutterW - 4, gutterPos.y + editorH);
        dl->AddRectFilled(g0, g1, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f));
        dl->PushClipRect(g0, g1, true);
        const float top = gutterPos.y + style.FramePadding.y - scrollY;
        int first = std::max(0, (int)((scrollY - style.FramePadding.y) / lineH) - 1);
        int last = std::min(lineCount, first + (int)(editorH / lineH) + 3);
        for (int i = first; i < last; i++) {
            char n[16]; std::snprintf(n, sizeof(n), "%d", i + 1);
            float w = ImGui::CalcTextSize(n).x;
            bool err = errorLines.count(i + 1) != 0;
            ImU32 col = err ? IM_COL32(255, 90, 80, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            float y = top + i * lineH;
            if (err) dl->AddRectFilled(ImVec2(g0.x, y), ImVec2(g1.x, y + lineH), IM_COL32(150, 30, 30, 120));
            dl->AddText(ImVec2(g1.x - w - 4, y), col, n);
        }
        dl->PopClipRect();
    }

    // ---- result
    {
        FlowRow result;
        result.textColored(lastOk ? ImVec4(0.5f, 0.9f, 0.5f, 1) : (messages.empty() ? ImVec4(0.7f, 0.7f, 0.7f, 1) : ImVec4(1.0f, 0.45f, 0.4f, 1)),
                           summary.c_str());
        if (lastOk && lastRun >= 0) { char r[16]; std::snprintf(r, sizeof r, "RUN &%04X", lastRun & 0xffff); result.textDisabled(r); }
    }
    if (ImGui::BeginTabBar("##asmresult")) {
        char label[48];
        std::snprintf(label, sizeof(label), "Messages (%d)###msgs", (int)messages.size());
        if (ImGui::BeginTabItem(label)) {
            ImGui::BeginChild("##msglist", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (size_t i = 0; i < messages.size(); i++) {
                const Message& m = messages[i];
                char line[640];
                if (m.line > 0) std::snprintf(line, sizeof(line), "%s%d: %s", m.inEditor ? "line " : (m.file + ":").c_str(), m.line, m.text.c_str());
                else std::snprintf(line, sizeof(line), "%s", m.text.c_str());
                ImGui::PushID((int)i);
                if (ImGui::Selectable(line) && m.tab >= 0 && m.line > 0) { selectTab = m.tab; gotoLine = m.line; }
                ImGui::PopID();
            }
            if (messages.empty()) ImGui::TextDisabled(lastOk ? "No errors, no warnings." : "Assemble with F9.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        std::snprintf(label, sizeof(label), "Symbols (%d)###syms", (int)symbols.size());
        if (ImGui::BeginTabItem(label)) {
            fitNextItemWidth(200, "##symfilter");
            ImGui::InputTextWithHint("##symfilter", "filter", symbolFilter, sizeof(symbolFilter));
            after().textDisabled("click a symbol to show it in the disassembly");
            if (ImGui::BeginTable("##symtab", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                 ImGuiTableFlags_Sortable, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableHeadersRow();
                if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs()) {
                    if (sort->SpecsDirty && sort->SpecsCount > 0) {
                        const auto spec = sort->Specs[0];
                        std::sort(symbols.begin(), symbols.end(), [&](const auto& a, const auto& b) {
                            bool lt = spec.ColumnIndex == 0 ? a.first < b.first : a.second < b.second;
                            bool gt = spec.ColumnIndex == 0 ? b.first < a.first : b.second < a.second;
                            return spec.SortDirection == ImGuiSortDirection_Ascending ? lt : gt; });
                        sort->SpecsDirty = false;
                    }
                }
                std::string filter = symbolFilter;
                for (char& c : filter) c = (char)std::toupper((unsigned char)c);
                for (size_t i = 0; i < symbols.size(); i++) {
                    const auto& s = symbols[i];
                    if (!filter.empty()) {
                        std::string u = s.first; for (char& c : u) c = (char)std::toupper((unsigned char)c);
                        if (u.find(filter) == std::string::npos) continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)i);
                    if (ImGui::Selectable(s.first.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && showInDisassembly)
                        showInDisassembly(s.second & 0xffff);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::Text("&%04X", s.second & 0xffff);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

} // namespace cpcse
