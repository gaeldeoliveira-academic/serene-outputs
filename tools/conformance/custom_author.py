"""Independent custom-container G0C fixture author; not a production writer.
Only Python struct/hashlib/json, NumPy and public zfpy define the wire bytes.
Historical HDF5 artifacts and production implementation are never imported.
"""
from pathlib import Path
import argparse, copy, hashlib, json, struct
import numpy as np
import zfpy

# Repository-relative fixture author root.
ROOT = Path(__file__).resolve().parents[2]
# Five mandatory custom-container 1.0 feature tokens.
FEATURES = ['serene-container-le-1','json-descriptors-1','utf8-pools-1','frames-77-le-1','zfp-5-le64-serial-1']
# Section kind, NumPy dtype and wire element width per array tag.
TYPES = {'u8':(2,'u1',1),'u32':(3,'<u4',4),'u64':(4,'<u8',8),'f64':(5,'<f8',8),'frames77':(6,None,77),'i8':(7,'i1',1),'i32':(8,'<i4',4)}
# Canonical 77-byte struct record.
FRAME = struct.Struct('<QQQQQBI32s')
# Canonical 64-byte directory-row layout.
DIRECTORY = struct.Struct('<QIIQQ32s')
# First 96 bootstrap bytes before their separate digest.
HEADER96 = struct.Struct('<8sHHIIIQQQQQ32s')
assert FRAME.size == 77 and DIRECTORY.size == 64 and HEADER96.size == 96

def sha(b):
    """Return the 32-byte SHA-256 digest of borrowed bytes-like b."""
    # Hash exact fixture bytes without text normalization.
    return hashlib.sha256(b).digest()
def js(x):
    """Return compact UTF-8 JSON bytes for x, preserving Unicode and rejecting NaN.

    x is serialized without mutating it; separators are the fixture spelling."""
    # Serialize the fixture object with deterministic compact separators.
    return json.dumps(x,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode('utf-8')

class Container:
    """Owned tiny custom-container assembly used by independent fixture authors.

    Stores section bytes, mutable root/run descriptors, late byte overrides and
    expected/sidecar evidence. Section IDs are list indexes; root is section zero.
    Mutators deliberately permit malformed cases. encode() seals current metadata
    and materializes the whole file; this is not a bounded production writer."""
    # Keep fixture payloads and descriptors in owned Python containers until encoding.
    def __init__(self):
        """Initialize an empty fixture assembly with a reserved root section.

        self owns all mutable lists/dictionaries; no input or external borrow exists."""
        # Reserve root section zero and initialize catalogs, overrides and oracle stores.
        # Owned ordered (kind, immutable bytes) sections; root occupies index zero.
        # Owned mutable root metadata.
        # Owned mutable run metadata in catalog order.
        # Metadata section IDs corresponding positionally to runs.
        # Explicit raw section overrides applied after normal metadata sealing.
        # Owned per-run exact logical/value oracle descriptions.
        # Filename-to-immutable-bytes map of independent index/payload/axis evidence.
        self.sections=[(1,b'')]; self.root={}; self.runs=[]; self.run_ids=[]; self.overrides={}; self.expected=[]; self.sidecars={}
    def add(self,kind,data):
        """Append kind and copied data to owned sections; return the new dense section ID.

        kind is a wire section code; data is converted to immutable bytes."""
        # Choose the next section index, store copied bytes, and return that identity.
        # New section index or owned canonical compound frame-index copy.
        idx=len(self.sections); self.sections.append((kind,bytes(data))); return idx
    def arr(self,dtype,data,shape=None):
        """Append dtype/data and return its section/dtype/shape descriptor.

        dtype indexes TYPES; data is bytes or array-like. shape=None derives its
        dimensions, otherwise copies the explicit shape (including negative fixtures)."""
        # Resolve wire kind/width and convert raw or NumPy values to exact bytes.
        # Derive or copy logical shape, append the section, and return an owned descriptor.
        # Wire kind selecting the current run or section interpretation.
        # NumPy dtype spelling for typed source values.
        # Wire byte width per scalar/frame record.
        kind,nt,width=TYPES[dtype]
        if isinstance(data,(bytes,bytearray)):
            # Owned exact bytes before publication or a deliberate mutation.
            # Owned derived or explicitly supplied logical dimensions.
            raw=bytes(data); shape=[len(raw)//width] if shape is None else list(shape)
        else:
            # Converted little-endian binary64 fixture matrix.
            # Owned exact bytes before publication or a deliberate mutation.
            # Owned derived or explicitly supplied logical dimensions.
            array=np.asarray(data,dtype=nt); raw=array.tobytes(); shape=list(array.shape) if shape is None else list(shape)
        return {'section':self.add(kind,raw),'dtype':dtype,'shape':shape}
    def data(self,descriptor):
        """Return stored immutable payload bytes addressed by descriptor.

        descriptor is borrowed; the returned bytes are shared immutable fixture data."""
        # Resolve the descriptor section index without copying its immutable payload.
        return self.sections[descriptor['section']][1]
    def replace(self,descriptor,data,dtype=None,shape=None):
        """Replace the section named by in-out descriptor using data.

        dtype=None preserves its existing type; explicit shape replaces dimensions,
        otherwise shape is unchanged. Returns None and deliberately allows negatives."""
        # Resolve the selected dtype and encode replacement bytes.
        # Replace the existing section in place and update only requested descriptor fields.
        # Replacement dtype tag or unchanged descriptor tag.
        # Wire kind selecting the current run or section interpretation.
        # NumPy dtype spelling for typed source values.
        # Wire byte width per scalar/frame record.
        tag=dtype or descriptor['dtype']; kind,nt,width=TYPES[tag]
        # Owned exact bytes before publication or a deliberate mutation.
        if isinstance(data,(bytes,bytearray)): raw=bytes(data)
        # Owned exact bytes before publication or a deliberate mutation.
        else: raw=np.asarray(data,dtype=nt).tobytes()
        self.sections[descriptor['section']]=(kind,raw)
        descriptor['dtype']=tag
        if shape is not None: descriptor['shape']=list(shape)
    def pool(self,values,presence=None):
        """Return a new UTF-8 pool descriptor for values.

        values are strings; optional presence supplies a separate byte column.
        Copies all encoded bytes/offsets and does not normalize spellings."""
        # Encode each string, accumulate terminal offsets, and append pool sections.
        # Attach a presence section only when the caller supplied it.
        # Individually encoded UTF-8 strings.
        # Current decoded string/value item.
        # Initial/terminal UTF-8 or JSONL byte boundaries.
        parts=[s.encode('utf-8') for s in values]; offsets=[0]
        # Current encoded string used to accumulate terminal offsets.
        for part in parts: offsets.append(offsets[-1]+len(part))
        # Created pool/profile descriptor or selected artifact path.
        p={'count':len(parts),'bytes':self.arr('u8',b''.join(parts)),'offsets':self.arr('u64',offsets)}
        if presence is not None: p['present']=self.arr('u8',presence)
        return p
    def pool_values(self,p):
        """Decode pool descriptor p into a new list of strings.

        Borrows stored byte/offset sections and uses strict UTF-8 decoding."""
        # Read exact byte ranges using the stored offset table and decode each slice.
        # Owned encoded payload bytes.
        # Initial/terminal UTF-8 or JSONL byte boundaries.
        data=self.data(p['bytes']); offsets=np.frombuffer(self.data(p['offsets']),dtype='<u8')
        # Beginning byte boundary of the current pool string.
        # Ending byte boundary of the current pool string.
        return [data[int(a):int(b)].decode('utf-8') for a,b in zip(offsets[:-1],offsets[1:])]
    def replace_pool(self,p,values):
        """Replace in-out pool p with strings values; return None.

        Reuses existing byte/offset section identities and updates count/shapes;
        any existing presence column is left to the caller."""
        # Encode strings and rebuild offsets, then replace existing pool sections.
        # Individually encoded UTF-8 strings.
        # Current decoded string/value item.
        # Initial/terminal UTF-8 or JSONL byte boundaries.
        parts=[s.encode('utf-8') for s in values]; offsets=[0]
        # Current encoded string used to accumulate terminal offsets.
        for part in parts: offsets.append(offsets[-1]+len(part))
        p['count']=len(parts); self.replace(p['bytes'],b''.join(parts),shape=[offsets[-1]])
        self.replace(p['offsets'],offsets,shape=[len(offsets)])
    def document(self,value=None,state=0):
        """Return a document descriptor for value and retention state.

        Included/partial states retain exact bytes or encoded JSON; inactive states
        contain only state. The caller chooses whether partial bytes are complete."""
        # Build state-only metadata and attach application/json bytes when retained.
        # Owned document-state descriptor.
        d={'state':state}
        if state in (0,3): d.update(media_type='application/json',bytes=self.arr('u8',value if isinstance(value,bytes) else js(value)))
        return d
    def seal(self):
        """Refresh root/run metadata section bytes and apply explicit overrides.

        Mutates self.sections only; overrides deliberately replace serialized bytes
        last so malformed JSON fixtures survive ordinary descriptor serialization."""
        # Serialize current root/run dictionaries, then apply author-requested raw overrides.
        self.sections[0]=(1,js(self.root))
        # Dense section ID in the fixture graph.
        # Owned metadata object for the current run.
        for sid,run in zip(self.run_ids,self.runs): self.sections[sid]=(1,js(run))
        # Dense section ID in the fixture graph.
        # Owned encoded payload bytes.
        for sid,data in self.overrides.items(): self.sections[sid]=(self.sections[sid][0],data)
    def encode(self):
        """Return owned complete container bytes from the current fixture assembly.

        Calls seal(), writes contiguous sections and canonical directory rows, and
        hashes exact directory/header bytes. Entire tiny files are materialized."""
        # Seal metadata and append section bodies with independent directory digests.
        # Encode the fixed bootstrap and its digest, then concatenate the complete file.
        # Owned contiguous section-body accumulator.
        # Owned packed directory rows with independent section hashes.
        # Next section offset or explicit header mutation byte offset.
        self.seal(); body=bytearray(); directory=bytearray(); offset=128
        # Dense section ID in the fixture graph.
        # Wire kind selecting the current run or section interpretation.
        # Owned encoded payload bytes.
        for sid,(kind,data) in enumerate(self.sections):
            directory.extend(DIRECTORY.pack(sid,kind,0,offset,len(data),sha(data))); body.extend(data); offset+=len(data)
        # Canonical first 96 bootstrap bytes before appending their digest.
        h=HEADER96.pack(b'SERENE\0\0',1,0,0x01020304,128,64,offset+len(directory),offset,len(self.sections),0,1,sha(directory))
        return h+sha(h)+bytes(body)+bytes(directory)

def run_info(c,r,C,K):
    """Return fixture run metadata for Container c and source run description r.

    C is scalar-channel count, K committed axis count; r determines run kind/status
    and optional overrides. Adds configuration documents to c and owns the result."""
    # Derive canonical absent/present envelope values from availability and run kind.
    # Attach independent solver/sampling/execution documents and return the descriptor.
    # Wire kind selecting the current run or section interpretation.
    # Whether the fixture catalog is unavailable after preparation.
    # Actual retained time/load-axis values from the source description.
    kind=r['kind']; un=r.get('unavailable',False); axis=r['axis']
    # Owned canonical run envelope.
    info={'run_id':r['id'],'assembly_ref':'' if un else 'fixture_assembly','kind':kind,
      'run_outcome':r.get('outcome',0),'numerical_status':r.get('numerical',1),
      'observation_status':r.get('observation',2 if C==0 else 0),'logging_status':r.get('logging',0),
      'failure_stage':r.get('failure_stage',''),'failure_code':r.get('failure_code',''),'termination_reason':r.get('termination_reason',''),
      'catalog_state':2 if un else 0,'committed_count':K,'accepted_count':r.get('accepted_count',10 if kind==0 else 7),
      'last_accepted_present':0 if un else 1,'last_recorded_present':int(K>0),
      'last_accepted_value':0.0 if un else r.get('last_accepted',.1 if kind==0 else 1.0),
      'last_recorded_value':float(axis[-1]) if K else 0.0,'requested_bounds_present':0 if un else 1,
      'requested_begin':0.0,'requested_end':0.0 if un else (.1 if kind==0 else 1.0),
      'elapsed_seconds_present':0 if un else 1,'elapsed_seconds':0.0 if un else .125,'elapsed_seconds_scope':'' if un else 'solve_only',
      'identity_axis_present':0 if un else 1,'identity_axis_convention':'' if un else ('integration_step_zero_is_initial' if kind==0 else 'accepted_level_one_is_first'),
      'axis_name':'time' if kind==0 else 'proportional_load','axis_unit':'s' if kind==0 else '1'}
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'solver_configuration':{'fixture':True},'sampling_configuration':{'fixture':True,'capture':'actual_axis'},'execution':{'timing_scope':'solve_only'}}.items():
        info[name]=c.document(value,state=2 if un else 0)
    return info

def catalogs(c,C,state,retain_requests=False):
    """Return channel/request catalogs in c for C channels and effective state.

    retain_requests preserves one request for an empty selection; otherwise the
    tiny deterministic catalog has at most two requests. Adds exact pools/digests."""
    # Map channels to one/two deterministic request groups and write identity/unit pools.
    # Attach effective JSON plus its independent digest only when the state is included.
    # Request count, distinct from selected values or committed samples.
    # Deterministic request group IDs, including Unicode text.
    # Channel-to-request row mapping.
    # Current scalar/request mapping index.
    R=min(2,C) if C else int(retain_requests); groups=['pale_α','shaft'][:R]; which=[0 if k<(C+1)//2 else 1 for k in range(C)] if R==2 else [0]*C
    # Channel catalog descriptor/group under construction.
    ch={'channel_index':c.arr('u64',list(range(C))),'request_index':c.arr('u64',which)}
    # Current authored property/case name, unchanged by normalization.
    # Current identity/unit strings for a catalog column.
    # Current scalar/request mapping index.
    for name,items in {'scalar_id':[groups[which[k]]+'.motion.c'+str(k) for k in range(C)],'group_id':[groups[k] for k in which],'request_id':['motion']*C,'component_path':['c'+str(k) for k in range(C)],'unit':[['m','m','rad','N','N*m'][k%5] for k in range(C)]}.items():ch[name]=c.pool(items)
    # Request catalog descriptor/group under construction.
    req={'effective_state':state}
    # Current authored property/case name, unchanged by normalization.
    # Current identity/unit strings for a catalog column.
    for name,items in {'group_id':groups,'request_id':['motion']*R,'schema_version':['3.0-wip.4']*R,'construction_profile':['1.0']*R}.items():req[name]=c.pool(items)
    if state==0:
        # Owned pool text values before replacement.
        # Current authored request group identity.
        texts=[js({'fixture':True,'group_id':group,'request_id':'motion'}).decode() for group in groups]
        # Effective JSON text whose exact UTF-8 bytes are hashed.
        req['effective_json']=c.pool(texts); req['effective_sha256']=c.arr('u8',b''.join(sha(t.encode()) for t in texts),[R,32])
    return ch,req

def values(c,array,kind,prefix):
    """Return (values descriptor, encoding evidence) for matrix array in c.

    kind selects channel/sample versus level/channel labels; prefix names sidecars.
    Uses four-value frames, public reversible zfpy, exact roundtrip assertions and
    raw-on-tie selection. Owns all payload/index/expected bytes in c.sidecars."""
    # Write the pinned reversible profile and frame each logical matrix row.
    # Independently compress/decode each frame, then select raw unless ZFP is smaller.
    # Write canonical records, exact verification columns and raw sidecar oracles.
    # Logical row count.
    # Logical inner matrix extent.
    # Values descriptor/group selected for this operation.
    outer,inner=array.shape; v={'shape':[outer,inner],'dtype':'float64','axis_names':['channel','sample'] if kind==0 else ['level','channel'],'frame_schema':1,'nominal_frame_values':4,'row_profile_index':c.arr('u32',[0]*outer)}
    # Created pool/profile descriptor or selected artifact path.
    p={}
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    # Explicit dtype or text entry used by the serialization step.
    for n,x,t in [('profile_index',[0],'u32'),('mode',[0],'u8'),('codec_version',[5],'u32'),('stream_word_bits',[64],'u32'),('rounding',[0],'i8'),('mode_code',[2176],'u64'),('tolerance_present',[0],'u8'),('requested_tolerance',[0.0],'f64'),('effective_tolerance',[0.0],'f64')]:p[n]=c.arr(t,x)
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    for n,x in {'codec':'zfp','library_version':'1.0.1','library_revision':'f40868a6a1c190c802e7d8b5987064f044bf7812','byte_order':'little','execution':'serial'}.items():p[n]=c.pool([x])
    # Owned canonical frame records before packing.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    # Owned per-frame compression-choice evidence.
    v['profiles']=p; records=[]; payload=bytearray(); modes=[]
    # Logical matrix row; original value order is preserved.
    for row in range(outer):
        # First value index of this at-most-four-value frame.
        for begin in range(0,inner,4):
            # Contiguous little-endian binary64 input frame.
            # Owned exact bytes before publication or a deliberate mutation.
            # Public zfpy output including its complete header.
            source=np.ascontiguousarray(array[row,begin:begin+4],dtype='<f8'); raw=source.tobytes(); encoded=bytes(zfpy.compress_numpy(source,write_header=True))
            assert zfpy.decompress_numpy(encoded).tobytes()==raw
            # Raw/ZFP selector: compressed only when strictly smaller than raw.
            # Selected exact raw/ZFP frame bytes.
            encoding=int(len(encoded)<len(raw)); actual=encoded if encoding else raw
            records.append((row,begin,len(source),len(payload),len(actual),encoding,0,sha(actual)));payload.extend(actual)
            modes.append({'outer':row,'begin':begin,'count':len(source),'raw_bytes':len(raw),'zfp_bytes':len(encoded),'actual_encoding':encoding,'raw_tie':len(encoded)==len(raw)})
    # Packed canonical 77-byte frame-index records.
    # Current borrowed run or decoded frame descriptor.
    index=b''.join(FRAME.pack(*r) for r in records)
    v['frame_index']=c.arr('frames77',index,[len(records)]);v['payload']=c.arr('u8',payload)
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    # Explicit dtype or text entry used by the serialization step.
    # Current borrowed run or decoded frame descriptor.
    v['verification']={n:c.arr(t,x) for n,x,t in [('max_abs_error',[0.0]*len(records),'f64'),('rms_error',[0.0]*len(records),'f64'),('verified_count',[r[2] for r in records],'u64'),('bitwise_equal',[1]*len(records),'u8')]}
    c.sidecars[prefix+'.index.bin']=index;c.sidecars[prefix+'.payload.bin']=bytes(payload);c.sidecars[prefix+'.expected.bin']=array.astype('<f8').tobytes();c.sidecars[prefix+'.shape.txt']=f'{outer} {inner}\n'.encode('ascii')
    return v,modes

def make(name,runs,optional=0,request_state=0,partial=False,large_document=False,retain_requests=False,clean_partial=False):
    """Return an unencoded Container for named case and ordered runs.

    optional and request_state select retained-section states. partial adds a log
    fragment; clean_partial marks incomplete capture without a fragment.
    large_document makes the explicit size case; retain_requests keeps an empty
    selection catalog. name supplies sidecar stems. No artifacts are written here."""
    # Build the envelope and optional original/context/log evidence.
    # Build the ordered run index, unavailable or prepared run catalogs/axes/frames.
    # Collect exact expected values and sidecar bytes, then return the owned assembly.
    # Owned fixture Container or current scalar index, according to this activity.
    # Root descriptor or decoded root section ID.
    # Current column name or bounded scalar extent selected by this activity.
    c=Container(); root={'format':{'name':'serene.outputs','version_major':1,'version_minor':0,'file_id':'00000000-0000-4000-8000-000000000001','created_utc':'2026-11-08T00:00:00Z','writer_name':'independent-g0c-author','writer_version':'0.1','write_state':'complete','required_features':FEATURES.copy(),'sections':{n:3 if (partial or clean_partial) and n=='logs' else optional for n in ['inputs','logs','context']}}}
    if optional in (0,3):
        # Exact original-input JSON bytes preserving whitespace and line endings.
        original=b'{ "fixture": true, "note": "exact source bytes" }\r\n'
        # Exact original-input JSON bytes preserving whitespace and line endings.
        if large_document:original=b'{"large":"'+b'x'*(131072)+b'"}\n'
        # Current column name or bounded scalar extent selected by this activity.
        # Current literal column values or exact bits from this fixture table.
        root['inputs']={n:c.document(x) for n,x in {'source_json':original,'effective_json':{'fixture':True,'defaulted':True},'overrides_json':{'argv':[]},'resources_json':{'resources':[]}}.items()}
        root['context']={'execution_json':c.document({'argv':['fixture author','--label','blade α'],'environment':{}})}
        # Owned exact bytes before publication or a deliberate mutation.
        # Initial/terminal UTF-8 or JSONL byte boundaries.
        # Partial JSONL suffix excluded from complete-record boundaries.
        raw=b'{"event":"begin","scope":"fixture"}\n{"event":"end","scope":"fixture"}\r\n';offsets=[0,raw.index(b'\n')+1,len(raw)];fragment=b'{"event":"partial' if partial else b''
        root['logs']={'records_jsonl':c.arr('u8',raw+fragment),'record_offsets':c.arr('u64',offsets),'complete_record_count':2,'trailing_fragment_bytes':len(fragment),'dropped_count_present':0,'dropped_count':0,'schema':'fixture-jsonl-1','info_json':c.document({'association':'all indexed fixture runs','scope':'fixture-author','incomplete_reason':'capture_stopped' if partial or clean_partial else ''})}
    # Owned ordered run-index descriptor.
    # Current borrowed run or decoded frame descriptor.
    index={'run_index':c.arr('u64',list(range(len(runs)))),'run_id':c.pool([r['id'] for r in runs]),'run_name':c.pool([r.get('name','') for r in runs],[int('name'in r) for r in runs]),'kind':c.arr('u8',[r['kind'] for r in runs])}
    # Current dense row/frame/section ordinal in this activity.
    # Current borrowed run or decoded frame descriptor.
    for i,r in enumerate(runs):
        # Dense section ID in the fixture graph.
        # Wire kind selecting the current run or section interpretation.
        # Converted little-endian binary64 fixture matrix.
        # Scalar-channel count implied by run kind and matrix orientation.
        # Committed sample/level count implied by matrix orientation.
        sid=c.add(1,b'');c.run_ids.append(sid); kind=r['kind'];array=np.asarray(r['array'],dtype='<f8');C=array.shape[0] if kind==0 else array.shape[1];K=array.shape[1] if kind==0 else array.shape[0]
        # Owned metadata object for the current run.
        run={'info':run_info(c,r,C,K)};c.runs.append(run)
        if r.get('unavailable'):
            c.expected.append({'run_id':r['id'],'kind':kind,'catalog_state':'unavailable','metadata_section':sid});continue
        run['channels'],run['requests']=catalogs(c,C,request_state,retain_requests)
        # Exact retained integration-step or accepted-level identities.
        run['time' if kind==0 else 'load_levels']=c.arr('f64',r['axis']);ids=r.get('ids',list(range(K)) if kind==0 else list(range(1,K+1)))
        # Deterministic per-run sidecar stem.
        run['step_index' if kind==0 else 'level_index']=c.arr('u64',ids);prefix=f'{name}.r{i:06d}'
        # Owned per-frame compression-choice evidence.
        run['values'],modes=values(c,array,kind,prefix);c.sidecars[prefix+'.axis.bin']=np.asarray(r['axis'],dtype='<f8').tobytes();c.sidecars[prefix+'.identity.bin']=np.asarray(ids,dtype='<u8').tobytes()
        # Current literal column values or exact bits from this fixture table.
        # Logical matrix row; original value order is preserved.
        c.expected.append({'run_id':r['id'],'kind':kind,'metadata_section':sid,'shape':list(array.shape),'axis':r['axis'],'identity_axis':ids,'bits_hex':[[f'{int(x):016x}' for x in row] for row in array.view('<u8')],'sidecar':prefix,'frames':modes,'run_outcome':r.get('outcome',0),'numerical_status':r.get('numerical',1)})
    index['run_metadata_section']=c.arr('u64',c.run_ids);root['results']={'index':index};c.root=root;return c

def envelope_check(blob):
    """Assert exact bootstrap, directory and section hashes/extents in blob.

    blob is complete trusted fixture bytes; returns None. This structural check
    deliberately does not validate scientific or descriptor semantics."""
    # Verify the bootstrap digest/fields and complete directory digest.
    # Walk dense contiguous directory rows, checking every exact section digest.
    assert len(blob)>=128 and sha(blob[:96])==blob[96:128]
    # Complete decoded bootstrap tuple.
    # Unused fixed-layout field, deliberately not used by this structural assertion.
    # Expected major version one.
    # Expected minor version zero.
    # Expected little-endian discriminator.
    # Expected 128-byte bootstrap size.
    # Expected 64-byte directory row size.
    # Declared total file byte extent.
    # Directory or payload byte offset decoded from the fixed layout.
    # Record/directory population decoded from the fixed layout.
    # Root descriptor or decoded root section ID.
    # Required finalized bootstrap flag.
    # Recorded directory SHA-256.
    fields=HEADER96.unpack(blob[:96]);_,major,minor,endian,hsize,rsize,size,start,count,root,flags,digest=fields
    assert (major,minor,endian,hsize,rsize,size,root,flags)==(1,0,0x01020304,128,64,len(blob),0,1)
    assert start+64*count==size and sha(blob[start:])==digest
    # Next section offset or explicit header mutation byte offset.
    offset=128
    # Current dense row/frame/section ordinal in this activity.
    for i in range(count):
        # Dense section ID in the fixture graph.
        # Wire kind selecting the current run or section interpretation.
        # Required zero directory flags.
        # First value index of this at-most-four-value frame.
        # Current column name or bounded scalar extent selected by this activity.
        # Recorded exact section digest.
        sid,kind,flag,begin,n,hash_=DIRECTORY.unpack_from(blob,start+64*i)
        assert sid==i and kind in range(1,9) and flag==0 and begin==offset and sha(blob[begin:begin+n])==hash_
        offset+=n
    assert offset==start

def positive(out,manifest,name,container):
    """Write named positive container and sidecars beneath out; update manifest.

    container is sealed/encoded, name forms filenames, out is a Path, and
    manifest receives the expected-file entry. Returns None."""
    # Encode and independently check the envelope, then write archive and sidecars.
    # Write owned expected JSON and append its manifest entry.
    # Owned exact bytes before publication or a deliberate mutation.
    raw=container.encode();envelope_check(raw);(out/(name+'.serene')).write_bytes(raw)
    # Sidecar filename from owned fixture evidence.
    # Owned encoded payload bytes.
    for file,data in container.sidecars.items():(out/file).write_bytes(data)
    # Owned expected run/value/byte oracle.
    # Expected-JSON artifact path or filename.
    expected={'file':name+'.serene','runs':container.expected}; ep=name+'.expected.json';(out/ep).write_text(json.dumps(expected,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    manifest['positive'].append({'file':name+'.serene','expected':ep,'description':name.replace('_',' ')})

# Negative authoring below changes one primary invariant and repairs supporting hashes/extents.
def frame_records(c):
    """Return first-run values descriptor and mutable decoded frame rows from c.

    The descriptor is borrowed; record lists are new copies for targeted mutation."""
    # Decode each canonical 77-byte row with the independent struct layout.
    # Values descriptor/group selected for this operation.
    # Current dense row/frame/section ordinal in this activity.
    v=c.runs[0]['values'];return v,[list(FRAME.unpack_from(c.data(v['frame_index']),i)) for i in range(0,len(c.data(v['frame_index'])),77)]
def store_records(c,v,records):
    """Replace c frame-index section addressed by v using records; return None.

    v is the in-out values descriptor, records are decoded canonical row values."""
    # Pack every record and update the existing frame-index section/shape.
    # Current borrowed run or decoded frame descriptor.
    c.replace(v['frame_index'],b''.join(FRAME.pack(*r) for r in records),shape=[len(records)])
def edit_frame(c,field,value,index=0):
    """Mutate numeric frame field in c at index (default first) to value.

    field is the tuple column ordinal. When changing count, repair its verification
    count too so the intended primary invariant remains isolated. Returns None."""
    # Decode rows, apply one field replacement and store the canonical index.
    # Repair the associated verified-count column only for count mutations.
    # Values descriptor/group selected for this operation.
    # Current borrowed run or decoded frame descriptor.
    v,r=frame_records(c);r[index][field]=value;store_records(c,v,r)
    if field==2:
        # Private verified-count column repaired alongside a frame-count mutation.
        counts=np.frombuffer(c.data(v['verification']['verified_count']),dtype='<u8').copy();counts[index]=value;c.replace(v['verification']['verified_count'],counts)
def replace_frame_payload(c,index,new,encoding=None):
    """Replace frame index in c with new bytes, optionally changing encoding.

    Repairs that frame digest/size and all later offsets; records/index/sections
    are rebuilt while unrelated frame payloads remain exact. Returns None."""
    # Splice the chosen frame bytes, repair its metadata and shift subsequent offsets.
    # Replace payload/index sections without altering other logical frame fields.
    # Values descriptor/group selected for this operation.
    # Owned canonical frame records before packing.
    # Selected mutable canonical frame row.
    # Original frame extent/bits or section identity before mutation.
    # Directory or payload byte offset decoded from the fixed layout.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    v,records=frame_records(c);record=records[index];old=record[4];start=record[3];payload=c.data(v['payload']);payload=payload[:start]+new+payload[start+old:]
    record[4]=len(new);record[7]=sha(new)
    if encoding is not None:record[5]=encoding
    # Subsequent frame whose byte offset shifts after replacement.
    for later in records[index+1:]:later[3]+=len(new)-old
    c.replace(v['payload'],payload,shape=[len(payload)]);store_records(c,v,records)
def request_json(c,value):
    """Replace first effective request JSON in c with exact string value.

    Repairs the string pool and each request digest; malformed JSON may be intentional."""
    # Decode the existing pool, substitute one text, and rebuild pool/digest evidence.
    # Current borrowed run or decoded frame descriptor.
    # Owned pool text values before replacement.
    # Explicit dtype or text entry used by the serialization step.
    r=c.runs[0]['requests'];texts=c.pool_values(r['effective_json']);texts[0]=value;c.replace_pool(r['effective_json'],texts);c.replace(r['effective_sha256'],b''.join(sha(t.encode()) for t in texts))
def header_edit(blob,offset,fmt,value,repair=True):
    """Return blob with struct fmt/value written at bootstrap byte offset.

    repair=True recomputes the bootstrap digest; false intentionally leaves it stale.
    Copies blob first and does not change the caller bytes."""
    # Copy bytes, apply the requested field edit, and optionally repair its digest.
    # Private byte storage or selected byte-range boundary.
    b=bytearray(blob);struct.pack_into(fmt,b,offset,value)
    if repair:b[96:128]=sha(b[:96])
    return bytes(b)
def directory_edit(blob,row,offset,fmt,value):
    """Return blob with fmt/value written at offset within directory row.

    row is the canonical record ordinal and offset is row-relative bytes. Repairs
    directory/bootstrap hashes while retaining the chosen malformed primary field."""
    # Locate the directory, mutate one record field, and repair enclosing digests.
    # Private byte storage or selected byte-range boundary.
    # Directory or payload byte offset decoded from the fixed layout.
    # Record/directory population decoded from the fixed layout.
    b=bytearray(blob);start,count=struct.unpack_from('<QQ',b,32);struct.pack_into(fmt,b,start+row*64+offset,value);b[64:96]=sha(b[start:start+64*count]);b[96:128]=sha(b[:96]);return bytes(b)
def negatives(out,manifest,bases):
    """Return a negative-case writer bound to out, manifest and pristine bases.

    Returned closure deep-copies its chosen base before mutation; owned fixture
    objects remain isolated and no production writer is called."""
    # Capture destination/manifest/base ownership and expose the isolated mutator closure.
    def n(name,reason,primary,change=None,base='mixed_timeseries',binary=None,phase='select_run',repair='All affected section, directory and bootstrap hashes and physical extents rebuilt.'):
        """Write one negative fixture and append its expected phase/category metadata.

        name/reason/primary label the case and invariant. change mutates a deep
        copied Container from base; binary optionally edits encoded bytes afterward.
        phase and repair describe the intended validation stage/supporting repairs."""
        # Deep-copy the selected base and apply descriptor mutation if supplied.
        # Encode, apply any binary edit, then persist exact bytes and manifest expectations.
        # Owned fixture Container or current scalar index, according to this activity.
        c=copy.deepcopy(bases[base]);
        if change:change(c)
        # Owned exact bytes before publication or a deliberate mutation.
        raw=c.encode()
        # Owned exact bytes before publication or a deliberate mutation.
        if binary:raw=binary(raw,c)
        # Negative archive filename used by its manifest expectation.
        filename=name+'.serene';(out/filename).write_bytes(raw);manifest['negative'].append({'file':filename,'base':base+'.serene','expected_error':reason,'primary_invariant':primary,'validation_phase':phase,'supporting_repairs':repair})
    # Appended below once the exact envelope and positives are established.
    return n

def main():
    """Author selected independent fixtures beneath the guarded output directory.

    Reads CLI --output/--positive-only; writes archive/sidecar/manifest artifacts
    explicitly and prints counts/hash. Never invoked implicitly by documentation."""
    # Validate the output root and construct positive matrices/envelopes.
    # Write positive cases and optional isolated negatives.
    # Pin current spec/author/artifact identities in the new manifest and print a receipt.
    # CLI parser for explicit output and positive-only selection.
    # Parsed authoring options.
    # Resolved and guarded fixtures_custom output directory.
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'fixtures_custom');parser.add_argument('--positive-only',action='store_true');args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to((ROOT/'fixtures_custom').resolve());out.mkdir(parents=True,exist_ok=True)
    # Owned fixture revision, expectations and artifact identities.
    manifest={'fixture_revision':'g0c-candidate-1','format':[1,0],'author':'independent Python struct/hashlib/json + NumPy/public zfpy; no HDF5 or production helpers','header_bytes':128,'directory_record_bytes':64,'frame_record_bytes':77,'positive':[],'negative':[]}
    # Private NumPy matrix/array copy or selected byte-range boundary.
    a=np.zeros((3,10),dtype='<f8');a[1]=np.array([0x8000000000000000,1,0x8000000000000001,0x0010000000000000,0x3ff0000000000000,0x4000000000000000,0x3fd5555555555555,0xbff4000000000000,0x3ff0000000000001,0x400921fb54442d18],dtype='<u8').view('<f8');a[2]=np.arange(10,dtype='<f8')/8
    # Pristine transient run description with asymmetric exact value bits.
    # Current deterministic sample/artifact index.
    base={'id':'run/α','name':'same display name','kind':0,'array':a,'axis':[j*.01 for j in range(10)],'ids':list(range(10))}
    # Static multi-level run description with sparse accepted-level IDs.
    static={'id':'static β','name':'same display name','kind':1,'array':np.array([[0.,-0.,np.nextafter(0.,1.),1.,2.],[2.5,-3.,0.,4.,5.]],dtype='<f8'),'axis':[.5,1.],'ids':[3,7]}
    # Pristine positive assemblies reused only through deep copies.
    bases={'mixed_timeseries':make('mixed_timeseries',[base])}
    if not args.positive_only:
        bases.update({'static_levels':make('static_levels',[static]),'mixed_bundle':make('mixed_bundle',[base,static]),
          'empty_selection':make('empty_selection',[{**base,'array':np.empty((0,0)),'axis':[],'ids':[]}],retain_requests=True),
          'failed_prefix':make('failed_prefix',[{**base,'array':a[:,:2],'axis':[0.,.04],'ids':[0,4],'outcome':1,'numerical':3,'failure_stage':'integration','failure_code':'fixture_failure','termination_reason':'solver_failure','accepted_count':6,'last_accepted':.06}]),
          'no_committed_samples':make('no_committed_samples',[{**base,'array':np.empty((3,0)),'axis':[],'ids':[],'outcome':2,'numerical':2,'observation':1,'failure_stage':'observation','failure_code':'fixture_failure','termination_reason':'observation_failure','accepted_count':0,'last_accepted':0.}]),
          'unavailable_catalog':make('unavailable_catalog',[{**base,'array':np.empty((0,0)),'axis':[],'ids':[],'unavailable':True,'outcome':4,'numerical':0,'observation':3,'logging':4,'accepted_count':0,'failure_stage':'preparation','failure_code':'fixture_failure','termination_reason':'preparation_failure'}],optional=2,request_state=2),
          'optional_disabled':make('optional_disabled',[base],optional=1,request_state=1),'optional_unavailable':make('optional_unavailable',[base],optional=2,request_state=2),
          'partial_logs':make('partial_logs',[{**base,'logging':1,'outcome':3,'termination_reason':'logging_failure','failure_stage':'logging','failure_code':'partial_capture'}],partial=True),
          'failed_static':make('failed_static',[{**static,'array':np.empty((0,5)),'axis':[],'ids':[],'outcome':1,'numerical':3,'failure_stage':'equilibrium','failure_code':'fixture_failure','termination_reason':'solver_failure','accepted_count':0,'last_accepted':0.}]),
          'large_document':make('large_document',[base],large_document=True),
          'clean_partial_logs':make('clean_partial_logs',[{**base,'logging':1,'outcome':3,'failure_stage':'logging','failure_code':'capture_stopped'}],clean_partial=True)})
    # Current authored property/case name, unchanged by normalization.
    # Owned fixture Container or current scalar index, according to this activity.
    for name,c in bases.items():positive(out,manifest,name,c)
    if not args.positive_only:author_negatives(negatives(out,manifest,bases))
    manifest['spec_sha256']=hashlib.sha256((ROOT/'spec/FORMAT.md').read_bytes()).hexdigest();manifest['author_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    # Created pool/profile descriptor or selected artifact path.
    manifest['files']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.iterdir()) if p.is_file() and p.name!='manifest.json'}
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({'positive':len(manifest['positive']),'negative':len(manifest['negative']),'files':len(manifest['files']),'manifest_sha256':hashlib.sha256((out/'manifest.json').read_bytes()).hexdigest()}))

def author_negatives(n):
    """Call case writer n for the maintained malformed-input matrix; return None.

    n owns output/base/manifest handling. Each helper edits one primary invariant,
    repairing support where specified; phase labels are expectations, not execution."""
    # Author envelope/version/hash and metadata/ownership failures.
    # Author catalog/profile/frame/finite-value failures with repaired enclosing evidence.
    # Author request/log complexity and run/verification consistency failures.
    # Envelope admission: isolate truncation, unsupported capabilities, finalized state and exact byte extents.
    n('truncated_bootstrap','invalid_contract','Bootstrap must contain 128 bytes.',binary=lambda b,c:b[:100],phase='open',repair='Deliberately truncated; no hash repair can restore missing bytes.')
    n('truncated_directory','invalid_contract','Exact actual file extent must include the complete directory.',binary=lambda b,c:b[:-1],phase='open',repair='Declared extent retained to expose truncation.')
    n('trailing_data','invalid_contract','Actual file extent cannot contain an ignored tail.',binary=lambda b,c:b+b'X',phase='open',repair='Declared extent retained to expose trailing bytes.')
    n('bad_magic','unsupported_feature','Magic must identify the Serene custom container.',binary=lambda b,c:header_edit(b,0,'8s',b'NOTSEREN'),phase='open',repair='Bootstrap digest repaired.')
    # Current authored property/case name, unchanged by normalization.
    # Next section offset or explicit header mutation byte offset.
    # Exact little-endian struct format for the current header mutation.
    # Current literal fixture field value.
    for name,offset,fmt,value in [('unknown_major',8,'<H',2),('unknown_minor',10,'<H',1),('unknown_endian',12,'<I',0x04030201),('unknown_bootstrap_flag',56,'<Q',3)]:
        n(name,'unsupported_feature','Unsupported bootstrap capability: '+name,binary=lambda b,c,o=offset,f=fmt,v=value:header_edit(b,o,f,v),phase='open',repair='Bootstrap digest repaired.')
    n('unfinished_bootstrap','invalid_contract','Finalized flag must be set.',binary=lambda b,c:header_edit(b,56,'<Q',0),phase='open',repair='Bootstrap digest repaired.')
    n('bootstrap_size','invalid_contract','Bootstrap byte count is exactly 128.',binary=lambda b,c:header_edit(b,16,'<I',127),phase='open',repair='Bootstrap digest repaired.')
    n('directory_record_size','invalid_contract','Directory records contain exactly 64 bytes.',binary=lambda b,c:header_edit(b,20,'<I',63),phase='open',repair='Bootstrap digest repaired.')
    n('file_extent_mismatch','invalid_contract','Declared file extent equals the actual extent.',binary=lambda b,c:header_edit(b,24,'<Q',len(b)-1),phase='open',repair='Bootstrap digest repaired.')
    n('directory_offset_overflow','invalid_contract','Directory offset/length must be subtraction-safe and within the file.',binary=lambda b,c:header_edit(b,32,'<Q',2**64-1),phase='open',repair='Bootstrap digest repaired.')
    n('directory_count_limit','resource_limit','Protocol directory count is at most 1048576.',binary=lambda b,c:header_edit(b,40,'<Q',1048577),phase='open',repair='Bootstrap digest repaired; limit must be checked before dependent reads/allocation.')
    n('root_section_out_of_range','invalid_contract','Root ID must identify an existing section.',binary=lambda b,c:header_edit(b,48,'<Q',len(c.sections)),phase='open',repair='Bootstrap digest repaired.')
    n('root_section_wrong_kind','invalid_contract','Root metadata must have known JSON kind1.',binary=lambda b,c:header_edit(b,48,'<Q',1),phase='open',repair='Bootstrap digest repaired; section1 is an existing u8 document section.')
    # Integrity admission: corrupt one stored digest while repairing the surrounding evidence when specified.
    n('bad_bootstrap_hash','verification_failure','Bootstrap digest covers the first 96 bytes.',binary=lambda b,c:b[:96]+bytes([b[96]^1])+b[97:],phase='open',repair='Only the bootstrap digest byte is corrupted.')
    n('bad_directory_hash','verification_failure','Directory digest covers every directory record.',binary=lambda b,c:header_edit(b,64,'B',b[64]^1),phase='open',repair='Bootstrap digest repaired; directory digest deliberately incorrect.')
    # Directory admission: preserve the surrounding hashes while changing identity, kind, flags or contiguous extents.
    n('directory_id_not_dense','invalid_contract','Section IDs equal zero-based directory row ordinals.',binary=lambda b,c:directory_edit(b,1,0,'<Q',7),phase='open',repair='Directory/bootstrap digests repaired.')
    n('unknown_section_kind','unsupported_feature','Only the eight declared section kinds are supported.',binary=lambda b,c:directory_edit(b,1,8,'<I',9),phase='open',repair='Directory/bootstrap digests repaired.')
    n('unknown_directory_flag','unsupported_feature','All directory flags are zero.',binary=lambda b,c:directory_edit(b,1,12,'<I',1),phase='open',repair='Directory/bootstrap digests repaired.')
    n('section_gap','invalid_contract','Section extents are contiguous from offset128.',binary=lambda b,c:directory_edit(b,0,16,'<Q',129),phase='open',repair='Directory/bootstrap digests repaired; extent intentionally invalid before section reads.')
    n('section_overlap','invalid_contract','Sections cannot overlap their predecessor.',binary=lambda b,c:directory_edit(b,1,16,'<Q',128),phase='open',repair='Directory/bootstrap digests repaired; extent intentionally invalid before section reads.')
    n('section_extent_overflow','invalid_contract','Section offset+length must be checked before following it.',binary=lambda b,c:directory_edit(b,1,24,'<Q',2**64-1),phase='open',repair='Directory/bootstrap digests repaired; impossible extent intentionally retained.')
    n('bad_root_hash','verification_failure','Root bytes must match their directory digest.',binary=lambda b,c:directory_edit(b,0,32,'B',b[struct.unpack_from('<Q',b,32)[0]+32]^1),phase='open',repair='Directory/bootstrap digests repaired; root section hash deliberately incorrect.')
    # Root metadata admission: exercise required-feature completeness/uniqueness and version agreement.
    n('unknown_feature','unsupported_feature','Unknown required features cannot be ignored.',lambda c:c.root['format']['required_features'].append('future-required'),phase='open')
    n('missing_feature','invalid_contract','All five mandatory feature tokens must be present.',lambda c:c.root['format']['required_features'].pop(),phase='open')
    n('duplicate_feature','invalid_contract','Required feature tokens are unique.',lambda c:c.root['format']['required_features'].append(FEATURES[0]),phase='open')
    n('root_version_disagrees','invalid_contract','Root metadata version must agree with accepted bootstrap1.0.',lambda c:c.root['format'].__setitem__('version_minor',1),phase='open')
    def raw_extra(c,raw):
        """Override c root bytes with comment value raw inserted verbatim.

        raw is intentionally unparsed JSON token bytes; returns None."""
        # Keep normal root fields while injecting the selected lexical violation.
        c.overrides[0]=b'{"comment":'+raw+b','+js(c.root)[1:]
    # Strict JSON admission: inject raw tokens so the author does not normalize malformed spelling away.
    n('malformed_metadata','invalid_contract','Metadata parses as a strict complete object.',lambda c:c.overrides.__setitem__(0,b'{"format":'),phase='open')
    n('duplicate_decoded_key','invalid_contract','Metadata duplicate keys are compared after JSON escape decoding.',lambda c:c.overrides.__setitem__(0,b'{"comment":1,"\\u0063omment":2,'+js(c.root)[1:]),phase='open')
    n('metadata_lone_surrogate','invalid_contract','Decoded metadata strings contain Unicode scalar values.',lambda c:raw_extra(c,b'"\\ud800"'),phase='open')
    n('metadata_nul','invalid_contract','Decoded metadata strings cannot contain U+0000.',lambda c:raw_extra(c,b'"\\u0000"'),phase='open')
    n('metadata_nan','invalid_contract','Metadata rejects nonfinite JSON literals.',lambda c:raw_extra(c,b'NaN'),phase='open')
    n('metadata_negative_integer_zero','invalid_contract','Integer negative-zero tokens are forbidden.',lambda c:raw_extra(c,b'-0'),phase='open')
    n('metadata_depth_limit','resource_limit','Metadata container depth cannot exceed16.',lambda c:raw_extra(c,b'['*16+b'0'+b']'*16),phase='open')
    n('metadata_value_limit','resource_limit','Metadata syntactic values cannot exceed16384.',lambda c:c.root.__setitem__('comment',[0]*16384),phase='open')
    n('metadata_byte_limit','resource_limit','Metadata section byte length cannot exceed1MiB.',lambda c:c.root.__setitem__('comment','x'*1048576),phase='open')
    n('metadata_numeric_token_limit','resource_limit','A JSON numeric token cannot exceed128ASCII bytes.',lambda c:raw_extra(c,b'1'*129),phase='open')
    # Typed run/array admission: distinguish integer spellings and require exact dtype, shape and section roles.
    n('boolean_integer_flag','invalid_contract','Integer presence flags do not accept JSON booleans.',lambda c:c.runs[0]['info'].__setitem__('last_accepted_present',True))
    n('floating_integer_field','invalid_contract','Integer fields do not accept integral float tokens.',lambda c:c.runs[0]['info'].__setitem__('committed_count',10.0))
    n('negative_unsigned_field','invalid_contract','Unsigned fields reject negative integers.',lambda c:c.runs[0]['info'].__setitem__('accepted_count',-1))
    n('wrong_time_dtype','invalid_contract','Time descriptor dtype/kind must be f64.',lambda c:c.runs[0]['time'].__setitem__('dtype','u64'))
    n('wrong_time_rank','invalid_contract','Actual time is rank1.',lambda c:c.runs[0]['time'].__setitem__('shape',[2,5]))
    n('shape_extent_mismatch','invalid_contract','Descriptor shape product equals section byte extent.',lambda c:c.runs[0]['time'].__setitem__('shape',[9]))
    n('bad_reference','invalid_contract','A descriptor section ID must exist.',lambda c:c.runs[0]['time'].__setitem__('section',len(c.sections)+3))
    n('metadata_as_data','invalid_contract','A run cannot interpret its metadata as numerical data.',lambda c:c.runs[0]['time'].__setitem__('section',c.run_ids[0]))
    # Whole-graph ownership: test both orphan payloads and an alias with the otherwise orphaned section removed.
    n('orphan_section','invalid_contract','Every physical section must have exactly one schema owner.',lambda c:c.add(2,b'not-referenced'),phase='verify_all')
    def erase(c,sid):
        """Remove section sid from in-out c and remap surviving descriptor IDs.

        Used after creating an alias so the removed section is not a second orphan.
        Assert no remaining array descriptor references sid; returns None."""
        # Remove the physical section, recursively shift later descriptor IDs.
        # Shift run metadata IDs and repair the root catalog column.
        c.sections.pop(sid)
        def walk(x):
            """Recursively adjust array section indexes in in-out descriptor tree x.

            Captures removed sid; asserts it is no longer referenced and returns None."""
            # Recognize exact array descriptors, otherwise recurse through dictionaries/lists.
            if isinstance(x,dict):
                if set(x)=={'section','dtype','shape'}:
                    assert x['section']!=sid
                    x['section']-=int(x['section']>sid)
                else:
                    # Borrowed metadata child remapped after section removal.
                    for v in x.values():walk(v)
            elif isinstance(x,list):
                # Borrowed metadata child remapped after section removal.
                for v in x:walk(v)
        walk(c.root)
        # Current borrowed run or decoded frame descriptor.
        for r in c.runs:walk(r)
        # Run-metadata section ID shifted past the removed section.
        c.run_ids=[x-int(x>sid) for x in c.run_ids]
        c.replace(c.root['results']['index']['run_metadata_section'],c.run_ids)
    def alias(c):
        """Make two verification fields in c share one section, removing the orphan."""
        # Point RMS at maximum-error data, then erase its formerly owned section.
        # Verification descriptor/group whose support is independently authored or repaired.
        # Original frame extent/bits or section identity before mutation.
        verify=c.runs[0]['values']['verification'];old=verify['rms_error']['section'];verify['rms_error']['section']=verify['max_abs_error']['section'];erase(c,old)
    n('aliased_section','invalid_contract','Even identical data fields cannot share a schema-owned section.',alias,phase='verify_all')
    def wrong_run(c):
        """Point c first run metadata index at an existing numeric data section."""
        # Replace the catalog ID while preserving the referenced section itself.
        c.replace(c.root['results']['index']['run_metadata_section'],[c.runs[0]['time']['section']])
    n('run_metadata_wrong_kind','invalid_contract','Run metadata IDs reference JSON metadata sections.',wrong_run,phase='open')
    def mutate_array(c,d,index,value):
        """Replace element index of c descriptor d with value using its existing dtype.

        Copies the decoded array before mutation; returns None."""
        # Decode a private typed copy, modify one element, and replace the section bytes.
        # Private NumPy matrix/array copy or selected byte-range boundary.
        a=np.frombuffer(c.data(d),dtype=TYPES[d['dtype']][1]).copy();a[index]=value;c.replace(d,a)
    # Catalog admission: test exact UTF-8 pool boundaries, unique scalar identities and per-entry limits.
    n('bad_utf8','invalid_contract','Catalog pool slices must decode as strict UTF8.',lambda c:mutate_array(c,c.runs[0]['channels']['scalar_id']['bytes'],0,255))
    n('offset_not_zero','invalid_contract','String offsets start at zero.',lambda c:mutate_array(c,c.runs[0]['channels']['scalar_id']['offsets'],0,1))
    def terminal(c):
        """Shorten c scalar-ID pool terminal offset without changing its byte payload."""
        # Mutate only the final offset to expose the terminal-extent invariant.
        # Created pool/profile descriptor or selected artifact path.
        p=c.runs[0]['channels']['scalar_id'];mutate_array(c,p['offsets'],-1,len(c.data(p['bytes']))-1)
    n('offset_terminal','invalid_contract','Final string offset equals pool bytes.',terminal)
    def duplicate_scalar(c):
        """Make c second scalar ID equal its first with valid UTF-8/pool extents."""
        # Decode the string pool, duplicate one identity, and rebuild its support.
        # Created pool/profile descriptor or selected artifact path.
        # Owned decoded scalar-ID list with one identity deliberately duplicated.
        p=c.runs[0]['channels']['scalar_id'];v=c.pool_values(p);v[1]=v[0];c.replace_pool(p,v)
    n('duplicate_scalar_id','invalid_contract','Scalar IDs must be unique.',duplicate_scalar)
    n('string_entry_limit','resource_limit','One catalog text entry cannot exceed1MiB.',lambda c:c.replace_pool(c.runs[0]['channels']['scalar_id'],['x'*1048577,'second','third']))
    # Reference/frame admission: require valid request/profile mappings even when no inner values are retained.
    n('bad_request_index','invalid_contract','Channel requests reference an existing matching identity row.',lambda c:mutate_array(c,c.runs[0]['channels']['request_index'],0,99))
    n('bad_profile_index','invalid_contract','Frame profile is valid and matches its row.',lambda c:edit_frame(c,6,9))
    n('empty_row_profile_index','invalid_contract','Row profiles remain valid when inner extent is zero.',lambda c:mutate_array(c,c.runs[0]['values']['row_profile_index'],0,99),base='no_committed_samples')
    n('coverage_overlap','invalid_contract','Frames partition each logical row without overlap.',lambda c:edit_frame(c,1,3,index=1))
    n('zero_frame_count','invalid_contract','Every indexed frame contains at least one value.',lambda c:edit_frame(c,2,0))
    # Frame integrity: preserve enclosing index/section hashes while invalidating only the selected payload digest.
    n('bad_frame_digest','verification_failure','Stored frame digest matches exact frame bytes.',lambda c:edit_frame(c,7,b'\0'*32),phase='read_frame',repair='Index/section/directory/bootstrap hashes repaired; frame hash deliberately incorrect.')
    def bad_index(blob,c):
        """Return blob with c frame-index directory digest corrupted.

        Repairs directory/bootstrap digests so index integrity is the primary failure."""
        # Locate the first-run index section and flip its stored digest byte.
        # Dense section ID in the fixture graph.
        # Directory or payload byte offset decoded from the fixed layout.
        sid=c.runs[0]['values']['frame_index']['section'];start=struct.unpack_from('<Q',blob,32)[0];return directory_edit(blob,sid,32,'B',blob[start+sid*64+32]^1)
    n('bad_index_digest','verification_failure','Canonical frame-index section digest must match.',binary=bad_index,repair='Directory/bootstrap hashes repaired; index section hash deliberately incorrect.')
    # Codec admission: separate unsupported profile metadata, dimension limits and selected-frame header failures.
    n('unsupported_profile','unsupported_profile','Only qualified reversible profile rows are accepted.',lambda c:mutate_array(c,c.runs[0]['values']['profiles']['mode'],0,99))
    n('dimension_limit','resource_limit','Logical dimensions cannot exceed2^40.',lambda c:c.runs[0]['values'].__setitem__('shape',[3,2**40+1]))
    n('header_count_mismatch','codec_failure','ZFP header count equals canonical frame count.',lambda c:replace_frame_payload(c,0,bytes(zfpy.compress_numpy(np.zeros(3,dtype='<f8'),write_header=True))),phase='read_frame')
    def raw_length(c):
        """Append one byte to an existing raw frame in c and repair surrounding metadata."""
        # Find a raw frame and replace it with the exact original bytes plus one zero.
        # Values descriptor/group selected for this operation.
        # Owned canonical frame records before packing.
        # Current dense row/frame/section ordinal in this activity.
        # Current borrowed run or decoded frame descriptor.
        v,records=frame_records(c);i=next(i for i,r in enumerate(records) if r[5]==0);r=records[i];replace_frame_payload(c,i,c.data(v['payload'])[r[3]:r[3]+r[4]]+b'\0')
    n('raw_length','invalid_contract','Raw byte count is exactly eight times value count.',raw_length)
    def nonfinite_raw(c):
        """Replace first double of an existing raw frame in c with a quiet NaN."""
        # Find a raw frame and preserve all remaining bytes while repairing its payload digest.
        # Values descriptor/group selected for this operation.
        # Owned canonical frame records before packing.
        # Current dense row/frame/section ordinal in this activity.
        # Current borrowed run or decoded frame descriptor.
        # Owned exact bytes before publication or a deliberate mutation.
        v,records=frame_records(c);i=next(i for i,r in enumerate(records) if r[5]==0);r=records[i];raw=c.data(v['payload'])[r[3]:r[3]+r[4]];replace_frame_payload(c,i,struct.pack('<Q',0x7ff8000000000000)+raw[8:])
    n('nonfinite_raw','invalid_contract','Decoded selected values must be finite (raw).',nonfinite_raw,phase='read_frame')
    n('nonfinite_zfp','invalid_contract','Decoded selected values must be finite (ZFP).',lambda c:replace_frame_payload(c,0,bytes(zfpy.compress_numpy(np.full(4,np.inf,dtype='<f8'),write_header=True))),phase='read_frame')
    def long_header(c):
        """Replace c first ZFP frame with a decodable noncanonical long expert header.

        Public zfpy proves its exact zero decode; strict-profile readers must reject it."""
        # Repack equivalent reversible expert parameters and shift payload bits.
        # Assert public decoding still works, then replace the frame and repair extents/hashes.
        # Values descriptor/group selected for this operation.
        # Current borrowed run or decoded frame descriptor.
        # Original first compressed frame bytes.
        # Original frame extent/bits or section identity before mutation.
        # Manually assembled equivalent long-expert ZFP mode bits.
        v,r=frame_records(c);old_bytes=c.data(v['payload'])[:r[0][4]];old=int.from_bytes(old_bytes,'little');mode=15420;mode=(mode<<7)+63;mode=(mode<<15)+16657;mode=(mode<<15)+0;mode=(mode<<12)+0xfff
        # Repacked bitstream integer containing the equivalent long expert header.
        # Owned exact bytes before publication or a deliberate mutation.
        value=(old&((1<<84)-1))|(mode<<84)|((old>>96)<<148);raw=value.to_bytes(((len(old_bytes)*8+52+63)//64)*8,'little');assert zfpy.decompress_numpy(raw).tobytes()==np.zeros(4,dtype='<f8').tobytes();replace_frame_payload(c,0,raw)
    n('long_expert_header','unsupported_profile','Only canonical SHORT reversible mode2176 is accepted.',long_header,phase='read_frame')
    # Run and request admission: preserve exact authored JSON bytes while testing state, identity and strict-object rules.
    n('outcome_inconsistent','invalid_contract','Completed outcome requires a successfully reached numerical target.',lambda c:c.runs[0]['info'].__setitem__('numerical_status',0))
    n('run_identity_mismatch','invalid_contract','Info run_id agrees with root run catalog.',lambda c:c.runs[0]['info'].__setitem__('run_id','different-run'))
    n('request_json_nan','invalid_contract','Effective JSON objects reject nonfinite literals.',lambda c:request_json(c,'{"x":NaN}'))
    n('request_json_duplicate_key','invalid_contract','Effective JSON objects reject duplicate decoded keys.',lambda c:request_json(c,'{"x":1,"\\u0078":2}'))
    n('request_json_depth_limit','resource_limit','Effective request JSON container depth cannot exceed16.',lambda c:request_json(c,'{"x":'+'['*16+'0'+']'*16+'}'))
    def log_value(c,record):
        """Replace c log evidence with one exact complete-record byte sequence record.

        Repairs offsets/count/fragment metadata to isolate its JSON/complexity issue."""
        # Replace log bytes and boundaries, retaining exactly one complete record.
        # Root log descriptor whose bytes/boundaries/counts are replaced.
        log=c.root['logs'];c.replace(log['records_jsonl'],record,shape=[len(record)]);c.replace(log['record_offsets'],[0,len(record)],shape=[2]);log['complete_record_count']=1;log['trailing_fragment_bytes']=0
    # Full verification: completed JSONL records use bounded strict metadata grammar; partial fragments remain raw evidence.
    n('log_record_limit','resource_limit','A complete JSONL record cannot exceed1MiB.',lambda c:log_value(c,b'{"x":"'+b'x'*1048576+b'"}\n'),phase='verify_all')
    n('log_record_depth_limit','resource_limit','Complete JSONL objects enforce container depth16.',lambda c:log_value(c,b'{"x":'+b'['*16+b'0'+b']'*16+b'}\n'),phase='verify_all')
    n('log_record_nan','invalid_contract','Complete JSONL objects reject nonfinite literals.',lambda c:log_value(c,b'{"x":NaN}\n'),phase='verify_all')
    # Exact-mode evidence and axes: require canonical zero errors plus finite, strictly increasing retained times.
    n('verification_error_nonzero','invalid_contract','Reversible verification errors must be positive zero.',lambda c:mutate_array(c,c.runs[0]['values']['verification']['max_abs_error'],0,0.125))
    n('verification_negative_zero','invalid_contract','Absent exact-mode error evidence is positive zero.',lambda c:mutate_array(c,c.runs[0]['values']['verification']['rms_error'],0,-0.0))
    n('time_not_increasing','invalid_contract','Transient actual times are strictly increasing.',lambda c:mutate_array(c,c.runs[0]['time'],1,0.0))
    n('time_nonfinite','invalid_contract','Actual times are finite binary64 values.',lambda c:mutate_array(c,c.runs[0]['time'],1,float('inf')))


if __name__=='__main__':main()
