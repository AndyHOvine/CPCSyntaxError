// CPCSyntaxError — the Logic analyser window: the machine's signals as traces over time
// (core/logic_analyser.h records them, one sample a microsecond).
#pragma once
#include <cstdint>

namespace cpcse {

class EmuHost;
class Debugger;

class LogicAnalyserWindow {
public:
    LogicAnalyserWindow(EmuHost& host, Debugger& debugger);
    void draw(bool* open);
    bool focused = false;
private:
    EmuHost& host;
    Debugger& debugger;
    double usPerPixel = 1.0;        // the zoom
    int64_t rightEdge = -1;         // the sample at the traces' right edge, -1: the newest
    int64_t markA = -1, markB = -1; // the two markers (click, Shift+click)
    int64_t shownTrigger = -1;      // the trigger the view last went to
    bool rows[19];                  // which traces are shown
    int triggerIndex = 0;
    char triggerPcText[8] = "0000";
};

} // namespace cpcse
