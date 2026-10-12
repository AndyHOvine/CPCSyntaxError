// CPCSyntaxError — what the TUBE does with the RGB the GATE ARRAY drives.
//
// This is the monitor's video stage and nothing else. It is handed the R, G and B the
// GATE ARRAY (or, on a Plus, the ASIC) is putting on pins 1, 2 and 3 of the DIN6 and it
// answers with what the screen shows. It must not look an ink up: which colour a pen is
// is the GATE ARRAY's question (GateArray::rgbForHardwareColour, ACCC §9.1) and it used
// to be answered TWICE, with a private 32-entry copy of that chip's table living here.
//
// A COLOUR SET (CTM 640/644, CM14) shows the RGB it is given: three guns, three signals,
// nothing to transform.
//
// A GREEN SET (GT 64/65) has ONE gun, and it is not fed by a TV luminance matrix. It is
// fed by the CPC's own LUM pin, which the machine builds itself: "These signals are mixed
// using resistors R131, R132 and R133, as well as R195, and amplified by transistor Q102.
// The resulting output signal is the LUM signal, which serves as a video signal for the
// green screen" (The Anatomy of the CPCs, p.92). The GT 65 takes it linearly -- "analogue
// voltage input which is linear between 0.8V (Black) and 1.75V (Peak white)" (Amstrad's
// own Arnold/Plus design spec, which gives the MM12 the same input).
//
// THE LUM CIRCUIT, from the service manuals' schematics (CPC464 and CPC6128, the same
// mixer): the GATE ARRAY's R, G and B pins go through R131 3.3k, R132 1k and R133 10k to
// the base of Q102 (2SC1815, an emitter follower); R135 680R brings the GATE ARRAY's
// C-SYNC (/SYNC) into the same node, so LUM is the picture AND the composite sync on one
// wire; R134/R136 (464: 100R/220R, 6128: 180R/150R) divide the emitter down to pin 6. No
// capacitor is in that path (the 6128's C139-C141 sit on the R, G, B outputs).
// A resistor mixer weights each input by its conductance: G:R:B = 1/1k : 1/3.3k : 1/10k =
// 330 : 100 : 33. In the picture /SYNC is high and only adds a constant pedestal, which
// the tube's black level takes out; in the sync the GATE ARRAY has blanked the guns. The
// monitor takes its sync from the GATE ARRAY's C-SYNC pin directly: the same signal.
// Cross-check (6128 values, Vbe ~0.65 V, no load): all guns low gives LUM ~0.86 V, all
// high ~2.0 V, against the Arnold spec's 0.8 V black and 1.75 V peak white.
// The weights keep the documented order -- "on a green screen ... the colours (in the
// software/firmware colours) are in order of INCREASING INTENSITY" (CPCWiki, gate array):
// green outweighs two reds and two blues (330 > 2x100 + 2x33) and red outweighs two blues
// (100 > 2x33), so the 27 colours are 27 distinct steps in firmware order. An earlier
// version used 9:3:1, derived from that order alone; the resistors say 330:100:33.
//
// The TV matrix that stood here (0.299R + 0.587G + 0.114B) put 13 of the 351 colour
// pairs out of that order -- bright magenta (8) brighter than green (9) -- and drew
// firmware 7 and 12, and 20 and 21, as the same shade. Measured, not argued: see
// cpcse-ga-check's green-screen order case.
//
// The level is LINEAR in the gun voltages, so it is formed on the voltages, not on the
// displayed RGB this table holds. That RGB already carries the tube's own response -- the
// GATE ARRAY's 50% level is drawn as 0x63, not 0x80 -- so it is taken back out with the
// curve that 0x63 implies, the sum is formed, and the SAME curve is put back on the way to
// the phosphor. No new constant: the curve is whatever the colour table already says a
// CPC tube does with half a volt.
//
// That single curve replaces a second, separate green model: a 32-entry table of greens,
// inherited from an earlier version of this code, that was used for GATE ARRAY inks
// while the curve was used for everything else. The two disagreed by up to 72 of 255 per
// channel on the same tube (measured), the table was not monotonic in luminance, and it
// put BLACK at green 88 of 255 -- a tube whose gun is off is black.
//
// THE PHOSPHORS ARE TYPICAL VALUES, NOT AMSTRAD FACTS (the user chose this, 2026-10-12).
// The service manuals give the GT 64's tube only as "V701 310GNB31" and name no phosphor;
// no datasheet for that type was found. The green is a copper-activated zinc sulphide
// (P31 is ZnS:Cu, the colour sets' P22 green is ZnS:Cu,Al: the JEDEC/EIA table of CRT
// phosphors), so the GT is drawn at a MEASURED P22 green: CIE x 0.285, y 0.605 (a KDS VS19,
// in the UCF CAP5725 chromaticity table). Black is black: a gun that is off lights nothing.
// The MM12 is "paper white" (Amstrad's Arnold spec): drawn neutral, because a 9300 K CRT
// white would be visibly blue and the name says paper. The colour sets keep the GATE
// ARRAY's RGB: P22 primaries are what sRGB was based on.
//
// THE FRONT CONTROLS (the service manuals' parts lists): every set has a Brightness knob;
// only the monochrome sets (GT 64/65: "Contrast Pot." 170518, "Brightness Pot." 170519;
// MM12: "bright and contrast controls") have Contrast. Two calibration points are printed:
//   - CM14 2-3 CUT OFF: with the bright control at MAXIMUM and no signal, the line is lit
//     "faintly" -- so full brightness lifts black just past cut-off;
//   - MM12 2-4 SUB BRIGHT: with bright AND contrast at MINIMUM, the letters "can be seen
//     slightly" -- so the bottom of both still leaves a dim picture.
// Brightness moves the black level of the drive, contrast scales the drive; the knob
// ranges below are chosen to meet those two points (they are not printed values).
#include "monitor_palette.h"
#include <cmath>

namespace cpcse {

const std::string MONITOR_MODE_COLOUR = "colour";
const std::string MONITOR_MODE_GREEN = "green";
const std::string MONITOR_MODE_MONO = "mono";

static std::string toLower(const std::string& s) { std::string r = s; for (auto& ch : r) ch = (char)std::tolower((unsigned char)ch); return r; }

std::string normaliseMonitorMode(const std::string& mode) {
    std::string value = toLower(mode);
    if (value == MONITOR_MODE_GREEN) return MONITOR_MODE_GREEN;
    if (value == MONITOR_MODE_MONO || value == "bw" || value == "grey" || value == "gray") return MONITOR_MODE_MONO;
    return MONITOR_MODE_COLOUR;
}

// The tube's response, as the GATE ARRAY's colour table implies it: a 50% drive is shown
// at 0x63/0xFF, so displayed = drive ^ gamma with gamma = ln(0x63/255) / ln(0.5).
static double tubeGamma() {
    static const double g = std::log(0x63 / 255.0) / std::log(0.5);
    return g;
}
// The CPC's LUM pin for an RGB the GATE ARRAY (or the Plus ASIC) is driving, as a displayed
// level 0..1 -- see the header comment: G:R:B = 9:3:1 on the linear gun drives.
static double cpcLumDisplayed(int rgb) {
    auto drive = [](int c) { return c <= 0 ? 0.0 : std::pow(c / 255.0, 1.0 / tubeGamma()); };
    const double r = drive((rgb >> 16) & 0xff), g = drive((rgb >> 8) & 0xff), b = drive(rgb & 0xff);
    const double lum = (LUM_WEIGHT_G * g + LUM_WEIGHT_R * r + LUM_WEIGHT_B * b) / LUM_WEIGHT_SUM;
    return std::pow(lum, tubeGamma());
}


static int pack(int r, int g, int b) { return (r << 16) | (g << 8) | b; }
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
static double srgbEncode(double v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }

// A phosphor's colour at full drive, as linear sRGB scaled so its largest channel is 1:
// CIE xy to XYZ (Y = 1), then the sRGB matrix; what lies outside sRGB is clipped.
static std::array<double, 3> phosphorLinear(double x, double y) {
    const double X = x / y, Y = 1.0, Z = (1 - x - y) / y;
    std::array<double, 3> c = { 3.2406 * X - 1.5372 * Y - 0.4986 * Z,
                                -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
                                0.0557 * X - 0.2040 * Y + 1.0570 * Z };
    double top = 0;
    for (double& v : c) { v = std::max(0.0, v); top = std::max(top, v); }
    for (double& v : c) v /= top;
    return c;
}

// The knobs (0..100) as the drive stage sees them: an offset on the black level and a gain.
// Brightness 0 -> -0.20, 50 -> 0, 100 -> +0.06: at the top a black (drive 0) is displayed at
// 0.06^gamma = about 2 %, "faintly" lit. Contrast 0 -> gain 0.3, 100 -> 1: with both at 0 a
// full white drives 0.3 - 0.2 = 0.1, displayed at about 4 %, letters "seen slightly".
static double brightnessOffset(int knob) {
    knob = std::max(0, std::min(100, knob));
    return knob < 50 ? -0.20 * (50 - knob) / 50.0 : 0.06 * (knob - 50) / 50.0;
}
static double contrastGain(int knob) { return 0.3 + 0.7 * std::max(0, std::min(100, knob)) / 100.0; }

// The output intensity (0..200 %) as a gain on the light of one channel.
static double intensityGain(int knob) { return std::max(0, std::min(200, knob)) / 100.0; }

static constexpr int LUM_STEPS = 4096;

void TubeTransform::configure(const std::string& mode, const TubeKnobs& knobs) {
    const std::string m = normaliseMonitorMode(mode);
    const double offset = brightnessOffset(knobs.brightness), gain = contrastGain(knobs.contrast);
    const double gamma = tubeGamma();
    colour = m == MONITOR_MODE_COLOUR;
    identity = colour && knobs.isDefault();
    auto drive = [&](int c) { return c <= 0 ? 0.0 : std::pow(c / 255.0, 1.0 / gamma); };
    auto shown = [&](double d) { return std::pow(clamp01(gain * d + offset), gamma); };
    // Last of all, and outside the set: each channel's output intensity, on the light.
    auto output = [&](double displayed, size_t k) {
        const double g = intensityGain(knobs.intensity[k]);
        return g == 1.0 ? displayed : srgbEncode(clamp01(srgbDecode(displayed) * g));
    };
    if (colour) {
        for (size_t k = 0; k < 3; k++)
            for (int c = 0; c < 256; c++)
                channelOut[k][(size_t)c] = (int)std::lround(output(shown(drive(c)), k) * 255.0);
        return;
    }
    // The LUM pin: the resistor mixer's weighted sum on the linear drives, in LUM_STEPS steps.
    for (int c = 0; c < 256; c++) lumDrive[(size_t)c] = (int)std::lround(drive(c) * (LUM_STEPS - 1));
    const std::array<double, 3> phosphor = m == MONITOR_MODE_GREEN ? phosphorLinear(0.285, 0.605)
                                                                   : std::array<double, 3>{ 1.0, 1.0, 1.0 };
    levelOut.assign(LUM_STEPS, 0);
    for (int s = 0; s < LUM_STEPS; s++) {
        const double light = srgbDecode(shown(s / double(LUM_STEPS - 1)));
        int out[3];
        for (size_t k = 0; k < 3; k++) out[k] = (int)std::lround(output(srgbEncode(phosphor[k] * light), k) * 255.0);
        levelOut[(size_t)s] = pack(out[0], out[1], out[2]);
    }
}

int monitorTransformRgb(int rgb, const std::string& mode) {
    TubeTransform tube;
    tube.configure(mode, TubeKnobs());
    return tube.apply(rgb);
}

int monitorLumLevel(int rgb) { return (int)std::lround(cpcLumDisplayed(rgb) * 255.0); }


} // namespace cpcse
