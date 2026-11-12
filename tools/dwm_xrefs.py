import bisect
import pathlib
import struct
import sys

data = pathlib.Path(r'C:\Windows\System32\dwmcore.dll').read_bytes()
nt = struct.unpack_from('<I', data, 60)[0]
number = struct.unpack_from('<H', data, nt + 6)[0]
optional_size = struct.unpack_from('<H', data, nt + 20)[0]
sections = []
for i in range(number):
    p = nt + 24 + optional_size + i * 40
    name, vs, va, raw_size, raw = struct.unpack_from('<8sIIII', data, p)
    sections.append((va, raw_size, raw, name.rstrip(b'\0')))
def read(rva, n):
    for va, size, raw, _ in sections:
        if va <= rva < va + size:
            return data[raw + rva - va:raw + rva - va + n]
    raise ValueError(hex(rva))
pdata, psize = struct.unpack_from('<II', data, nt + 24 + 112 + 3 * 8)
funcs = [struct.unpack_from('<III', read(pdata + i, 12))[:2] for i in range(0, psize, 12)]
starts = [f[0] for f in funcs]
symbols = {}
for line in pathlib.Path('build/dwm-symbols.txt').read_text().splitlines():
    address, size, name = line.split(' ', 2)
    symbols.setdefault(int(address, 16), name)
for target in map(lambda s: int(s, 16), sys.argv[1:]):
    print(f'\nTARGET {target:08x} {symbols.get(target, "")}')
    for va, size, raw, name in sections:
        if name != b'.text': continue
        code = data[raw:raw + size]
        for i in range(len(code) - 5):
            if code[i] != 0xe8: continue
            if va + i + 5 + struct.unpack_from('<i', code, i + 1)[0] != target: continue
            addr = va + i
            k = bisect.bisect_right(starts, addr) - 1
            begin, end = funcs[k]
            print(f'{addr:08x} in {begin:08x}..{end:08x} {symbols.get(begin, "[unwind fragment]")}')
