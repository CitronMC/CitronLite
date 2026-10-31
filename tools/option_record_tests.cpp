// Which memory counts as the game's FOV option.
//
// The three vectors below are the 48 bytes at each place the FOV byte pattern matched in a running
// Minecraft 1.26.52.03, read by a separate process and pasted here unchanged. One is the real option
// and two are false matches that zoom used to accept (it took the first match and wrote there). The
// image bounds are that game's module, because a real option's vtable has to point into it.
#include "citron/bedrock.h"
#include "citron/option_record.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

constexpr uint64_t kImageBase = 0x7FF65BCB0000ull;
constexpr uint64_t kImageSize = 314576896ull;

// Real option record at 0x212C54DB030: min 30, max 110, value 100, default 60, step 0.001.
const uint8_t kReal[48] = {
    0xF0, 0x8E, 0x42, 0x6A, 0xF6, 0x7F, 0x00, 0x00, 0x40, 0xDF, 0x81, 0xC5, 0x12, 0x02, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x41, 0x00, 0x00, 0xDC, 0x42, 0x00, 0x00, 0xC8, 0x42, 0x00, 0x00, 0x70, 0x42,
    0x6F, 0x12, 0x83, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x7B, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// False match at 0x2122AA005E2: odd address, random data, and the 70.0 the old zoom wrote into it.
const uint8_t kJunkA[48] = {
    0x00, 0x00, 0x20, 0xFE, 0xFF, 0xFF, 0xD0, 0xFF, 0xFF, 0xFF, 0xB0, 0xFD, 0xFF, 0xFF, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x42, 0x00, 0x00, 0x8C, 0x42, 0x00, 0x00, 0x70, 0x42,
    0x6F, 0x12, 0x14, 0x02, 0x00, 0x00, 0x3E, 0x3D, 0x3B, 0x37, 0x2F, 0x1F, 0x00, 0x01, 0x00, 0x00 };

// False match at 0x2122AA0271A: odd address again, pointers and a repeated pattern.
const uint8_t kJunkB[48] = {
    0x01, 0x2A, 0x00, 0x00, 0x70, 0x41, 0xD0, 0x05, 0xA0, 0x2A, 0x12, 0x02, 0x00, 0x00, 0xC0, 0x05,
    0xA0, 0x2A, 0x12, 0x02, 0x00, 0x00, 0x80, 0x42, 0x6F, 0x12, 0x14, 0x02, 0x00, 0x00, 0x70, 0x42,
    0x6F, 0x12, 0x14, 0x02, 0x00, 0x00, 0xD0, 0xC1, 0x1E, 0x5E, 0x14, 0x02, 0x00, 0x00, 0xC0, 0xC1 };

bool Looks(const uint8_t* raw, uint64_t record, float* value = nullptr) {
    return citron::optrec::LooksLikeFovOption(raw, record, kImageBase, kImageSize, value);
}

// A copy of the real record with one thing changed, for the cases a game update could produce.
struct Edited {
    uint8_t raw[48];
    explicit Edited(void (*edit)(uint8_t*)) { std::memcpy(raw, kReal, sizeof(raw)); edit(raw); }
};

void SetF(uint8_t* raw, size_t at, float v) { std::memcpy(raw + at, &v, 4); }

// The options table in this process, laid out as the game's, read the way the game's is.
struct FakeOption {
    uint8_t raw[0x40] = {};
    uint8_t details[0x240] = {};
    FakeOption() {
        const uintptr_t at = (uintptr_t)details;
        std::memcpy(raw + citron::optrec::kDetails, &at, sizeof at);
    }
    // std::string as the game's compiler lays it out: the characters, or a pointer to them, then size and capacity
    void Name(const char* name) {
        uint8_t* s = details + citron::optrec::kSaveName;
        const uint64_t size = std::strlen(name), cap = size < 16 ? 15 : 31;
        if (size < 16) {
            std::memcpy(s, name, size);
        } else {
            std::memcpy(s, &name, sizeof name);
        }
        std::memcpy(s + 0x10, &size, 8);
        std::memcpy(s + 0x18, &cap, 8);
    }
    void StoodInFor(const FakeOption& by) {
        const uintptr_t at = (uintptr_t)by.raw;
        std::memcpy(details + citron::optrec::kStandIn, &at, sizeof at);
    }
};

void OptionTable() {
    using namespace citron;
    static uintptr_t options[optrec::kTable / 8 + optrec::kTableIds] = {};
    static FakeOption audio, gamma, fov, standIn;
    audio.Name("audio_main");
    gamma.Name("gfx_gamma");
    fov.Name("gfx_field_of_view");
    standIn.Name("gfx_gamma_override");
    auto put = [](size_t id, FakeOption& o) { options[optrec::kTable / 8 + id] = (uintptr_t)o.raw; };
    put(3, audio);
    put(47, fov);      // ids with holes before them, as the real table has
    put(50, gamma);

    engine::Proc proc;
    proc.h = GetCurrentProcess();
    proc.base = reinterpret_cast<uint8_t*>(options);
    proc.size = sizeof options;
    proc.pid = GetCurrentProcessId();
    bedrock::Reader rd;
    rd.Attach(proc, (uintptr_t)options, game::Layout{});

    uintptr_t record = 0;
    Require(rd.OptionIn((uintptr_t)options, "gfx_gamma", record) && record == (uintptr_t)gamma.raw, "a short name was not found");
    Require(rd.OptionIn((uintptr_t)options, "gfx_field_of_view", record) && record == (uintptr_t)fov.raw, "a long name was not found");
    Require(!rd.OptionIn((uintptr_t)options, "gfx_gamm", record), "the start of a name was taken for it");
    Require(!rd.OptionIn((uintptr_t)options, "gfx_gamma2", record), "a longer name was taken for it");
    Require(!rd.OptionIn((uintptr_t)options, "game_difficulty", record), "an option that is not there was found");

    gamma.StoodInFor(standIn);
    Require(rd.OptionIn((uintptr_t)options, "gfx_gamma", record) && record == (uintptr_t)standIn.raw, "the option standing in was not followed");
    put(823, audio);
    options[optrec::kTable / 8 + 3] = 0;
    Require(rd.OptionIn((uintptr_t)options, "audio_main", record) && record == (uintptr_t)audio.raw, "the end of the table was not reached");
}

} // namespace

int main() {
    OptionTable();

    // The three records from the live game.
    float value = 0;
    Require(Looks(kReal, 0x212C54DB030ull, &value), "the real FOV option was rejected");
    Require(value == 100.0f, "the real option's value was misread");
    Require(!Looks(kJunkA, 0x2122AA005E2ull), "a false match at an odd address was accepted");
    Require(!Looks(kJunkB, 0x2122AA0271Aull), "a second false match at an odd address was accepted");

    // The real bytes at an address no object could start at.
    Require(!Looks(kReal, 0x212C54DB031ull), "a misaligned address was accepted");
    Require(!Looks(kReal, 0x212C54DB034ull), "a 4-aligned but not 8-aligned address was accepted");

    // A vtable that does not point into the game.
    {
        Edited e([](uint8_t* r) { std::memset(r, 0x11, 8); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "a vtable outside the game image was accepted");
    }
    {
        Edited e([](uint8_t* r) { std::memset(r, 0, 8); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "a null vtable was accepted");
    }

    // The fields themselves.
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kMin, 110.f); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "min above max was accepted");
    }
    // Custom FOV and Zoom write values past the slider's ends; the record is still the FOV.
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kValue, 170.f); });
        Require(Looks(e.raw, 0x212C54DB030ull), "a Custom FOV value above the slider was rejected");
    }
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kValue, 10.f); });
        Require(Looks(e.raw, 0x212C54DB030ull), "a zoomed value below the slider was rejected");
    }
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kValue, 200.f); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "a value no field of view has was accepted");
    }
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kValue, 0.5f); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "a value near zero was accepted");
    }
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kStep, 0.f); });
        Require(!Looks(e.raw, 0x212C54DB030ull), "a zero step was accepted");
    }
    {
        Edited e([](uint8_t* r) { std::memset(r + citron::optrec::kValue, 0xFF, 4); });   // NaN
        Require(!Looks(e.raw, 0x212C54DB030ull), "a NaN value was accepted");
    }

    // A different float option is not the FOV: a brightness-style record, 0 to 1.
    {
        Edited e([](uint8_t* r) {
            SetF(r, citron::optrec::kMin, 0.f); SetF(r, citron::optrec::kMax, 1.f);
            SetF(r, citron::optrec::kValue, 0.5f); SetF(r, citron::optrec::kDefault, 0.5f);
        });
        Require(!Looks(e.raw, 0x212C54DB030ull), "another float option was accepted as the FOV");
    }

    // A game patch that nudges the slider's ends must not silently turn zoom off.
    {
        Edited e([](uint8_t* r) { SetF(r, citron::optrec::kMin, 20.f); SetF(r, citron::optrec::kMax, 120.f); });
        Require(Looks(e.raw, 0x212C54DB030ull), "a slightly wider FOV range was rejected");
    }

    // The value is optional, and a null image base never matches.
    Require(Looks(kReal, 0x212C54DB030ull), "the value pointer is meant to be optional");
    Require(!citron::optrec::LooksLikeFovOption(kReal, 0x212C54DB030ull, 0, 0), "a missing image was accepted");

    std::cout << "option record tests passed\n";
    return 0;
}
