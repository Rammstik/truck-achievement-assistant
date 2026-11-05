"""Read-only check of exact executable signatures and native field contracts."""
from pathlib import Path
import sys,re,hashlib,struct
import pefile,capstone
if len(sys.argv)!=2:raise SystemExit('Usage: ExecutableBindingsTests.py <eurotrucks2.exe>')
exe=Path(sys.argv[1]);raw=exe.read_bytes();pe=pefile.PE(data=raw,fast_load=True);root=Path(__file__).resolve().parents[1]/'src';rv={}
for file in ['MapProbeTarget.h','JobProbeTarget.h']:
 s=(root/file).read_text(encoding='utf-8')
 rv.update({k:int(v,16) for k,v in re.findall(r'\b(\w+)Rva=0x([0-9a-fA-F]+)',s)})
 for key,blob in re.findall(r'\b(\w+)Signature\[\]=\{([^}]+)\}',s):
  expected=bytes(int(x.strip(),16) for x in blob.split(','));assert pe.get_data(rv[key],len(expected))==expected,key+' signature differs'
 if file.startswith('Map'):
  sha=re.search(r'char Sha256\[\]="([0-9a-f]+)"',s)[1];assert hashlib.sha256(raw).hexdigest()==sha,'Unsupported executable hash'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);base=pe.OPTIONAL_HEADER.ImageBase
asm=lambda key,size:'\n'.join(i.mnemonic+' '+i.op_str for i in md.disasm(pe.get_data(rv[key],size),base+rv[key]))
draw=asm('Draw',0x1e00);pred=asm('Predicate',0xee);special=asm('SpecialPredicate',0x43);city=asm('City',0x90);update=asm('Update',0x400);rebuild=asm('Rebuild',0x52)
for pattern in ['[rsi + 0x3b8]','[rsi + 0x3c0]','[r12 + 8]','[r12 + 0xc]','[r12 + 0x10]','[r12 + 0x14]','mov qword ptr [rbp - 0x80], r12','add r13, 0x28','mov dword ptr [rdi + 0x50], eax']:assert pattern in draw,pattern
for pattern in ['[r8 + 0x58]','[rsi + 0x68]','lea rcx, [rbx + 0x18]',hex(base+rv['City']),hex(base+rv['Destination'])]:assert pattern in pred,pattern
assert '[rcx + 0x100]' in city and '[rcx + 0x398]' in update
assert '[rax + 0x130]' in special
assert '[rcx + 0x57a]' in rebuild and '[rcx + 0x578]' in rebuild
assert rv['PackCall']+5==rv['PackReturn']
assert rv['PackReturn']+struct.unpack('<i',pe.get_data(rv['PackCall']+1,4))[0]==rv['Pack']
print('PASS: exact supported EXE hash/signatures; current map array/frame/stride/color and ordinary/special job field contracts; direct pack call target.')
