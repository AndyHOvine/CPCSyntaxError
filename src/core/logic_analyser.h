// CPCSyntaxError — a logic analyser on the machine's own signals.
//
// One sample a microsecond (one CRTC character, the machine's own clock): the pins of the
// CRTC, the GATE ARRAY and the Z80 as they are at the end of that character, the counters
// that drive them, the instruction the Z80 is in, and the three AY channels. It only reads
// the machine: nothing it does changes what the machine does. It records only while
// `enabled` (the window is open and recording), so it costs nothing otherwise.
#pragma once
#include "common.h"

namespace cpcse {

struct LaSample {
    enum Flag : uint16_t {
        HSYNC = 1 << 0,     // the CRTC's HSYNC pin
        VSYNC = 1 << 1,     // the CRTC's VSYNC pin (a ghost VSYNC does not drive it)
        CSYNC = 1 << 2,     // the GATE ARRAY's C-SYNC, to the monitor
        DISPEN = 1 << 3,    // the CRTC's display enable (DISPTMG, after the skew)
        INT = 1 << 4,       // the Z80's INT line
        HALT = 1 << 5,      // the Z80 is in a HALT
        MOTOR = 1 << 6,     // the cassette motor relay
        TAPE = 1 << 7,      // the cassette input (PPI port B bit 7)
        FDCINT = 1 << 8,    // the disc controller has an interrupt pending
    };
    uint16_t flags = 0;
    uint16_t pc = 0;        // the address of the instruction the Z80 is in
    uint16_t ma = 0;        // the CRTC's MA (the start of its character row)
    uint8_t c0 = 0, c4 = 0, c9 = 0;
    uint8_t r52 = 0;        // the GATE ARRAY's interrupt counter
    uint8_t mode = 0;       // the GATE ARRAY's graphic mode
    uint8_t ay[3] = { 0, 0, 0 };   // each channel's output, 0..15 (0 while the mixer has it off)
};

class LogicAnalyser {
public:
    static constexpr uint64_t CAPACITY = 1u << 18;   // 262144 usec: about 13 frames
    bool enabled = false;
    // Stop the machine when this happens (GX4000::recordAnalyser sets debuggerPaused).
    enum class Trigger { None, VsyncRise, HsyncRise, IntRise, ModeChange, PcEquals };
    Trigger trigger = Trigger::None;
    int triggerPc = 0;
    int64_t triggeredAt = -1;      // the sample the trigger fired on, -1: none yet
    bool triggerArmed = false;     // fires once, then disarms

    uint64_t total() const { return count_; }                     // samples ever recorded
    uint64_t oldest() const { return count_ > CAPACITY ? count_ - CAPACITY : 0; }
    bool has(uint64_t n) const { return n >= oldest() && n < count_; }
    const LaSample& at(uint64_t n) const { return ring_[(size_t)(n & (CAPACITY - 1))]; }
    void clear() { count_ = 0; triggeredAt = -1; }
    // the sample, and whether the trigger fired on it
    bool push(const LaSample& s) {
        if (ring_.empty()) ring_.resize((size_t)CAPACITY);
        bool fired = false;
        if (triggerArmed && trigger != Trigger::None && count_ > 0) {
            const LaSample& p = at(count_ - 1);
            auto rise = [&](uint16_t f) { return (s.flags & f) && !(p.flags & f); };
            switch (trigger) {
                case Trigger::VsyncRise: fired = rise(LaSample::VSYNC); break;
                case Trigger::HsyncRise: fired = rise(LaSample::HSYNC); break;
                case Trigger::IntRise: fired = rise(LaSample::INT); break;
                case Trigger::ModeChange: fired = s.mode != p.mode; break;
                case Trigger::PcEquals: fired = s.pc == (triggerPc & 0xffff) && p.pc != s.pc; break;
                case Trigger::None: break;
            }
        }
        ring_[(size_t)(count_ & (CAPACITY - 1))] = s;
        if (fired) { triggeredAt = (int64_t)count_; triggerArmed = false; }
        count_ += 1;
        return fired;
    }
private:
    std::vector<LaSample> ring_;
    uint64_t count_ = 0;
};

} // namespace cpcse
