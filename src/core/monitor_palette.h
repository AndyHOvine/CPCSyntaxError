// CPCSyntaxError — Monitor colour transforms.
// The TUBE: colour, green phosphor or monochrome, as one transform on the RGB pins, with the
// set's front controls (Brightness, and on the monochrome sets Contrast).
#pragma once
#include "common.h"
#include <array>

namespace cpcse {

extern const std::string MONITOR_MODE_COLOUR;
extern const std::string MONITOR_MODE_GREEN;
extern const std::string MONITOR_MODE_MONO;

std::string normaliseMonitorMode(const std::string& mode);
// What the tube shows for the RGB the GATE ARRAY is driving, with the knobs at their
// default. There is no ink-indexed version of this: which colour a pen is belongs to that
// chip (ACCC 9.1), and the tube only ever sees three signals.
int monitorTransformRgb(int rgb, const std::string& mode);
// The green/mono set's displayed level (0..255) for a GATE ARRAY RGB: the CPC's LUM pin
// through the tube. Exposed for the oracle that checks the documented firmware order.
int monitorLumLevel(int rgb);

// The CPC's LUM mixer (CPC464/6128 schematics): R131 3.3k, R132 1k, R133 10k into Q102's
// base, so the weights are the conductances, G:R:B = 330:100:33. See monitor_palette.cpp.
constexpr int LUM_WEIGHT_G = 330, LUM_WEIGHT_R = 100, LUM_WEIGHT_B = 33;
constexpr int LUM_WEIGHT_SUM = LUM_WEIGHT_G + LUM_WEIGHT_R + LUM_WEIGHT_B;

// THE CONTROLS, as knob positions (what the GUI and the .ini store). At the defaults the
// transform is the plain tube (a colour tube passes the RGB through untouched).
constexpr int MONITOR_BRIGHTNESS_DEFAULT = 50;     // front knob, 0..100
constexpr int MONITOR_CONTRAST_DEFAULT = 100;      // front knob, 0..100 (monochrome sets)
constexpr int MONITOR_INTENSITY_DEFAULT = 100;     // per channel, 0..200 %
struct TubeKnobs {
    int brightness = MONITOR_BRIGHTNESS_DEFAULT;
    int contrast = MONITOR_CONTRAST_DEFAULT;
    // R, G, B INTENSITY OF THE OUTPUT: not a part of the set. A gain on each channel of the
    // light the tube gives, after everything above, so one monitor's colours can be trimmed
    // to match a real one (or a photograph). The tube model itself is not touched; 100 % is
    // the output unchanged. Kept per monitor set by the front end.
    std::array<int, 3> intensity{ MONITOR_INTENSITY_DEFAULT, MONITOR_INTENSITY_DEFAULT, MONITOR_INTENSITY_DEFAULT };
    bool isDefault() const {
        return brightness == MONITOR_BRIGHTNESS_DEFAULT && contrast == MONITOR_CONTRAST_DEFAULT &&
               intensity == TubeKnobs().intensity;
    }
};

// One tube with its knobs, built once when a setting changes and then asked per pixel:
// tables, no pow() on the drawing path.
class TubeTransform {
public:
    TubeTransform() { configure(MONITOR_MODE_COLOUR, TubeKnobs()); }
    void configure(const std::string& mode, const TubeKnobs& knobs);
    int apply(int rgb) const {
        if (identity) return rgb & 0xffffff;
        if (colour) return channelOut[0][(rgb >> 16) & 0xff] << 16 | channelOut[1][(rgb >> 8) & 0xff] << 8 | channelOut[2][rgb & 0xff];
        const int sum = LUM_WEIGHT_G * lumDrive[(rgb >> 8) & 0xff] + LUM_WEIGHT_R * lumDrive[(rgb >> 16) & 0xff] +
                        LUM_WEIGHT_B * lumDrive[rgb & 0xff];
        return levelOut[(size_t)(sum / LUM_WEIGHT_SUM)];
    }
private:
    bool identity = true, colour = true;
    std::array<std::array<int, 256>, 3> channelOut{};   // colour tube: a gun's byte in, its byte out
    std::array<int, 256> lumDrive{};        // monochrome: a gun's linear drive, 0..LUM_STEPS-1
    std::vector<int> levelOut;              // monochrome: the LUM drive in, the packed RGB out
};

} // namespace cpcse
