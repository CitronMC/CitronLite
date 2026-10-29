#include "citron/pe_signature_scan.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<uint8_t> Image() {
    std::vector<uint8_t> image(0x1000, 0);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    auto* sections = IMAGE_FIRST_SECTION(nt);
    std::memcpy(sections[0].Name, ".text", 5);
    sections[0].VirtualAddress = 0x400;
    sections[0].Misc.VirtualSize = 0x100;
    sections[0].SizeOfRawData = 0x100;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    std::memcpy(sections[1].Name, ".rdata", 6);
    sections[1].VirtualAddress = 0x600;
    sections[1].Misc.VirtualSize = 0x100;
    sections[1].SizeOfRawData = 0x100;
    sections[1].Characteristics = IMAGE_SCN_MEM_READ;
    return image;
}

} // namespace

int main() {
    const std::vector<int> pattern{0x48, 0x8B, -1, 0x90, 0xCC};
    auto image = Image();
    const uint8_t bytes[]{0x48, 0x8B, 0x7F, 0x90, 0xCC};
    std::memcpy(image.data() + 0x440, bytes, sizeof(bytes));
    std::memcpy(image.data() + 0x620, bytes, sizeof(bytes)); // non-executable: ignored

    auto one = citron::sigscan::FindExecutableMatches(
        image.data(), image.size(), 0x140000000ull, pattern, 2);
    assert(one.exact(1));
    assert(one.addresses.front() == 0x140000440ull);

    std::memcpy(image.data() + 0x470, bytes, sizeof(bytes));
    auto ambiguous = citron::sigscan::FindExecutableMatches(
        image.data(), image.size(), 0x140000000ull, pattern, 2);
    assert(!ambiguous.exact(1));
    assert(ambiguous.addresses.size() == 2);

    image[0] = 0;
    auto invalid = citron::sigscan::FindExecutableMatches(
        image.data(), image.size(), 0x140000000ull, pattern, 2);
    assert(!invalid.validImage);
    return 0;
}
