"""Exact-build offline candidate; original fallbacks and Windows unwind entries retained."""
import argparse,hashlib,importlib.util,json,struct
from pathlib import Path
import pefile
if not __debug__:
    raise RuntimeError('Run without Python optimization; candidate checks must remain enabled')
HERE=Path(__file__).resolve().parent
ORIGINAL='81be5bc2916ba979e0ecc68b5fd039a48821765c6e9a7bcfd8d27ad877cf1692'
def align(n,a):return (n+a-1)//a*a
def relative(opcode,source,dest):return bytes([opcode])+struct.pack('<i',dest-source-5)
def make(source,destination):
    original=source.read_bytes();assert hashlib.sha256(original).hexdigest()==ORIGINAL
    assert source.resolve()!=destination.resolve() and not destination.exists()
    data=bytearray(original);pe=pefile.PE(data=original,fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
    patches=[]
    def patch(rva,replacement,expected=None,name=''):
        offset=pe.get_offset_from_rva(rva);before=original[offset:offset+len(replacement)]
        if expected is not None:assert before==expected,name
        data[offset:offset+len(replacement)]=replacement
        patches.append(dict(name=name,rva=hex(rva),offset=hex(offset),before=before.hex(),after=replacement.hex()))
    def block(start,end,code,name):
        assert len(code)<=end-start,(name,len(code),end-start)
        patch(start,code+b'\x90'*(end-start-len(code)),name=name)
    spec=importlib.util.spec_from_file_location('unpack_patch',HERE.parent/'unpack_patch.py')
    unpack=importlib.util.module_from_spec(spec);spec.loader.exec_module(unpack)
    patch(unpack.RVA,unpack.REPLACEMENT,unpack.EXPECTED,'existing unpack subrange')
    dll=pefile.PE(str(HERE/'../../build/kernels/StudioNativeKernels.dll'))
    assert not getattr(dll,'DIRECTORY_ENTRY_IMPORT',[]) and not getattr(dll,'DIRECTORY_ENTRY_BASERELOC',[])
    first=min(s.VirtualAddress for s in dll.sections);last=max(s.VirtualAddress+s.Misc_VirtualSize for s in dll.sections)
    payload=bytearray(last-first)
    for section in dll.sections:
        content=section.get_data()[:section.Misc_VirtualSize];pos=section.VirtualAddress-first;payload[pos:pos+len(content)]=content
        # MSVC's zero-initialized ISA-dispatch state is read-only in these kernels; future writable globals need a new design.
        if section.Name.rstrip(b'\0')==b'.data':assert section.Misc_VirtualSize<=8 and not any(content)
    new_rva=align(pe.OPTIONAL_HEADER.SizeOfImage,pe.OPTIONAL_HEADER.SectionAlignment);delta=new_rva-first
    exports={e.name.decode():e.address+delta for e in dll.DIRECTORY_ENTRY_EXPORT.symbols}
    # Rebase MSVC chained RUNTIME_FUNCTION triples inside .xdata as well as the main table.
    seen=set()
    def unwind(rva):
        if rva in seen:return
        seen.add(rva);pos=rva-first;flags=payload[pos]>>3;count=payload[pos+2];assert flags in (0,4),flags
        if flags==4:
            chain=pos+4+align(count*2,4);begin,end,info=struct.unpack_from('<III',payload,chain)
            unwind(info);struct.pack_into('<III',payload,chain,begin+delta,end+delta,info+delta)
    added=[]
    for entry in dll.DIRECTORY_ENTRY_EXCEPTION:
        f=entry.struct;unwind(f.UnwindData);added.append((f.BeginAddress+delta,f.EndAddress+delta,f.UnwindData+delta))
    def append(blob):
        pos=align(len(payload),4);payload.extend(bytes(pos-len(payload)));payload.extend(blob);return new_rva+pos
    def leaf_jump_marker(name,target):
        begin=exports[name]-new_rva;end=payload.find(bytes.fromhex('e978563412'),begin,begin+96);assert end>=begin,name
        payload[end:end+5]=relative(0xe9,new_rva+end,target)
    leaf_jump_marker('KernelSort',0x315b5a0)
    leaf_jump_marker('ReadFallback',0x79d92e5);leaf_jump_marker('WriteFallback',0x79d93f5)
    # Only the explicit non-nil filled tcreate branch is redirected. Empty and
    # over-limit sizes retain the original call/loop and exact overflow error.
    pos=new_rva+align(len(payload),4);stub=bytearray()
    def create_emit(blob):stub.extend(bytes.fromhex(blob))
    def create_call(target):stub.extend(relative(0xe8,pos+len(stub),target))
    def create_jump(target):
        # A direct tail JMP is recognized as an epilogue by Windows, despite
        # this fragment retaining the original tcreate frame. Use an always-
        # taken conditional transfer so chained metadata restores that frame.
        stub.extend(bytes.fromhex('39c0')) # cmp eax,eax (no live flags at either continuation)
        at=pos+len(stub);stub.extend(bytes.fromhex('0f84')+struct.pack('<i',target-at-6))
    create_emit('85ff');zero_branch=len(stub);create_emit('0f8e00000000')
    create_emit('81ff00000004');large_branch=len(stub);create_emit('0f8700000000')
    create_emit('33d24533c0488bcb');create_call(0x3144ea0)
    create_emit('488b4370488b50f0488bcb448bc7')
    at=pos+len(stub);create_emit('4c8d0d');stub.extend(struct.pack('<i',0x315cc10-at-7))
    create_call(exports['KernelCreate']);create_jump(0x316aa4c)
    fallback=pos+len(stub)
    create_emit('8bd74533c0488bcb');create_call(0x3144ea0);create_jump(0x316aa0c)
    for branch in (zero_branch,large_branch):struct.pack_into('<i',stub,branch+2,fallback-(pos+branch+6))
    assert append(stub)==pos
    parent=next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=0x316aa07<e.struct.EndAddress)
    info=append(bytes([0x21,0,0,0])+struct.pack('<III',parent.BeginAddress,parent.EndAddress,parent.UnwindData))
    added.append((pos,pos+len(stub),info))
    patch(0x316aa07,relative(0xe9,0x316aa07,pos),bytes.fromhex('e894a4fdff'),'filled table.create single-pass allocation')
    # Array bounds, readonly check, sparse-table fallback (af63 onward), and GC barrier stay.
    # The old dense forward/backward direction branches are replaced by KernelMove.
    start=0x316ae9a;end=0x316af44
    stub=bytes.fromhex('488b4118498bd648ffca48c1e2044803d0488bcf48ffc948c1e10449034b18458bc54d8bf3')
    stub+=relative(0xe8,start+len(stub),exports['KernelMove'])+bytes.fromhex('4d8bde')
    stub+=relative(0xe9,start+len(stub),end);block(start,end,stub,'dense TValue moves')
    start=0x79d5359;stub=bytes.fromhex('488bcf498d542fff448bc6')
    stub+=relative(0xe8,start+len(stub),exports['KernelByte']);block(start,0x79d5375,stub,'byte range bulk writes')
    # Zero-length branches and buffer finalization are left at their original addresses.
    for start,iat,name in [(0x79d5051,0x89f78f0,'KernelLower'),(0x79d50f1,0x89f7880,'KernelUpper')]:
        stub=bytes.fromhex('4c8bc2488bcf488bd6')
        stub+=bytes.fromhex('4c8b0d')+struct.pack('<i',iat-(start+len(stub)+7))
        stub+=relative(0xe8,start+len(stub),exports[name])+bytes.fromhex('488b942458020000')
        block(start,start+32,stub,name+' locale-checked SWAR')
    start=0x316a954
    patch(start,bytes.fromhex('488d35')+struct.pack('<i',exports['KernelSort']-start-7),bytes.fromhex('488d35450cffff'),'default sort numeric predicate')
    for start,kernel,fallback in [(0x79d92e0,'KernelReadBits','ReadFallback'),(0x79d93f0,'KernelWriteBits','WriteFallback')]:
        pos=new_rva+align(len(payload),4)
        stub=bytes.fromhex('488d15')+struct.pack('<i',exports[fallback]-pos-7)
        stub+=relative(0xe9,pos+len(stub),exports[kernel]);assert append(stub)==pos
        patch(start,relative(0xe9,start,pos),bytes.fromhex('48895c2408'),kernel+' guarded exact fallback')
    # Long plain search only; short needles and patterns retain the original path.
    pos=new_rva+align(len(payload),4);stub=bytearray()
    def emit(b):stub.extend(bytes.fromhex(b))
    def jump(target,cond=None):
        at=pos+len(stub)
        stub.extend(relative(0xe9,at,target) if cond is None else b'\x0f'+bytes([cond])+struct.pack('<i',target-at-6))
    emit('4883fd1f');jump(0x79d8540,0x82);emit('4881fdffff0000');jump(0x79d8540,0x87)
    emit('488bcb498bd64c8d042e4c8d4d01')
    stub.extend(relative(0xe8,pos+len(stub),exports['KernelSearch']))
    emit('4885c0');jump(0x79d8455,0x84);emit('4c8bf8');jump(0x79d8583)
    assert append(stub)==pos
    parent=next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=0x79d853a<e.struct.EndAddress)
    info=append(bytes([0x21,0,0,0])+struct.pack('<III',parent.BeginAddress,parent.EndAddress,parent.UnwindData))
    added.append((pos,pos+len(stub),info))
    block(0x79d853a,0x79d8540,relative(0xe9,0x79d853a,pos),'long plain Two-Way search')
    entries=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]+added
    entries.sort();assert all(a[1]<=b[0] for a,b in zip(entries,entries[1:]))
    pdata=b''.join(struct.pack('<III',*entry) for entry in entries);pdata_rva=append(pdata)
    directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']]
    struct.pack_into('<II',data,directory.get_file_offset(),pdata_rva,len(pdata))
    raw=align(len(data),pe.OPTIONAL_HEADER.FileAlignment);raw_size=align(len(payload),pe.OPTIONAL_HEADER.FileAlignment)
    header=pe.sections[-1].get_file_offset()+40
    assert header+40<=pe.OPTIONAL_HEADER.SizeOfHeaders and original[header:header+40]==bytes(40)
    data[header:header+40]=struct.pack('<8sIIIIIIHHI',b'.luaux\0\0',len(payload),new_rva,raw_size,raw,0,0,0,0,0x60000020)
    struct.pack_into('<H',data,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),len(pe.sections)+1)
    struct.pack_into('<I',data,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),align(new_rva+len(payload),pe.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into('<I',data,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfCode'),pe.OPTIONAL_HEADER.SizeOfCode+raw_size)
    struct.pack_into('<I',data,pe.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'),0)
    data.extend(bytes(raw-len(data)));data.extend(payload+bytes(raw_size-len(payload)))
    with destination.open('xb') as out:out.write(data)
    result=dict(version='0.744.0.7441077',originalSha256=ORIGINAL,candidateSha256=hashlib.sha256(data).hexdigest(),originalSize=len(original),candidateSize=len(data),patches=patches,
        section=dict(rva=hex(new_rva),offset=hex(raw),codeBytes=len(payload),runtimeFunctions=len(added)),exports={k:hex(v) for k,v in exports.items()},
        scope='offline candidate: filled create, unpack, dense move, byte, case, long plain search, default numeric predicate, packed bits; not installed')
    destination.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ['patches','exports']},indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();make(args.source,args.destination)
