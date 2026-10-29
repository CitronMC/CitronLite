// Which memory counts as the game's FOV option.
//
// The three vectors below are the 48 bytes at each place the FOV byte pattern matched in a running
// Minecraft 1.26.52.03, read by a separate process and pasted here unchanged. One is the real option
// and two are false matches that zoom used to accept (it took the first match and wrote there). The
// image bounds are that game's module, because a real option's vtable has to point into it.
#include "citron/option_record.h"

#include <cstdlib>
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

} // namespace

int main() {
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
