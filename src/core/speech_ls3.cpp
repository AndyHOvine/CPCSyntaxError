// CPCSyntaxError — LambdaSpeak 3's own modes: the board's control bytes, its Epson and
// DECtalk speech (with the host's voices), its getters and its clock. See speech.h.
#include "speech.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <ctime>

namespace cpcse {

namespace {

const int VERSION = 54;              // the firmware this follows
const size_t FLUSH_AT = 253;         // SPEECH_BUFFER_FLUSH_AT

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::tm hostTime() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv;
}

// DECtalk's ARPAbet codes: two letters first.
const char* TWO[] = { "aa", "ae", "ah", "ao", "aw", "ax", "ay", "ch", "dh", "dx", "eh", "el", "en", "er", "ey", "hx", "ih",
                      "ix", "iy", "jh", "lx", "ll", "nx", "ow", "oy", "rr", "rx", "sh", "th", "tx", "uh", "uw", "yu", "yx",
                      "zh", "ar", "or", "ir", "ur" };

void parsePhones(const std::string& s, std::vector<SpeechPhone>& out) {
    size_t i = 0;
    while (i < s.size()) {
        const char c = (char)std::tolower((unsigned char)s[i]);
        if (c == '_') {
            SpeechPhone p; p.arpabet = "_"; out.push_back(p); i++;
        } else if (std::isalpha((unsigned char)c)) {
            SpeechPhone p;
            const std::string two = lower(s.substr(i, 2));
            bool matched = false;
            for (const char* t : TWO) if (two == t) { p.arpabet = two; i += 2; matched = true; break; }
            if (!matched) { p.arpabet = std::string(1, c); i += 1; }
            out.push_back(p);
        } else if (c == '<') {
            // <duration[,note]> belongs to the phone before it
            const size_t end = s.find('>', i);
            const std::string inside = s.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
            i = end == std::string::npos ? s.size() : end + 1;
            if (!out.empty()) {
                int ms = 0, note = 0;
                if (std::sscanf(inside.c_str(), "%d,%d", &ms, &note) >= 1) { out.back().ms = std::max(0, std::min(ms, 5000)); out.back().note = std::max(0, std::min(note, 37)); }
            }
        } else {
            i++;                                    // stress marks, spaces, punctuation
        }
    }
}

bool hasWords(const std::string& s) {
    for (unsigned char c : s) if (std::isalnum(c)) return true;
    return false;
}

// The SP0256-AL2's 64 allophones by their data-sheet names (PA1-PA5 pauses, then OY, AY, EH,
// KK3 ...), each as the DECtalk ARPAbet phone it names. LambdaSpeak 3 speaks the emulated
// SSA-1 and dk'tronics through its DECtalk this way ("a DECtalk-based emulation of the
// Amstrad SSA-1", its README). "_" is a pause.
const char* const ALLOPHONE_PHONES[64] = {
    "_", "_", "_", "_", "_", "oy", "ay", "eh",            // PA1 PA2 PA3 PA4 PA5 OY AY EH
    "k", "p", "jh", "n", "ih", "t", "r", "ax",            // KK3 PP JH NN1 IH TT2 RR1 AX
    "m", "t", "dh", "iy", "ey", "d", "uw", "ao",          // MM TT1 DH1 IY EY DD1 UW1 AO
    "aa", "y", "ae", "hx", "b", "th", "uh", "uw",         // AA YY2 AE HH1 BB1 TH UH UW2
    "aw", "d", "g", "v", "g", "sh", "zh", "r",            // AW DD2 GG3 VV GG1 SH ZH RR2
    "f", "k", "k", "z", "nx", "l", "w", "eh r",           // FF KK2 KK1 ZZ NG LL WW XR
    "w", "y", "ch", "er", "er", "ow", "dh", "s",          // WH YY1 CH ER1 ER2 OW DH2 SS
    "n", "hx", "or", "ar", "ir", "g", "el", "b",          // NN2 HH2 OR AR YR GG2 EL BB2
};

const size_t EEPROM_SIZE = 0x20000;  // 25LC1024: 128 KB
const int EEPROM_PAGE = 512;         // LambdaSpeak 3's page (2 x the chip's 256-byte write page)
const long long EEPROM_WRITE_US = 6000;   // 25LC1024 data sheet: write cycle time Twc, 6 ms max
const long long EEPROM_ERASE_US = 10000;  // ...and chip erase time Tce, 10 ms max
// One play-loop pass of the firmware: about 2 us of its own, then a 5-byte SPI read per channel
// at 20 MHz / 2 (SPI_CLOCK_DIV2), 0.8 us a byte. The timer (20 MHz / 8 = 2.5 MHz) is only
// looked at once a pass, so a channel's sample time is whole passes.
double playLoopUs(int channels) { return 2.0 + 4.4 * channels; }

} // namespace

// The Epson parser (the Emic 2 manual's, as LambdaSpeak's ENGLISH.BAS uses it):
//   :-)N voice N (0-8)   ## whisper on/off   /\ pitch up   \/ pitch down
//   >> faster   << slower
std::vector<SpeechSegment> SpeechSynth::parseEpson(const std::string& text, int voice, int wpm, bool spanish) {
    std::vector<SpeechSegment> out;
    SpeechSegment cur;
    cur.voice = std::max(0, std::min(voice, 8));
    cur.wpm = wpm;
    cur.spanish = spanish;
    auto flush = [&]() {
        if (hasWords(cur.text)) out.push_back(cur);
        cur.text.clear();
    };
    for (size_t i = 0; i < text.size();) {
        auto at = [&](const char* p) { return text.compare(i, std::strlen(p), p) == 0; };
        if (at(":-)") && i + 3 < text.size() && std::isdigit((unsigned char)text[i + 3])) {
            flush(); cur.voice = std::min(8, text[i + 3] - '0'); i += 4;
        } else if (at("##")) { flush(); cur.whisper = !cur.whisper; i += 2; }
        else if (at("/\\")) { flush(); cur.pitch += 1; i += 2; }
        else if (at("\\/")) { flush(); cur.pitch -= 1; i += 2; }
        else if (at(">>")) { flush(); cur.wpm = std::min(600, (int)std::lround(cur.wpm * 1.15)); i += 2; }
        else if (at("<<")) { flush(); cur.wpm = std::max(75, (int)std::lround(cur.wpm / 1.15)); i += 2; }
        else { cur.text += text[i]; i += 1; }
    }
    flush();
    return out;
}

// DECtalk's in-line commands (the DECtalk 5.01 guide): [:np] [:nh] ... or [:name x] the
// voice, [:nN] by number (as LambdaSpeak sends it), [:rate N] words a minute, [:dv ap N]
// the average pitch in Hz; [:phoneme ... on] (or [:phone arpa speak on]) makes [...] a
// string of ARPAbet phones, each with an optional <duration,note>. Other commands are
// DECtalk's own and have no host equivalent; they are passed over.
std::vector<SpeechSegment> SpeechSynth::parseDecTalk(const std::string& text, int voice, int wpm, bool spanish) {
    std::vector<SpeechSegment> out;
    SpeechSegment cur;
    cur.voice = std::max(0, std::min(voice, 8));
    cur.wpm = wpm;
    cur.spanish = spanish;
    bool phonemes = false;
    auto flush = [&]() {
        if (hasWords(cur.text) || !cur.phones.empty()) out.push_back(cur);
        cur.text.clear();
        cur.phones.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '[') { cur.text += text[i++]; continue; }
        const size_t end = text.find(']', i);
        const std::string inside = text.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
        i = end == std::string::npos ? text.size() : end + 1;
        if (!inside.empty() && inside[0] == ':') {
            std::string c = lower(inside.substr(1));
            while (!c.empty() && c.back() == ' ') c.pop_back();
            static const char letters[] = "phbudkfrw";          // the S1V30120's order
            int v = -1;
            if (c.size() == 2 && c[0] == 'n') {
                if (std::isdigit((unsigned char)c[1])) v = c[1] - '0';
                else if (const char* p = std::strchr(letters, c[1])) v = (int)(p - letters);
            } else if (c.rfind("name ", 0) == 0 && c.size() > 5) {
                if (const char* p = std::strchr(letters, c[5])) v = (int)(p - letters);
            }
            if (v >= 0 && v <= 8) { flush(); cur.voice = v; continue; }
            int n = 0;
            if (std::sscanf(c.c_str(), "rate %d", &n) == 1) { flush(); cur.wpm = std::max(75, std::min(n, 600)); continue; }
            if (std::sscanf(c.c_str(), "dv ap %d", &n) == 1 && n > 0) { flush(); cur.pitch = (int)std::lround(6.0 * std::log2(n / 122.0)); continue; }
            if (c.rfind("phone", 0) == 0) {
                const bool on = c.find(" on") != std::string::npos, off = c.find(" off") != std::string::npos;
                if (on) phonemes = true;
                else if (off) phonemes = false;
            }
            continue;
        }
        if (phonemes) {
            if (hasWords(cur.text)) flush();
            cur.text.clear();
            parsePhones(inside, cur.phones);
        } else cur.text += inside;
    }
    flush();
    return out;
}

// ------------------------------------------------------------------ the board

void SpeechSynth::ls3Reset() {
    ls3Mode = ls3LastMode = LS3_SSA1;
    ls3Blocking = true; ls3Confirm = true; ls3NonBlockConfirm = false;
    ls3Voice = 1; ls3Rate = 9; ls3Volume = 13; ls3Language = 0; ls3FlushDelay = 10; ls3Getters = 0;
    steps.clear(); stepUs = 0;
    textBuffer.clear(); hexState = 0; ls3Pending = 0;
    ttsSample = 0.0f;
    serialMode = false; escape = 0; pendingResult = -1; directMode = true;
    txBuffer.clear(); rxBuffer.clear(); rxCursor = 0;
    serialBaud = 0; midiDuplex = false; midiEscape = 0;
    pcmSample = 0.0f;
    eeStage = EE_NONE; eeTesting = false;
    playLastZero = false; playChannel = playStart = playEnd = 0;
    pcmChannels = {};
    amdrumExit = 0; amdrumLast = 0;
    allophones.clear(); allophoneQuietUs = -1; allophoneBusyUs = 0;
    chip.reset();
}

int SpeechSynth::ls3Idle() {
    switch (ls3Mode) {
        case LS3_EPSON: case LS3_DECTALK: return 32;
        case LS3_SERIAL: return 16;
        // The emulated SSA-1: SBY (bit 7) and /LRQ (bit 6) as ls300hardware.h drives them:
        // idle 128, "speaking, load me" 0 for 14 ms after an allophone, busy 64 while it reads.
        case LS3_SSA1: return allophoneBusyUs > 0 ? 0x40 : (allophoneQuietUs >= 0 && allophoneQuietUs < 14000) ? 0x00 : 0x80;
        case LS3_SSA1_SP0:
            return (chip.sby_r() ? 0x80 : 0) | (chip.lrq_r() ? 0 : 0x40);   // 128 idle, 0 speaking, 64 full
        case LS3_DK: return allophoneBusyUs > 0 ? 0x80 : 0x00;                    // dk_speech_busy / idle_loadme
        case LS3_AMDRUM: return 0;
        case LS3_EEPROM_PLAY: return 32;                                    // speech_native_ready
        case LS3_DK_SP0: return chip.lrq_r() ? 0x7f : 0xff;                 // the dk'tronics card's own bit 7
    }
    return 0xff;
}

int SpeechSynth::ls3Read() {
    if (!steps.empty() && !steps.front().listening) return steps.front().bus;
    if (eeStage == EE_GET_DATA) return eeBus;                            // the byte last fetched (32 at a page start)
    if (eeStage != EE_NONE) return 32;                                    // waiting for a byte: speech_native_ready
    if (ls3Mode == LS3_SERIAL && pendingResult >= 0) { const int r = pendingResult; pendingResult = -1; return r; }
    return ls3Idle();
}

void SpeechSynth::ls3Speak(const std::string& text, bool hold) {
    const int wpm = 15 + 25 * ls3Rate;
    const bool spanish = ls3Language == 1;
    std::vector<SpeechSegment> say = ls3Mode == LS3_DECTALK ? parseDecTalk(text, ls3Voice - 1, wpm, spanish)
                                                            : parseEpson(text, ls3Voice - 1, wpm, spanish);
    for (SpeechSegment& s : say) s.gain = ls3Volume / 13.0;
    Step st;
    st.bus = 0;
    st.hold = hold;
    st.say = std::move(say);
    if (st.say.empty()) { st.us = 10; st.say.clear(); }   // nothing to say: a moment's work
    steps.push_back(std::move(st));
}

void SpeechSynth::ls3Confirmation(const std::string& text) {
    if (ls3Confirm) ls3Speak(text, !ls3NonBlockConfirm);
}

void SpeechSynth::ls3Getter(int value, bool fourBit, const std::string& name) {
    Step a; a.us = getterUs();
    if (fourBit) {
        a.bus = (value << 4) & 0xff;
        steps.push_back(a);
        if (ls3Confirm) {
            // the value stays on the bus while it is said (cpc_input)
            ls3Speak(name + " " + std::to_string(value) + ".", false);
            steps.back().bus = a.bus;
        }
        Step z; z.bus = 0; z.us = getterUs();
        steps.push_back(z);
    } else {
        a.bus = value & 0xff;
        steps.push_back(a);
        Step b; b.bus = 255; b.us = getterUs(); steps.push_back(b);
        Step z; z.bus = 0; z.us = 10; steps.push_back(z);
    }
}

void SpeechSynth::ls3Control(int v) {
    textBuffer.clear();                          // proceed(): what was not yet said is dropped
    hexState = 0;
    const bool sp0 = ls3Mode == LS3_SSA1_SP0 || ls3Mode == LS3_DK_SP0;
    const bool native = ls3Mode == LS3_EPSON || ls3Mode == LS3_DECTALK;
    auto mode = [&](int m, const char* say) { ls3Mode = ls3LastMode = m; ls3Confirmation(say); };
    switch (v) {
        case 0xff: { ls3Reset(); Step boot; boot.us = 50000; steps.push_back(boot); return; }
        case 0xf0: ls3Pending = 1; return;      // the next byte only lights the LEDs
        case 0xf1:
            ls3LastMode = ls3Mode;
            ls3Mode = LS3_SERIAL;
            serialMode = true; escape = 0; pendingResult = -1;
            return;
        case 0xf2:
            ls3Getter((ls3Mode & 15) | ls3Blocking << 4 | ls3NonBlockConfirm << 5 | ls3Language << 6 | ls3Confirm << 7, false, "");
            return;
        case 0xf4: ls3Confirmation("Use non-blocking confirmations."); ls3NonBlockConfirm = true; return;
        case 0xf3: ls3NonBlockConfirm = false; ls3Confirmation("Use blocking confirmations."); return;
        case 0xef: mode(LS3_EPSON, "Native Epson mode."); return;
        case 0xee: mode(LS3_DECTALK, "Native DecTalk mode."); return;
        case 0xed: mode(LS3_SSA1, "S-S-A 1 mode."); return;
        case 0xec: mode(LS3_DK, "DeeKay Tronics mode."); return;
        case 0xe2: mode(LS3_SSA1_SP0, "S-S-A 1 vintage mode."); return;
        case 0xe1: mode(LS3_DK_SP0, "DeeKay Tronics vintage mode."); return;
        // ---- the EEPROM (eeprom_pcm_upload_mode, eeprom_get_data, eeprom_pcm_clear_all,
        // eeprom_full_check, eeprom_pcm_play_mode). The prompts are said while the Z80 runs.
        case 0xfe: case 0xf9:
            eeTesting = v == 0xfe;
            if (ls3Confirm) ls3Speak("EEPROM data upload. Send start page number.", false);
            eeStage = EE_START;
            return;
        case 0xf7:
            if (ls3Confirm) ls3Speak("EEPROM get data. Send start page number.", false);
            eeStage = EE_GET_START;
            return;
        case 0xf8: {
            // the Z80 is held from the control byte to the end (process_control runs halted)
            if (ls3Confirm) ls3Speak("EEPROM clear all.", true);
            Step erase; erase.hold = true; erase.us = EEPROM_ERASE_US;
            erase.then = [this]() { std::fill(eeprom.begin(), eeprom.end(), (uint8_t)0xff); saveEeprom(); };
            steps.push_back(std::move(erase));
            if (ls3Confirm) ls3Speak("EEPROM cleared.", true);
            return;
        }
        case 0xf5: {
            if (ls3Confirm) ls3Speak("EEPROM determine size.", true);
            Step erase; erase.hold = true; erase.us = EEPROM_ERASE_US;
            erase.then = [this]() { std::fill(eeprom.begin(), eeprom.end(), (uint8_t)0xff); };
            steps.push_back(std::move(erase));
            if (ls3Confirm) ls3Speak("EEPROM cleared. All bytes 255.", true);
            // a test byte at 0, read on until it comes round again: the size
            if (ls3Confirm) ls3Speak("EEPROM has " + std::to_string(EEPROM_SIZE) + " bytes.", true);
            Step erase2; erase2.hold = true; erase2.us = EEPROM_ERASE_US;
            steps.push_back(std::move(erase2));
            if (ls3Confirm) ls3Speak("EEPROM cleared.", true);
            // every 256-byte write page filled with its number / 2, so a 512-byte page with its own
            Step pages; pages.hold = true; pages.us = (long long)(EEPROM_SIZE / 256) * EEPROM_WRITE_US;
            pages.then = [this]() {
                for (size_t a = 0; a < EEPROM_SIZE; a++) eeprom[a] = (uint8_t)((a / 256) >> 1);
                saveEeprom();
            };
            steps.push_back(std::move(pages));
            if (ls3Confirm) ls3Speak("EEPROM pages written.", true);
            if (ls3Confirm) ls3Speak("EEPROM pages okay. Done testing EEPROM.", true);
            return;
        }
        case 0xfa: case 0xfb: case 0xfc: case 0xfd:
            playChannels = v - 0xf9;
            if (ls3Confirm)
                ls3Speak("EEPROM P C M play with " + std::to_string(playChannels) + " channels. Send channel, page number, number of pages, "
                         "and sample rate byte. Use reset button to quit.", false);
            ls3Mode = LS3_EEPROM_PLAY;
            playLastZero = false; playChannel = playStart = playEnd = 0;
            pcmChannels = {};
            pcmSample = 0.0f;
            return;
        // ---- Amdrum (amdrum_mode): &FFxx is the DAC from now on
        case 0xe3:
            ls3Mode = ls3LastMode = LS3_AMDRUM;
            ls3Confirmation("Amdrum mode. Power cycle C P C to quit.");
            amdrumExit = 0; amdrumLast = 0;
            pcmSample = 0.0f;
            return;
        case 0xc8: ls3Speak("LambdaSpeak 3, by Michael Wessel, with Dr. Stefan Stumpferl. Emulated by C-P-C Syntax Error.", !ls3NonBlockConfirm); return;
        case 0xc7: ls3Speak("I'm sorry Dave. I'm afraid I can't do that.", !ls3NonBlockConfirm); return;
        case 0xc6: ls3Speak("Daisy, Daisy, give me your answer do. I'm half crazy, all for the love of you.", !ls3NonBlockConfirm); return;
        case 0xc3: {
            static const char* names[] = { "S-S-A 1 mode.", "Epson mode.", "DecTalk mode.", "DeeKay Tronics mode.", "", "S-S-A 1 vintage mode.", "DeeKay Tronics vintage mode." };
            if (ls3Mode >= 0 && ls3Mode <= 6 && names[ls3Mode][0]) ls3Speak(names[ls3Mode], !ls3NonBlockConfirm);
            return;
        }
        default: break;
    }
    if (sp0) return;                             // the SP0 modes know only the commands above
    switch (v) {
        case 0xeb: ls3Blocking = false; ls3Confirmation("Non-Blocking mode."); return;
        case 0xea: ls3Blocking = true; ls3Confirmation("Blocking mode."); return;
        case 0xe9: ls3Confirm = true; ls3Confirmation("Confirmations on."); return;
        case 0xe8: ls3Confirm = false; return;
        case 0xe7: ls3Language = 0; ls3Confirmation("English mode."); return;
        case 0xe6: ls3Language = 1; ls3Confirmation("Castilian spanish mode."); return;
        case 0xe5: ls3Getters = 1; ls3Confirmation("Fast getters."); return;
        case 0xe0: ls3Getters = 2; ls3Confirmation("Medium getters."); return;
        case 0xe4: ls3Getters = 0; ls3Confirmation("Slow getters."); return;
        case 0xdf: return;                       // stop: nothing is being said
        case 0xde: if (native && !textBuffer.empty()) ls3Speak(textBuffer, ls3Blocking); return;
        case 0xdd: case 0xdc: {
            static const char* days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
            static const char* months[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                            "September", "October", "November", "December" };
            const std::tm t = hostTime();
            char b[96];
            if (v == 0xdd) std::snprintf(b, sizeof b, "It is %d o clock, %d minutes, and %d seconds.", t.tm_hour, t.tm_min, t.tm_sec);
            else std::snprintf(b, sizeof b, "It is %s, %s %d 20%02d.", days[t.tm_wday], months[t.tm_mon], t.tm_mday, t.tm_year % 100);
            ls3Speak(b, !ls3NonBlockConfirm);
            return;
        }
        case 0xdb: ls3Pending = 3; return;       // set time: hours, minutes, seconds (the host's clock stays)
        case 0xda: ls3Pending = 4; return;       // set date: year, month, date, weekday
        case 0xd2: if (native) ls3Getter(21, false, ""); return;   // no thermometer here: a room's 21 C
        case 0xd1: ls3Speak("Temperature is 21 degrees celsius.", !ls3NonBlockConfirm); return;
        case 0xcf: ls3Getter((ls3Mode & 3) | ls3Blocking << 2 | ls3Language << 3, true, "Current mode"); return;
        case 0xce: ls3Getter(ls3Volume, true, "Current volume"); return;
        case 0xcd: ls3Getter(ls3Voice, true, "Current voice"); return;
        case 0xcc: ls3Getter(ls3Rate, true, "Current speak rate"); return;
        case 0xcb: ls3Getter(ls3Language, true, "Current language"); return;
        case 0xca: ls3Getter(ls3FlushDelay, true, "Current flush delay"); return;
        case 0xc9: ls3Getter(VERSION, false, ""); return;
        default: break;
    }
    if (v >= 0xd3 && v <= 0xd9) {
        if (!native) return;
        const std::tm t = hostTime();
        const int values[] = { t.tm_hour, t.tm_min, t.tm_sec, t.tm_year % 100, t.tm_mon + 1, t.tm_mday, t.tm_wday == 0 ? 7 : t.tm_wday };
        ls3Getter(values[v - 0xd3], false, "");
        return;
    }
    if (v >= 0xb0 && v <= 0xbf) { ls3Voice = v == 0xb0 ? 1 : std::min(9, v - 0xb0); ls3Confirmation("Voice set to " + std::to_string(ls3Voice) + "."); return; }
    if (v >= 0xa0 && v <= 0xaf) { ls3Volume = v == 0xa0 ? 13 : v - 0xa0; ls3Confirmation("Volume set to " + std::to_string(ls3Volume) + "."); return; }
    if (v >= 0x90 && v <= 0x9f) { ls3Rate = v == 0x90 ? 9 : v - 0x90; ls3Confirmation("Speech rate set to " + std::to_string(ls3Rate) + "."); return; }
    if (v >= 0x80 && v <= 0x8f) {
        ls3FlushDelay = v == 0x80 ? 10 : v - 0x80;
        ls3Confirmation("Flush buffer after " + std::to_string(10 + ls3FlushDelay * 10) + " milliseconds.");
        return;
    }
    // &C5 / &C2 (the port echo test and the built-in PCM test program): not here.
}

// ---- the EEPROM, the PCM DAC, the emulated SSA-1 -------------------------------------------

void SpeechSynth::setEepromFile(const std::string& path) {
    eepromPath = path;
    if (path.empty()) return;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        Bytes data(EEPROM_SIZE, 0xff);
        const size_t n = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
        if (n == EEPROM_SIZE) eeprom = data;
    }
}
void SpeechSynth::saveEeprom() {
    if (eepromPath.empty()) return;
    if (FILE* f = std::fopen(eepromPath.c_str(), "wb")) { std::fwrite(eeprom.data(), 1, eeprom.size(), f); std::fclose(f); }
}

// The Amdrum mode's loop: a new byte on the bus goes to the PWM, clipped to 5..250, and the
// sequence 131 133 131 133 128 129 135 resets the board ("4 6 4 6 1 2 8" in its source).
void SpeechSynth::ls3Amdrum(int v) {
    if (v == amdrumLast) return;
    amdrumLast = v;
    const int clipped = std::max(5, std::min(250, v));
    pcmSample = (clipped - 127) / 127.0f;
    if ((v == 128 && amdrumExit == 4) || (v == 129 && amdrumExit == 5) || (v == 131 && (amdrumExit == 0 || amdrumExit == 2))
        || (v == 133 && (amdrumExit == 1 || amdrumExit == 3))) {
        amdrumExit++;
    } else if (v == 135 && amdrumExit == 6) {
        ls3Reset();
        Step boot; boot.us = 50000; steps.push_back(boot);
    } else {
        amdrumExit = 0;
    }
}

// eeprom_pcm_play_mode's command: 0, channel, 0, start page, 0, pages, 0, speed: "The zero
// is being used as a synchronization byte". Channel 8 is the SP0256, 255 resets the board.
void SpeechSynth::playByte(int v) {
    if (v == 0) { playLastZero = true; return; }
    if (!playLastZero) return;
    playLastZero = false;
    if (playChannel == 0) {
        if ((v >= 1 && v <= playChannels) || v == 8 || v == 255) playChannel = v;
        else { playLastZero = true; return; }       // not taken: still after a zero
        if (playChannel == 255) { ls3Reset(); Step boot; boot.us = 50000; steps.push_back(boot); }
        return;
    }
    if (playStart == 0) { playStart = v; return; }
    if (playEnd == 0) { playEnd = (v + playStart) & 0xff; return; }
    // the speed: the channel starts (its value 127 until its first sample is due)
    if (playChannel >= 1 && playChannel <= 4) {
        PcmChannel& c = pcmChannels[(size_t)playChannel - 1];
        c.address = (size_t)playStart * EEPROM_PAGE;
        c.end = (size_t)playEnd * EEPROM_PAGE;
        c.speed = v;
        c.sinceUs = 0;
        c.value = 127;
    } else if (playChannel == 8 && hasRom) {
        chip.ald_w((uint8_t)playStart);
    }
    playChannel = playStart = playEnd = 0;
}

void SpeechSynth::playAdvance() {
    const double loop = playLoopUs(playChannels);
    int sum = 127;
    for (int n = 0; n < 4; n++) {
        PcmChannel& c = pcmChannels[(size_t)n];
        if (n < playChannels) {
            if (c.address <= c.end) {
                // speed timer ticks of 0.4 us, seen once a loop pass
                const double due = std::max(1.0, std::ceil((c.speed / 2.5) / loop)) * loop;
                if (++c.sinceUs >= (long long)std::llround(due)) {
                    c.value = (int)eeprom[c.address % EEPROM_SIZE] - 127;
                    c.address++;
                    c.sinceUs = 0;
                }
            } else c.value = 0;
        }
        sum += c.value;
    }
    pcmSample = (std::max(0, std::min(254, sum)) - 127) / 127.0f;
}

// The EEPROM jobs' bytes. Upload: start page, pages, then pages x 512 bytes; the Z80 is held
// while each 256-byte write page is written (z80_halt until EEPROM_WRITE_IN_PROGRESS clears).
// Download: start page, pages; then each write puts the next byte on the bus, the Z80 held
// while each page is read in.
void SpeechSynth::eepromByte(int v) {
    switch (eeStage) {
        case EE_START: case EE_GET_START:
            eeStart = v;
            if (ls3Confirm) ls3Speak("Send number of pages.", false);
            eeStage = eeStage == EE_START ? EE_PAGES : EE_GET_PAGES;
            return;
        case EE_PAGES: {
            eePages = v;
            if (ls3Confirm) ls3Speak("Send " + std::to_string(v) + " pages now.", false);
            eeAddress = (size_t)eeStart * EEPROM_PAGE;
            eeEnd = (size_t)(eeStart + eePages) * EEPROM_PAGE;
            eeStage = EE_DATA;
            if (eeAddress >= eeEnd) eepromByte(-1);          // nothing to send: done at once
            return;
        }
        case EE_DATA: {
            if (v >= 0) {
                eeprom[eeAddress % EEPROM_SIZE] = (uint8_t)v;
                eeAddress++;
                if (eeAddress % 256 == 0) { Step w; w.hold = true; w.us = EEPROM_WRITE_US; steps.push_back(std::move(w)); }
            }
            if (eeAddress < eeEnd) return;
            eeStage = EE_NONE;
            saveEeprom();
            if (ls3Confirm) ls3Speak("Data loaded into EEPROM.", true);
            if (eeTesting) {
                // five plays of the upload, a byte each 50 us (+ its SPI read), 200 ms apart
                if (ls3Confirm) ls3Speak("Testing P C M data.", true);
                Bytes data;
                for (size_t a = (size_t)eeStart * EEPROM_PAGE; a < eeEnd; a++) data.push_back(eeprom[a % EEPROM_SIZE]);
                for (int i = 0; i < 5; i++) {
                    Step play; play.hold = true; play.pcm = data; play.pcmUs = 51; steps.push_back(play);
                    Step gap; gap.hold = true; gap.us = 200000; steps.push_back(gap);
                }
                if (ls3Confirm) ls3Speak("Done testing.", true);
            }
            return;
        }
        case EE_GET_PAGES: {
            eePages = v;
            eeAddress = (size_t)eeStart * EEPROM_PAGE;
            eeEnd = (size_t)(eeStart + eePages) * EEPROM_PAGE;
            eeStage = EE_GET_DATA;
            eeBus = 32;
            if (eeAddress >= eeEnd) { eeStage = EE_NONE; if (ls3Confirm) ls3Speak("EEPROM data retrieved.", false); return; }
            Step read; read.hold = true; read.us = EEPROM_PAGE * 8 / 10; steps.push_back(std::move(read));   // 512 bytes at 10 MHz
            return;
        }
        case EE_GET_DATA: {
            eeBus = eeprom[eeAddress % EEPROM_SIZE];
            eeAddress++;
            if (eeAddress % EEPROM_PAGE != 0) return;
            // the page's last byte stays a getter delay (CPC_READ_DELAY), then the next page
            Step stay; stay.bus = eeBus; stay.us = getterUs(); steps.push_back(std::move(stay));
            if (eeAddress >= eeEnd) {
                eeStage = EE_NONE;
                if (ls3Confirm) ls3Speak("EEPROM data retrieved.", false);
                return;
            }
            Step read; read.hold = true; read.us = EEPROM_PAGE * 8 / 10;
            read.then = [this]() { eeBus = 32; };
            steps.push_back(std::move(read));
            return;
        }
        default: return;
    }
}

void SpeechSynth::speakAllophones() {
    std::string phones;
    for (int a : allophones) { phones += ALLOPHONE_PHONES[a & 63]; phones += ' '; }
    allophones.clear();
    SpeechSegment seg;
    seg.voice = std::max(0, std::min(ls3Voice - 1, 8));
    seg.wpm = 15 + 25 * ls3Rate;
    seg.gain = ls3Volume / 13.0;
    parsePhones(phones, seg.phones);
    bool sound = false;
    for (const SpeechPhone& p : seg.phones) sound |= p.arpabet != "_";
    if (!sound) return;
    Step st;
    st.say.push_back(std::move(seg));
    st.listening = true;                         // DECtalk speaks; the board reads on (cont())
    steps.push_back(std::move(st));
}

void SpeechSynth::ls3Write(int v) {
    v &= 0xff;
    if (!steps.empty() && !steps.front().listening) {
        // Busy: the board is not reading the bus -- except for &DF while it speaks.
        Step& s = steps.front();
        if (v == 0xdf && !s.say.empty()) { steps.clear(); stepUs = 0; ttsSample = 0.0f; }
        return;
    }
    if (ls3Mode == LS3_EEPROM_PLAY) { playByte(v); return; }   // the play loop takes every byte
    if (eeStage != EE_NONE) { eepromByte(v); return; }
    if (ls3Pending > 0) { ls3Pending -= 1; return; }
    if (ls3Mode == LS3_SERIAL) {
        serialWrite(v);
        if (!serialMode) ls3Mode = ls3LastMode;   // &FF,&14 quits back
        return;
    }
    if (v >= 0x80) { ls3Control(v); return; }
    switch (ls3Mode) {
        case LS3_SSA1_SP0: if (hasRom) chip.ald_w((uint8_t)v); return;
        case LS3_DK_SP0: if (hasRom) chip.ald_w((uint8_t)(v & 0x3f)); return;
        case LS3_SSA1: case LS3_DK: {
            // the emulated SSA-1 / dk'tronics: the allophone into the buffer (busy while it is
            // read, "_delay_us(15)"), spoken 10 + flush delay x 10 ms after the last one, or at
            // once when the buffer is full
            if (allophoneBusyUs > 0) return;          // still reading the last one
            allophones.push_back(ls3Mode == LS3_DK ? (v & 0x3f) : v);
            allophoneQuietUs = 0;
            allophoneBusyUs = 18;
            if (allophones.size() >= FLUSH_AT) { speakAllophones(); allophoneQuietUs = -1; }
            return;
        }
        default: break;
    }
    // Epson / DECtalk text, with \xHH
    if (hexState == 0 && v == '\\') { hexState = 1; return; }
    if (hexState == 1) {
        hexState = 0;
        if (v == 'x') { hexState = 2; return; }
        textBuffer += '\\';                      // not an escape after all
    } else if (hexState == 2 || hexState == 3) {
        int d = v >= '0' && v <= '9' ? v - '0' : v >= 'A' && v <= 'F' ? v - 'A' : -1;   // (the firmware's 'A' = 0)
        if (d < 0) { hexState = 0; return; }
        if (hexState == 2) { hexHigh = d; hexState = 3; return; }
        hexState = 0;
        v = hexHigh * 16 + d;
    }
    textBuffer += (char)v;
    if (textBuffer.size() >= FLUSH_AT || v == 13) {
        std::string say = textBuffer;
        say.pop_back();                          // speak_buffer: buffer[length-1] = 0
        textBuffer.clear();
        ls3Speak(say, ls3Blocking);
    }
}

void SpeechSynth::ls3Advance() {
    // the emulated SSA-1's timer: "load me" again after 14 ms, spoken after the flush delay
    if (allophoneBusyUs > 0) allophoneBusyUs--;
    if (allophoneQuietUs >= 0 && allophoneBusyUs == 0) {
        allophoneQuietUs++;
        if (allophoneQuietUs >= (10 + ls3FlushDelay * 10) * 1000LL) { allophoneQuietUs = -1; speakAllophones(); }
    }
    if (ls3Mode == LS3_EEPROM_PLAY) playAdvance();
    if (steps.empty()) { ttsSample = 0.0f; return; }
    Step& s = steps.front();
    bool finished = false;
    if (!s.say.empty()) {
        if (!s.started) {
            s.started = true;
            lastSaid = s.say;
            s.job = HostSpeech::render(s.say);
        }
        if (s.job && s.job->done) {
            if (s.job->pcm.empty()) {
                // No voice on this host: the board still takes the time a sentence takes.
                if (!s.job->error.empty()) speechError = s.job->error;
                size_t chars = 0;
                for (const SpeechSegment& g : s.say) chars += g.text.size() + g.phones.size();
                stepUs += 1;
                finished = stepUs >= (long long)chars * 60000;
                ttsSample = 0.0f;
            } else {
                s.playPhase += s.job->rate / 1e6;
                while (s.playPhase >= 1.0) { s.playPhase -= 1.0; s.played += 1; }
                ttsSample = s.played < s.job->pcm.size() ? s.job->pcm[s.played] / 32768.0f : 0.0f;
                finished = s.played >= s.job->pcm.size();
            }
        }
    } else if (!s.pcm.empty()) {
        const size_t at = (size_t)(stepUs / std::max(1, s.pcmUs));
        pcmSample = at < s.pcm.size() ? ((int)s.pcm[at] - 127) / 127.0f : 0.0f;
        stepUs += 1;
        finished = at >= s.pcm.size();
        if (finished) pcmSample = 0.0f;
    } else {
        stepUs += 1;
        finished = stepUs >= s.us;
    }
    if (finished) {
        std::function<void()> then = std::move(s.then);
        steps.pop_front(); stepUs = 0; ttsSample = 0.0f;
        if (then) then();
    }
}

} // namespace cpcse
