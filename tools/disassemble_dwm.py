"""Disassemble selected on-disk functions without loading or altering DWM."""
import bisect
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path('build/python').resolve()))
import capstone
import pefile

pe = pefile.PE(r'C:\Windows\System32\dwmcore.dll')
base = pe.OPTIONAL_HEADER.ImageBase
symbols = []
for line in pathlib.Path('build/dwm-symbols.txt').read_text().splitlines():
    addr, size, name = line.split(' ', 2)
    symbols.append((int(addr, 16), name))
symbols.sort()
addresses = [s[0] for s in symbols]
unwind = {p.struct.BeginAddress: p.struct.EndAddress for p in pe.DIRECTORY_ENTRY_EXCEPTION}
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
decoder.detail = True
patterns = [re.compile(p) for p in sys.argv[1:]]
out = []
seen = set()
for start, name in symbols:
    if start in seen or not any(p.search(name) for p in patterns):
        continue
    seen.add(start)
    end = unwind.get(start, start + 256)
    out.append(f'\n{name} @ {start:08x}..{end:08x}')
    for i in decoder.disasm(pe.get_data(start, end - start), base + start):
        comment = ''
        for operand in i.operands:
            target = None
            if operand.type == capstone.CS_OP_IMM:
                target = operand.imm - base
            elif operand.type == capstone.CS_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                target = i.address + i.size + operand.mem.disp - base
            if target is not None and target in addresses:
                comment += ' ; ' + symbols[bisect.bisect_left(addresses, target)][1]
        out.append(f'{i.address - base:08x}  {i.mnemonic:9s} {i.op_str}{comment}')
pathlib.Path('build/research/disassembly.txt').write_text('\n'.join(out), encoding='utf-8')
print('\n'.join(out))
