// CPCSyntaxError GUI — the built-in assembler: RASM (Edouard BERGE, MIT) itself, linked
// in as a library, with an editor window around it. Being RASM, every directive, macro,
// cruncher and expression it has works as it does from the command line; the only
// difference is where the result goes -- into the running machine's memory.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "asm_highlight.h"
#include "c_build.h"

struct ImFont;
struct ImGuiInputTextCallbackData;

namespace cpcse {

class EmuHost;
class Debugger;
struct FileBrowser;
struct SaveDialog;

class AssemblerWindow {
public:
    AssemblerWindow(EmuHost& host, Debugger& debugger, FileBrowser& browser, SaveDialog& saver);

    void draw(bool* open);
    bool focused = false;                       // the window (or its editor) has focus this frame
    std::function<void(int address)> showInDisassembly;

    void assemble(bool thenRun);                // F9 / Ctrl+F9
    // A C build runs on a thread and draw() takes its result in; this waits for it instead
    // (the checks, which draw no frames).
    void waitForBuild() { if (cRun) finishCBuild(); }
    void setCToolchain(const CToolchain& t) { ctools = t; }
    const CToolchain& cToolchain() const { return ctools; }
    const std::string& buildLog() const { return cLog; }
    void setSource(const std::string& text) { source = text; modified = true; sourceRevision++; }
    const std::string& resultSummary() const { return summary; }
    bool assembledOk() const { return lastOk; }
    int errorCount() const { return (int)messages.size(); }
    bool errorOnLine(int line) const { return errorLines.count(line) != 0; }
    std::string messageLog() const {
        std::string s;
        for (const auto& m : messages) s += "    [" + m.file + ":" + std::to_string(m.line) + "] " + m.text + "\n";
        return s;
    }
    // the open files: 0 is the main file, the rest the files it links
    int fileCount() const { return (int)tabs.size(); }
    std::string filePath(int index) const { return index == activeTab ? path : (index >= 0 && index < (int)tabs.size() ? tabs[(size_t)index].path : std::string()); }
    void showFile(int index) { showTab(index); }
    void newFile();
    void openFile(const std::string& path);
    void saveFile(const std::string& path);
    void requestOpen();
    void requestSave(bool saveAs);

    void loadSettings(const std::map<std::string, std::string>& ini);
    void saveSettings(std::ostream& out) const;

private:
    EmuHost& host;
    Debugger& debugger;
    FileBrowser& browser;
    SaveDialog& saver;

    // The open files, one tab each. tabs[0] is the main file, the one assembled; the others
    // are the files it links (INCLUDE / READ, found and opened by themselves). The active
    // tab's text is edited in place in source / path / modified / lineCount below, and put
    // back into its Tab when another tab is shown.
    struct Tab { std::string source, path; bool modified = false; int lineCount = 1; };
    std::vector<Tab> tabs;
    int activeTab = 0;
    int selectTab = -1;                         // show this tab next frame
    void storeActive();
    void showTab(int index);
    int tabFor(const std::string& file) const;  // the tab of a file rasm named, -1 = none
    std::vector<std::string> linkedFiles(const std::string& text, const std::string& fromPath) const;
    void openLinked(const std::string& text, const std::string& fromPath, int depth);
    void refreshErrorLines();

    std::string source;
    std::string path;                           // "" = never saved
    bool modified = false;
    int syntax = 0;                             // RASM_SYNTAX_*
    bool brkLabels = true;                      // BRK labels become breakpoints, as rasm -eb
    int lastStart = -1, lastRun = -1, lastLength = 0;
    int lineCount = 1;

    struct Message { int line = 0; std::string file; std::string text; bool inEditor = false; int tab = -1; };
    std::vector<Message> messages;
    std::set<int> errorLines;
    std::vector<std::pair<std::string, int>> symbols;
    std::string summary = "Nothing assembled yet.";
    bool lastOk = false;
    char symbolFilter[64] = {0};

    // Syntax colouring (asm_highlight.h): the text box draws its text invisibly and the
    // coloured spans are drawn over it, so editing, selection and undo stay ImGui's own.
    bool highlight = true;
    AsmColours colours = defaultAsmColours();
    uint64_t sourceRevision = 0, indexedRevision = ~0ull;
    std::vector<int> lineStarts;                // offset of each line in `source`
    std::vector<uint8_t> lineInComment;         // the line starts inside /* */
    void indexLines();
    void drawHighlighted(const char* childName, float lineH, float editorH);
    void coloursPopup();

    // C (c_build.h): a main file ending .c is built with SDCC, and CPCtelera's library when
    // its folder is set, instead of rasm. The build runs on a thread; draw() takes its
    // result in when it is done.
    CToolchain ctools;
    bool cMode() const;
    struct CRun {
        std::thread worker;
        std::mutex lock;
        std::string progress;
        bool done = false, thenRun = false, assetsOnly = false;
        CBuildResult result;
        ~CRun() { if (worker.joinable()) worker.join(); }
    };
    std::shared_ptr<CRun> cRun;
    std::string cLog;                           // the last build's commands and what they printed
    void pageForRun(int start, int length);     // the ROMs over the program paged out before it runs
    void startCBuild(bool thenRun, bool assetsOnly = false);   // assetsOnly: "Convert assets"
    void finishCBuild();
    void cSettingsPopup();

    int gotoLine = -1;                          // move the editor's cursor here next frame
    static int editorCallback(ImGuiInputTextCallbackData* data);
    std::string displayName() const;
};

} // namespace cpcse
