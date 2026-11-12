"""Read-only lookup of the installed DWM module's matching Microsoft public PDB."""
import pathlib
import struct
import urllib.request
import uuid

binary = pathlib.Path(r'C:\Windows\System32\dwmcore.dll').read_bytes()
offset = binary.index(b'RSDS')
guid = uuid.UUID(bytes_le=binary[offset + 4:offset + 20]).hex.upper()
age = struct.unpack_from('<I', binary, offset + 20)[0]
name = binary[offset + 24:binary.index(b'\0', offset + 24)].decode().split('\\')[-1]
assert name == 'dwmcore.pdb', name
target = pathlib.Path('build/symbols') / name
target.parent.mkdir(parents=True, exist_ok=True)
url = f'https://msdl.microsoft.com/download/symbols/{name}/{guid}{age:X}/{name}'
print(url, flush=True)
if not target.exists():
    urllib.request.urlretrieve(url, target)
print(target, target.stat().st_size)
