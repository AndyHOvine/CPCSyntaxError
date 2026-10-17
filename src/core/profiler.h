// CPCSyntaxError — the code profiler.
//
// Where the Z80 spends its time, while it is switched on (GX4000::profiler not null; off,
// the machine asks one pointer per instruction and nothing more):
//  - per CPU address, how many instructions started there and their machine time;
//  - per ROUTINE (an address a CALL, an RST or an interrupt went to): how many times it was
//    entered, its own time (self) and its time with what it called (inclusive).
// The routines come from the stack: a call pushes a frame (the routine, the SP the call
// left), and a frame ends when SP rises above it -- a RET, but also a POP of the return
// address, or an LD SP to a stack higher up, as demos do. So the call tree survives the
// tricks programs play with SP, and a program that never returns from an interrupt (it
// restores SP itself) still closes the interrupt's frame.
// Times are machine cycles (GX4000::machineCycles: 4 a microsecond, after the CPC's
// rounding of each instruction to whole microseconds).
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cpcse {

struct CodeProfiler {
    static constexpr int TOP = -1;            // code reached without a call seen
    struct Routine { uint64_t calls = 0, self = 0, inclusive = 0; bool interrupt = false; };
    std::vector<uint32_t> count;              // per CPU address: instructions started there
    std::vector<uint64_t> cycles;             // per CPU address: their machine cycles
    std::unordered_map<int, Routine> routines;
    uint64_t totalCycles = 0, instructions = 0;

    void clear() {
        count.assign(0x10000, 0); cycles.assign(0x10000, 0);
        routines.clear(); stack.clear(); onStack.clear();
        totalCycles = 0; instructions = 0;
    }
    int current() const { return stack.empty() ? TOP : stack.back().entry; }
    // inclusive time, with the time of a call still open (the outermost one) up to now
    uint64_t inclusiveOf(int entry) const {
        if (entry == TOP) return totalCycles;
        const auto it = routines.find(entry);
        uint64_t v = it == routines.end() ? 0 : it->second.inclusive;
        for (const Frame& f : stack) if (f.entry == entry) { v += totalCycles - f.start; break; }
        return v;
    }
    size_t depth() const { return stack.size(); }

    // One step of the Z80: started at pc with sp0 and the opcode there; ended at pcAfter,
    // spAfter, `cyc` machine cycles long. `interrupt`: it was an interrupt's response.
    void step(int pc, int sp0, int opcode, int pcAfter, int spAfter, int cyc, bool interrupt) {
        if (count.empty()) clear();
        totalCycles += (uint64_t)cyc;
        const int at = interrupt ? pcAfter : pc;   // the response's time is the handler's
        if (!interrupt) { count[(size_t)pc]++; instructions++; }
        cycles[(size_t)at] += (uint64_t)cyc;
        if (!interrupt) routines[current()].self += (uint64_t)cyc;
        const bool callOp = opcode == 0xcd || (opcode & 0xc7) == 0xc4 || (opcode & 0xc7) == 0xc7;
        if (interrupt || (callOp && spAfter == ((sp0 - 2) & 0xffff))) {
            if (stack.size() >= 1024) stack.erase(stack.begin());   // a runaway: forget the oldest
            stack.push_back({ pcAfter, spAfter, totalCycles });
            onStack[pcAfter]++;
            Routine& r = routines[pcAfter];
            r.calls++;
            if (interrupt) { r.interrupt = true; r.self += (uint64_t)cyc; }
            return;
        }
        // frames whose return address SP has gone above have ended
        while (!stack.empty() && spAfter > stack.back().sp) {
            const Frame f = stack.back();
            stack.pop_back();
            int& n = onStack[f.entry];
            if (--n <= 0) { routines[f.entry].inclusive += totalCycles - f.start; onStack.erase(f.entry); }
        }
    }

private:
    struct Frame { int entry; int sp; uint64_t start; };
    std::vector<Frame> stack;
    std::unordered_map<int, int> onStack;     // how many frames of each routine are open
};

} // namespace cpcse
