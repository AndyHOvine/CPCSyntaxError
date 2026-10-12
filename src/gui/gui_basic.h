// CPCSyntaxError GUI — the BASIC window, beside the Assembler: a Locomotive BASIC program as
// text. It lists the program in the machine (core/basic.h's decoder), edits it, and puts it
// back into the machine at once: tokenized as BASIC tokenizes a typed line (checked byte for
// byte against BASIC itself by cpcse-basic-check) and written at &0170 with BASIC's pointers
// set (EmuHost::injectBasic), optionally followed by a typed RUN.
#pragma once
#include <map>
#include <ostream>
#include <string>

#include "core/basic.h"

namespace cpcse {

class EmuHost;
struct FileBrowser;
struct SaveDialog;

class BasicWindow {
public:
    BasicWindow(EmuHost& host, FileBrowser& browser, SaveDialog& saver);
    void draw(bool* open);
    bool focused = false;                       // the window (or its editor) has focus this frame

    bool fromMachine();                         // the program in memory into the editor
    bool intoMachine(bool thenRun);             // the editor's program into memory (and RUN)
    void newFile();
    void openFile(const std::string& path);
    void saveFile(const std::string& path);
    void requestOpen();
    void requestSave(bool saveAs);
    void setSource(const std::string& text) { source = text; }
    const std::string& text() const { return source; }
    const std::string& status() const { return message; }

    void loadSettings(const std::map<std::string, std::string>& ini);
    void saveSettings(std::ostream& out) const;

private:
    EmuHost& host;
    FileBrowser& browser;
    SaveDialog& saver;
    BasicDecoder basic;
    std::string source = "10 PRINT \"HELLO\"\n";
    std::string path;                           // "" = never saved
    std::string message = "Type a program, or take the one in the machine (From the machine).";
    bool error = false;
    int tabRequest = -1;                        // 0 Editor, 1 In memory: brought to the front once
    void toolbar();
    void editorTab();
    void memoryTab();
};

} // namespace cpcse
