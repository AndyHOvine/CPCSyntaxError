// CPCSyntaxError GUI — a PNG reader (the asset conversions read CPCtelera's images).
//
// The PNG specification (W3C, second edition): every colour type (grey, RGB, palette,
// grey + alpha, RGBA) at every bit depth it allows (1, 2, 4, 8, 16), the five row filters,
// Adam7 interlacing, and tRNS transparency. The image comes back as 8-bit RGBA, rows top
// down, as FreeImage_ConvertTo32Bits gives it to img2cpc: a palette entry's alpha from tRNS,
// 255 when there is none; 16-bit samples keep their high byte. The zlib stream (RFC 1950)
// is inflated by this file's own decoder (RFC 1951: stored, fixed and dynamic Huffman).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cpcse {

// False (and `why`) for a file that is not a PNG this reader takes.
bool decodePng(const std::vector<uint8_t>& file, int& width, int& height, std::vector<uint8_t>& rgba, std::string& why);

} // namespace cpcse
