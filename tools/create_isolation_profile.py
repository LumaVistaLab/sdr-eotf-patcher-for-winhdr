import hashlib
import pathlib
import struct

image = pathlib.Path(r'C:\Windows\System32\dwmcore.dll').read_bytes()
# Windows x64 dwmcore.dll 10.0.26100.9949; Microsoft public PDB
# DCF0C9C9-BAEF-E1CC-9AC4-140D683DE8CA, age 1.
# Rechecked RenderAndPresent (both classes), GetShadersNoRef and
# GetCommonRenderingShaderInternalNoRef against the matching symbols/code.
# RenderAndPresent at 0xb54db / 0xb62f2 passes self+0x76c0 / self+0x76b8
# to CMonitorTransform::GetTreeBounds (0x93788), which returns transform+8.
# Thus the hook-relative bounds remain +0x76c8 / +0x76c0, relative to the
# incoming RenderAndPresent this pointer (also confirmed on three live outputs).
# The optional magnifier branch can select the following rectangle; unknown
# bounds continue to fail the agent's exact-output match.
# CLinkedShader's constructor (0x1fb344) retains the self pointer at +0x28,
# blob at +0x10 and resource vector at +0x30/+0x38;
# CD3DPixelShader::Initialize (0x101130) retains
# pixel shader +0x78. The common-cache lookup (0x1449c1..0x144a12) confirms
# the global at 0x44c2c0, entries +8 and count +0x10.
expected = 'a3c7441a4b53a302f836a3b9b04efa343e320e2d0cffce730601f89689fb7b48'
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
for name, rva in [('DisplayRenderSite',0xb5470), ('LegacyRenderSite',0xb6270), ('LinkedShaderSite',0x1464c0), ('CommonShaderSite',0x144860)]:
    out.append('inline constexpr HookSite %s = {0x%x, {%s}};' % (name,rva,','.join('0x%02x' % b for b in bytes_at(rva,16))))
out.append('inline constexpr BYTE ProfileDigest[32] = {%s};' % ','.join('0x%02x' % b for b in bytes.fromhex(expected)))
out.append('inline constexpr size_t DisplayRectOffset = 0x76c8, LegacyRectOffset = 0x76c0;')
out.append('inline constexpr DWORD CommonCacheRva = 0x44c2c0;')
out.append('}')
pathlib.Path('src/isolation_profile.h').write_text('\n'.join(out)+'\n')
print('Generated exact-image isolation profile; no DWM memory accessed.')
