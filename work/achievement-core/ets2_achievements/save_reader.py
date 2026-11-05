"""Read-only SII save reader. Unknown formats fail closed; never modifies saves."""
import struct,zlib,re,json,math
from pathlib import Path

ARRAY={2:1,4:3,6:5,10:9,18:17,24:23,26:25,38:37,40:39,44:43,50:49,52:51,54:53,58:57,60:57}
SCALAR={5:'f',7:'2f',9:'3f',17:'3i',23:'4f',37:'i',39:'I',43:'H',47:'I',49:'q',51:'Q',53:'B',55:'I'}
TEXT_FIELDS=json.loads((Path(__file__).parent/'save_fields.json').read_text(encoding='utf-8'))

def text_value(raw):
    raw=raw.strip()
    if raw.startswith('"') and raw.endswith('"'):
        try:return json.loads(raw)
        except ValueError:return raw[1:-1]
    if raw in ('true','false'):return int(raw=='true')
    if re.fullmatch(r'&[a-fA-F0-9]{8}',raw):
        value=struct.unpack('>f',bytes.fromhex(raw[1:]))[0]
        return value if math.isfinite(value) else None
    try:return int(raw,16) if raw.lower().startswith('0x') else int(raw)
    except ValueError:
        try:
            value=float(raw);return value if math.isfinite(value) else None
        except ValueError:return raw

def text_sii(text):
    blocks=[]
    for match in re.finditer(r'(?m)^\s*([a-z_]+)\s*:\s*([^\s]+)\s*\{([^{}]*)\}',text):
        cls,uid,body=match.groups();attrs={};scalars={};arrays={}
        for line in body.splitlines():
            m=re.fullmatch(r'\s*(\w+)(\[(\d*)\])?\s*:\s*(.*?)\s*',line)
            if not m:continue
            k,bracket,index,v=m.groups()
            if bracket:arrays.setdefault(k,[]).append((index,v))
            elif k not in scalars:scalars[k]=v
            else:scalars[k]=None
        schema=TEXT_FIELDS.get(cls,{})
        for k,v in scalars.items():
            if v is None:continue
            if k not in arrays and schema.get(k)!='list':attrs[k]=text_value(v)
            elif k not in arrays and v=='0':attrs[k]=[]
        for k,values in arrays.items():
            declared=scalars.get(k)
            if declared is None or not declared.isdigit() or int(declared)!=len(values):continue
            indices=[i for i,v in values]
            if all(i=='' for i in indices):ordered=[v for i,v in values]
            elif all(i!='' for i in indices) and sorted(map(int,indices))==list(range(len(values))):ordered=[v for i,v in sorted(values,key=lambda p:int(p[0]))]
            else:continue
            attrs[k]=[v.strip().strip('"') for v in ordered] if k=='params' else [text_value(v) for v in ordered]
        blocks.append({'class':cls,'id':uid,'attrs':attrs})
    return blocks

def decode_save(raw):
    if raw.startswith(b'ScsC'):
        if len(raw)<72 or (len(raw)-56)%16:raise ValueError('Invalid encrypted SII length')
        from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
        key=bytes.fromhex('2a5fcb1791d22fb60245b3d8369ed0b2c27371563fbf1f3c9edf6b11825a5d0a')
        dec=Cipher(algorithms.AES(key),modes.CBC(raw[36:52])).decryptor()
        stream=zlib.decompressobj(); result=stream.decompress(dec.update(raw[56:])+dec.finalize(),128*1024*1024+1)
        if len(result)>128*1024*1024 or not stream.eof:raise ValueError('Invalid or oversized save payload')
        if len(result)!=struct.unpack_from('<I',raw,52)[0]:raise ValueError('Invalid save payload size')
        return result
    return raw

def bsii(data):
    pos=8;version=struct.unpack_from('<I',data,4)[0]
    if version not in (1,2,3):raise ValueError('Unsupported BSII version')
    def take(n):
        nonlocal pos
        if n<0 or pos+n>len(data):raise ValueError('Truncated BSII')
        b=data[pos:pos+n];pos+=n;return b
    def unpack(fmt):
        v=struct.unpack('<'+fmt,take(struct.calcsize('<'+fmt)));return v[0] if len(v)==1 else v
    def count():
        n=unpack('I')
        if n>len(data)-pos:raise ValueError('Invalid BSII count')
        return n
    def string():return take(count()).decode('utf-8',errors='replace')
    def encoded():
        value=unpack('Q')&0x7fffffffffffffff;chars='0123456789abcdefghijklmnopqrstuvwxyz_';text=''
        while value:
            value,c=divmod(value,38)
            if not 1<=c<=37:raise ValueError('Invalid token')
            text+=chars[c-1]
        return text
    def ident():
        n=unpack('B')
        return f'@{unpack("Q"):x}' if n==255 else '.'.join(encoded() for _ in range(n))
    def value(t):
        if t in ARRAY:return [value(ARRAY[t]) for _ in range(count())]
        if t==1:return string()
        if t==3:return encoded()
        if t in (57,59,61):return ident()
        if t==25:return unpack('7f' if version==1 else '8f') # coordinates not used
        if t in SCALAR:return unpack(SCALAR[t])
        raise ValueError(f'Unsupported BSII type {t}')
    structures={}; blocks=[]
    while pos<len(data):
        kind=unpack('I')
        if kind==0:
            valid=unpack('B')
            if valid==0:
                if pos!=len(data):raise ValueError('Unexpected BSII tail')
                return blocks
            if valid!=1:raise ValueError('Invalid structure')
            sid=unpack('I');name=string();fields=[]
            if not sid or sid in structures:raise ValueError('Invalid structure ID')
            while True:
                t=unpack('I')
                if not t:break
                key=string()
                if t==55:
                    for _ in range(count()):unpack('I');string()
                fields.append((key,t))
            structures[sid]=(name,fields)
        else:
            if kind not in structures:raise ValueError('Unknown structure ID')
            name,fields=structures[kind];unit_id=ident()
            attrs={key:value(t) for key,t in fields}
            blocks.append({'class':name,'id':unit_id,'attrs':attrs})
    raise ValueError('Missing BSII terminator')

def load_save(path):
    path=Path(path);a=path.stat()
    if a.st_size>128*1024*1024:raise ValueError('Save too large')
    raw=path.read_bytes();b=path.stat()
    if a.st_mtime_ns!=b.st_mtime_ns or a.st_size!=len(raw):raise ValueError('Save is changing')
    data=decode_save(raw)
    if data.startswith(b'BSII'):blocks=bsii(data)
    elif data.startswith(b'SiiNunit'):
        return text_sii(data.decode('utf-8',errors='replace')),b.st_mtime
    else:raise ValueError('Unsupported save encoding')
    return blocks,b.st_mtime
