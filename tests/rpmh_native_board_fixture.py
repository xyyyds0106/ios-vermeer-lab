"""Build a SYNTHETIC FDT from public fixed board rules, never a phone dump.

This exercises the real native gate/flow in CI. It is not live DT evidence and
must never be embedded in a hardware image or passed to a real provider.
"""
from pathlib import Path
import json,re,struct
def fixture():
    header=Path(__file__).resolve().parent.parent/'xnu/vermeer/vermeer_rpmh_board_contract.h'
    root={'props':{},'children':{}}
    count=0
    for line in header.read_text().splitlines():
        match=re.fullmatch(r'\s*\{("[^"]*"),("[^"]*"),(\d+),\{([^}]*)\}\},',line)
        if not match:continue
        path,key=json.loads(match[1]),json.loads(match[2]);size=int(match[3])
        values=bytes(int(word,0) for word in match[4].split(','))[:size]
        assert len(values)==size
        node=root
        for name in path.strip('/').split('/') if path!='/' else []:
            node=node['children'].setdefault(name,{'props':{},'children':{}})
        assert key not in node['props'];node['props'][key]=values;count+=1
    assert count>=61
    strings=bytearray();offsets={}
    def word(value):return struct.pack('>I',value)
    def pad(data):return data+bytes(-len(data)%4)
    def emit(name,node):
        data=word(1)+pad(name.encode()+b'\0')
        for key,value in node['props'].items():
            if key not in offsets:offsets[key]=len(strings);strings.extend(key.encode()+b'\0')
            data+=word(3)+word(len(value))+word(offsets[key])+pad(value)
        for name,child in node['children'].items():data+=emit(name,child)
        return data+word(2)
    body=emit('',root)+word(9);start=56;string_start=start+len(body);total=string_start+len(strings)
    return struct.pack('>10I',0xd00dfeed,total,start,string_start,40,17,16,0,len(strings),len(body))+bytes(16)+body+strings
if __name__=='__main__':
    import sys
    Path(sys.argv[1]).write_bytes(fixture())
