// CPCSyntaxError — MIDI out to the host computer. See host_midi.h.
#include "host_midi.h"

#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

namespace cpcse {

namespace {

std::mutex lock;
std::function<void(const std::vector<int>&)> sink;
std::vector<int> message;            // the message being assembled
int runningStatus = 0;
bool inSysEx = false;
std::string lastError;

#if defined(_WIN32)
HMIDIOUT out = nullptr;
bool opened = false;
#endif

// Data bytes after a status byte (the MIDI 1.0 specification's table).
int dataCount(int status) {
    if (status < 0xf0) {
        const int kind = status & 0xf0;
        return kind == 0xc0 || kind == 0xd0 ? 1 : 2;
    }
    switch (status) {
        case 0xf1: case 0xf3: return 1;
        case 0xf2: return 2;
        default: return 0;
    }
}

void deliver(const std::vector<int>& m) {
    if (sink) { sink(m); return; }
#if defined(_WIN32)
    if (!opened) {
        opened = true;
        if (midiOutOpen(&out, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { out = nullptr; lastError = "No MIDI out device on this computer."; }
    }
    if (!out) return;
    if (!m.empty() && m[0] == 0xf0) {
        std::vector<char> data(m.begin(), m.end());
        MIDIHDR header{};
        header.lpData = data.data();
        header.dwBufferLength = (DWORD)data.size();
        if (midiOutPrepareHeader(out, &header, sizeof header) == MMSYSERR_NOERROR) {
            midiOutLongMsg(out, &header, sizeof header);
            while (midiOutUnprepareHeader(out, &header, sizeof header) == MIDIERR_STILLPLAYING) Sleep(1);
        }
        return;
    }
    DWORD word = 0;
    for (size_t i = 0; i < m.size() && i < 3; i++) word |= (DWORD)(m[i] & 0xff) << (8 * i);
    midiOutShortMsg(out, word);
#else
    lastError = "MIDI out is not available on this system yet.";
#endif
}

} // namespace

void HostMidi::send(int byte) {
    std::lock_guard<std::mutex> g(lock);
    byte &= 0xff;
    if (byte >= 0xf8) { deliver({ byte }); return; }          // real time: anywhere, alone
    if (inSysEx) {
        message.push_back(byte);
        if (byte == 0xf7 || message.size() > 65536) { deliver(message); message.clear(); inSysEx = false; }
        if (byte < 0x80 || byte == 0xf7) return;
        // another status byte ends the SysEx without its F7, and starts its own message
        message.pop_back(); deliver(message); message.clear(); inSysEx = false;
    }
    if (byte == 0xf0) { message = { 0xf0 }; inSysEx = true; runningStatus = 0; return; }
    if (byte >= 0x80) {
        message = { byte };
        runningStatus = byte < 0xf0 ? byte : 0;
        if (dataCount(byte) == 0) { deliver(message); message.clear(); }
        return;
    }
    // a data byte: after its status, or under running status
    if (message.empty()) {
        if (!runningStatus) return;                              // no status yet: dropped
        message = { runningStatus };
    }
    message.push_back(byte);
    if ((int)message.size() - 1 >= dataCount(message[0])) { deliver(message); message.clear(); }
}

void HostMidi::setSink(std::function<void(const std::vector<int>&)> s) {
    std::lock_guard<std::mutex> g(lock);
    sink = std::move(s);
    message.clear(); runningStatus = 0; inSysEx = false;
}

std::string HostMidi::error() { std::lock_guard<std::mutex> g(lock); return lastError; }

void HostMidi::close() {
    std::lock_guard<std::mutex> g(lock);
#if defined(_WIN32)
    if (out) { midiOutReset(out); midiOutClose(out); out = nullptr; }
    opened = false;
#endif
}

} // namespace cpcse
