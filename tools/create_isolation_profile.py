import hashlib
import pathlib
import struct

image = pathlib.Path(r'C:\Windows\System32\dwmcore.dll').read_bytes()
expected = '825726f26bd6b5a02b49f6145963b54d537d3d1239055dda28d1fb0b3e864ef3'
assert hashlib.sha256(image).hexdigest() == expected, 'Profile requires a fresh reverse-engineering pass for this binary'
nt = struct.unpack_from('<I', image, 60)[0]
count = struct.unpack_from('<H', image, nt + 6)[0]
optional_size = struct.unpack_from('<H', image, nt + 20)[0]
def bytes_at(rva, size):
    for i in range(count):
        p = nt + 24 + optional_size + i * 40
        vs, va, raw_size, raw = struct.unpack_from('<IIII', image, p + 8)
        if va <= rva < va + raw_size:
            return image[raw + rva - va:raw + rva - va + size]
    raise ValueError(hex(rva))
out = ['#pragma once', '#include <windows.h>', 'namespace eotf {', 'struct HookSite { DWORD rva; BYTE original[16]; };']
for name, rva in [('DisplayRenderSite',0xd8910), ('LegacyRenderSite',0xd8db0), ('LinkedShaderSite',0x141d10), ('CommonShaderSite',0xf1340)]:
    out.append('inline constexpr HookSite %s = {0x%x, {%s}};' % (name,rva,','.join('0x%02x' % b for b in bytes_at(rva,16))))
out.append('inline constexpr BYTE ProfileDigest[32] = {%s};' % ','.join('0x%02x' % b for b in bytes.fromhex(expected)))
out.append('inline constexpr size_t DisplayRectOffset = 0x76c8, LegacyRectOffset = 0x76c0;')
out.append('inline constexpr DWORD CommonCacheRva = 0x3f92c0;')
out.append('}')
pathlib.Path('src/isolation_profile.h').write_text('\n'.join(out)+'\n')
print('Generated exact-image isolation profile; no DWM memory accessed.')
