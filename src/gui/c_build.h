// CPCSyntaxError GUI — C programs for the CPC: SDCC, with CPCtelera's library, run from the
// assembler window.
//
// SDCC is GPL, so it is not linked in: it stays its own programs (sdcc, sdasz80, sdar) in a
// folder the user names, and this file runs them. CPCtelera (its "next" branch,
// github.com/Arnaud6128/cpctelera/tree/cpctelera_next: SDCC 4.6.0, __sdcccall(1), LGPL v3)
// builds with bash, make and, on Windows, Cygwin; here the same commands are run directly,
// so a CPCtelera folder needs no setup at all:
//
//   library  every src/**/*.s assembled from the CPCtelera folder (the sources include
//            "src/macros/..."): sdasz80 -I<cpct>/src/ -l -o -s <obj>.rel <src>.s, then
//            sdar rc cpctelera.lib (its Makefile and cfg/build_config.mk). Built once into a
//            cache, again when SDCC or the sources change.
//   project  every .c and .s under the project's source folder (CPCtelera: SRCDIR of
//            cfg/build_config.mk, else the main file's folder):
//              sdcc -I<cpct>/src -I<srcdir> -mz80 <Z80CCFLAGS> -c x.c -o obj/x.rel
//              sdasz80 -l -o -s -I<cpct>/src obj/x.rel x.s
//            linked as the project template does:
//              sdcc -mz80 --no-std-crt0 -Wl-u --code-loc <Z80CODELOC> --data-loc 0
//                   -l<cpctelera.lib> <objects> -o obj/<project>.ihx
//   result   the Intel HEX's bytes from its lowest address (hex2bin -p 00: gaps are 0); the
//            program starts at _main, read from the link map (CPCtelera's GETRUNADDRESS).
//
// Without a CPCtelera folder the same steps build plain SDCC C, with no library.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cpcse {

struct CToolchain {
    std::string sdccFolder;          // SDCC's folder (or its bin); "" = look in the usual places
    std::string cpcteleraFolder;     // CPCtelera (the repository or its cpctelera folder); "" = none
    int codeLoc = 0x4000;            // when the project has no cfg/build_config.mk (CPCtelera's default)
    std::string extraFlags;          // added to every sdcc -c
    // A CPCtelera project's asset steps (cpct_assets.h) run before the compile, each when its
    // outputs are older than its inputs. forceAssets: all of them; assetsOnly: stop there.
    bool forceAssets = false, assetsOnly = false;
};

struct CBuildMessage {
    std::string file;                // "" = not about a line of a file (the linker, a tool)
    int line = 0;
    std::string text;
    bool error = true;
};

struct CBuildResult {
    bool ok = false;
    std::vector<CBuildMessage> messages;
    std::vector<uint8_t> bytes;
    int loadAddress = -1, runAddress = -1;
    std::vector<std::pair<std::string, int>> symbols;   // from the link map
    std::string log;                 // every command and what it printed
    std::string binPath;             // the binary written beside the objects
    std::string project;             // the project's name
};

// SDCC's bin folder: the folder given or its bin, else the usual installs, else the PATH.
// "" when no sdcc is found.
std::string findSdccBin(const std::string& folder);
// CPCtelera's own folder (the one holding src/cpctelera.h), from the repository or itself.
std::string findCpctelera(const std::string& folder);

// Build the project the main file belongs to. Runs the tools on threads; calls progress
// with a line now and then (from those threads). Safe to run off the UI thread.
CBuildResult buildC(const CToolchain& tools, const std::string& mainPath, const std::string& cacheFolder,
                    const std::function<void(const std::string&)>& progress);

// Runs a program (no window); its standard output and error together into `output`. The
// exit code, or -1 when it could not be started.
int runProgram(const std::vector<std::string>& args, const std::string& cwd, std::string& output);

// Exposed for the check: one tool line of output read as a message (false: not one).
bool parseToolMessage(const std::string& line, CBuildMessage& out);
// The bytes of an Intel HEX file from its lowest address; false when it is not one.
bool readIntelHex(const std::string& text, std::vector<uint8_t>& bytes, int& lowest);

} // namespace cpcse
