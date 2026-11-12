#include "core.h"
#include <cstring>
extern "C" {
#include "third_party/DXBCChecksum.h"
}
namespace eotf {
bool ValidChecksum(const BYTE* p, size_t size) {
    if (size < 32 || size > MAXDWORD) return false;
    DWORD hash[4]; CalculateDXBCChecksum(const_cast<BYTE*>(p), (DWORD)size, hash);
    return memcmp(hash, p + 4, 16) == 0;
}
void FixChecksum(BYTE* p, size_t size) {
    if (size < 32 || size > MAXDWORD) throw Error("Invalid DXBC size");
    DWORD hash[4]; CalculateDXBCChecksum(p, (DWORD)size, hash); memcpy(p + 4, hash, 16);
}
}
