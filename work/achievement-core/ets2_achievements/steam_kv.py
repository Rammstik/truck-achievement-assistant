import struct

def binary_kv(data):
    pos=0
    def take(n):
        nonlocal pos
        if n<0 or pos+n>len(data):raise ValueError('Truncated KV file')
        b=data[pos:pos+n];pos+=n;return b
    def string():
        nonlocal pos
        end=data.find(b'\0',pos)
        if end<0:raise ValueError('Unterminated KV string')
        value=data[pos:end].decode('utf-8');pos=end+1;return value
    def obj(depth=0):
        if depth>32:raise ValueError('KV too deep')
        result={}
        while pos<len(data):
            kind=take(1)[0]
            if kind==8:return result
            key=string()
            if key in result:raise ValueError('Duplicate KV key')
            if kind==0:value=obj(depth+1)
            elif kind==1:value=string()
            elif kind==2:value=struct.unpack('<i',take(4))[0]
            elif kind==3:value=struct.unpack('<f',take(4))[0]
            elif kind==7:value=struct.unpack('<Q',take(8))[0]
            elif kind==10:value=struct.unpack('<q',take(8))[0]
            else:raise ValueError(f'Unsupported KV type {kind}')
            result[key]=value
        raise ValueError('Missing KV end marker')
    result=obj()
    if data[pos:].strip(b'\x08\x00'):raise ValueError('Unexpected KV tail')
    return result

