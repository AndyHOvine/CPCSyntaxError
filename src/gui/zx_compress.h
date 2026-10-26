// CPCSyntaxError GUI — the compressors CPCtelera's cpct_pack runs (tools/zx0, tools/zx1,
// tools/zx7b in CPCtelera), as functions, for the asset steps when those programs are not
// built (CPCtelera ships them as C sources only).
//
// Each gives the same bytes as the program with cpct_pack's switches:
//   zx0  -f        ZX0 v2.2 by Einar Saukas (BSD-3)    zx0b  zx0 -f -b
//   zx1  -f        ZX1 v1.5 by Einar Saukas (BSD-3)    zx1b  zx1 -f -b
//   zx7b           ZX7 Backwards v1.01 by Antonio Villena, from ZX7 by Einar Saukas (BSD-3)
// The licence notices are in zx_compress.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cpcse {

// method: "zx0", "zx0b", "zx1", "zx1b" or "zx7b". False (and why) for an unknown method or
// empty data.
bool zxCompress(const std::string& method, const std::vector<uint8_t>& data, std::vector<uint8_t>& out, std::string& why);

} // namespace cpcse
