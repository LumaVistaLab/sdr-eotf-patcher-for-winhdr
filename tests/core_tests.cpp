#include "../src/core.h"
#include "../src/isolation.h"
#include "../src/isolation_profile.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <cstring>
#include <algorithm>
using namespace eotf;
using Microsoft::WRL::ComPtr;
static void Require(bool ok, const char* message) { if (!ok) throw Error(message); }
static void Rejected(const Bytes& b) { try { InspectShaders(b); } catch (const Error&) { return; } throw Error("Malformed/unknown PE was accepted"); }
static void CheckIsolationProfile(const Bytes& disk) {
    Require(MatchesIsolationProfile(), "Installed dwmcore.dll does not match the isolation profile digest");
    IMAGE_DOS_HEADER dos; memcpy(&dos, disk.data(), sizeof(dos));
    IMAGE_NT_HEADERS64 nt; memcpy(&nt, disk.data() + dos.e_lfanew, sizeof(nt));
    size_t table = dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    for (const auto* site : {&DisplayRenderSite, &LegacyRenderSite, &LinkedShaderSite, &CommonShaderSite}) {
        bool matched = false;
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section; memcpy(&section, disk.data() + table + i * sizeof(section), sizeof(section));
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || site->rva < section.VirtualAddress) continue;
            size_t offset = site->rva - section.VirtualAddress;
            if (offset > section.SizeOfRawData || sizeof(site->original) > section.SizeOfRawData - offset) continue;
            matched = !memcmp(disk.data() + section.PointerToRawData + offset, site->original, sizeof(site->original));
            break;
        }
        Require(matched, "Isolation hook entry is outside executable code or its bytes differ");
    }
    bool cacheMapped = false;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section; memcpy(&section, disk.data() + table + i * sizeof(section), sizeof(section));
        const DWORD flags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        if ((section.Characteristics & flags) != flags || CommonCacheRva < section.VirtualAddress) continue;
        size_t offset = CommonCacheRva - section.VirtualAddress;
        cacheMapped = offset <= section.Misc.VirtualSize && 0x14 <= section.Misc.VirtualSize - offset;
        if (cacheMapped) break;
    }
    Require(cacheMapped, "Common shader cache is outside writable image data");
}
int main() {
    try {
        Rejected({}); Rejected(Bytes(1024, 0));
        auto disk = ReadFileBytes(SystemDwmPath()); const auto before = disk;
        auto inventory = InspectShaders(disk);
        CheckIsolationProfile(disk);
        Require(disk == before, "Inspection mutated source bytes");
        Require(inventory.shaders > 0 && inventory.untouched > 0, "No meaningful shader coverage");
        size_t knownLeaves = 0;
        for (const auto& container : inventory.containers) {
            Require(container.offset <= disk.size() && container.original.size() <= disk.size() - container.offset, "Container outside image");
            Require(std::equal(container.original.begin(), container.original.end(), disk.begin() + container.offset), "Inspection changed library bytes");
            Require(ValidChecksum(container.original.data(), container.original.size()), "Library checksum invalid");
            size_t leaves = 0;
            for (size_t i = 0; i + 32 <= container.original.size(); ++i) {
                const auto p = container.original.data() + i;
                if (memcmp(p, "DXBC", 4) || !IsKnownSdrShader(p + 4)) continue;
                DWORD n; memcpy(&n, p + 24, 4);
                Require(n >= 32 && n <= container.original.size() - i, "Bad child size");
                Require(ValidChecksum(p, n), "Source shader checksum invalid");
                ComPtr<ID3DBlob> assembly;
                Require(SUCCEEDED(D3DDisassemble(p, n, 0, nullptr, &assembly)), "D3DDisassemble rejected source shader");
                ++leaves;
            }
            Require(leaves == (size_t)container.shaders, "Known shader coverage mismatch");
            knownLeaves += leaves;
            auto damaged = disk; damaged[container.offset + container.original.size() - 1] ^= 1; Rejected(damaged);
        }
        Require(knownLeaves == (size_t)inventory.shaders, "Total known shader coverage mismatch");
        // Independently count unrelated leaves, including native HDR shaders.
        int otherLeaves = 0;
        for (size_t i = 0; i + 32 <= disk.size(); ++i) {
            if (memcmp(disk.data() + i, "DXBC", 4)) continue;
            DWORD n; memcpy(&n, disk.data() + i + 24, 4);
            if (n < 32 || n > disk.size() - i || !ValidChecksum(disk.data() + i, n) || IsKnownSdrShader(disk.data() + i + 4)) continue;
            bool nested = false;
            for (size_t k = i + 32; k + 32 <= i + n; ++k) if (!memcmp(disk.data() + k, "DXBC", 4)) { nested = true; break; }
            if (!nested) ++otherLeaves;
        }
        Require(otherLeaves == inventory.untouched, "Unrelated shader coverage mismatch");
        IMAGE_DOS_HEADER dos; memcpy(&dos, disk.data(), sizeof(dos));
        Rejected(Bytes(disk.begin(), disk.begin() + dos.e_lfanew));
        auto invalid = disk;
        IMAGE_NT_HEADERS64 nt; memcpy(&nt, invalid.data() + dos.e_lfanew, sizeof(nt));
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        memcpy(invalid.data() + dos.e_lfanew, &nt, sizeof(nt)); Rejected(invalid);
        invalid = disk;
        size_t table = dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        IMAGE_SECTION_HEADER section; memcpy(&section, invalid.data() + table, sizeof(section));
        section.PointerToRawData = MAXDWORD;
        memcpy(invalid.data() + table, &section, sizeof(section)); Rejected(invalid);
        Require(disk == before, "Validation mutated source bytes");
        WriteText(L"build\\inspection.txt", Utf8(InspectReport()));
        std::cout << "PASS: read-only inventory of " << inventory.shaders << " SDR shaders and " << inventory.untouched << " unrelated shader leaves; source libraries unchanged.\n";
        std::cout << "PASS: checksums, source disassembly, corrupt/unknown input, truncated PE, wrong architecture and invalid section rejection.\n";
        std::cout << "PASS: exact isolation profile digest, all four executable hook entries and writable common-cache mapping.\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << "\n"; return 1; }
}
