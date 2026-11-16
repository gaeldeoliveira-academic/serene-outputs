"""N01 independent tiny fixture author. Uses h5py/zfpy, no Serene code.
Not a production writer: this tool deliberately materializes tiny fixtures.
"""
from pathlib import Path
import copy, hashlib, json, shutil, struct
import h5py
import numpy as np
import zfpy

# Repository-relative fixture author root.
ROOT=Path(__file__).resolve().parents[2]
# Explicit historical-layout artifact destination.
OUT=ROOT/'fixtures'
# Packed 77-byte NumPy compound-record dtype.
INDEX=np.dtype({'names':['outer_index','inner_begin','count','byte_offset','byte_count','encoding','profile_index','sha256'],
 'formats':['<u8','<u8','<u8','<u8','<u8','u1','<u4',('u1',(32,))],
 'offsets':[0,8,16,24,32,40,41,45],'itemsize':77})
assert INDEX.itemsize==77
# Owned expected cases and exact artifact identity manifest.
MANIFEST={'fixture_revision':'n01-candidate-1','format':[1,0],'author':'independent h5py + public zfpy',
 'index_dtype_offsets':[0,8,16,24,32,40,41,45],'positive':[],'negative':[]}

def sha(b): 
    """Return the 32-byte SHA-256 of borrowed bytes-like b."""
    # Hash exact fixture bytes without normalization.
    return hashlib.sha256(b).digest()
def ds(g,n,x,t): 
    """Create dataset n beneath HDF5 group g from array-like x with dtype t.

    Returns the h5py dataset; track_times=False keeps fixture metadata deterministic."""
    # Convert values to the exact requested NumPy dtype and create the dataset.
    return g.create_dataset(n,data=np.asarray(x,dtype=t),track_times=False)
def text(g,n,s): 
    """Store string s as exact UTF-8 bytes in dataset n under g; return the dataset."""
    # Encode UTF-8 and delegate explicit byte-dtype creation.
    return ds(g,n,np.frombuffer(s.encode('utf-8'),dtype='u1'),'u1')
def strings(g,n,values,presence=None):
    """Create and return UTF-8 string-pool group n under g for values.

    Optional presence supplies a separate byte flag column; byte offsets include
    both initial zero and terminal extent. Strings are not normalized."""
    # Create the pool group, concatenate encoded strings and accumulate boundaries.
    # Write byte/offset datasets and optional presence flags.
    # Created pool/profile descriptor or selected artifact path.
    # Owned encoded payload bytes.
    # Initial/terminal UTF-8 or JSONL byte boundaries.
    p=g.create_group(n); data=bytearray(); offsets=[0]
    # Current authored string encoded without normalization.
    for v in values: data.extend(v.encode('utf-8')); offsets.append(len(data))
    ds(p,'bytes',np.frombuffer(data,dtype='u1'),'u1'); ds(p,'offsets',offsets,'<u8')
    if presence is not None: ds(p,'present',presence,'u1')
    return p

def document(g,n,value=None,state=0):
    """Create document group n under g using value and retention state.

    Retained states zero/three store exact bytes or compact JSON, media type and
    digest; inactive states store only state. Returns the created h5py group."""
    # Write the state and, when retained, exact document bytes/type/SHA-256.
    # Created pool/profile descriptor or selected artifact path.
    p=g.create_group(n); ds(p,'state',state,'u1')
    if state in (0,3):
        # Owned encoded payload bytes.
        data=value if isinstance(value,bytes) else json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()
        ds(p,'bytes',np.frombuffer(data,dtype='u1'),'u1'); text(p,'media_type','application/json'); ds(p,'sha256',list(sha(data)),'u1')
    return p

def run_info(g,run,C,K,kind):
    """Create and return run info under g from run fixture description.

    C is channel count, K committed-axis count and kind selects transient/static
    semantics. Availability controls canonical presence/zero fields and documents."""
    # Write string identities/status flags/counts and actual requested/retained bounds.
    # Use canonical absent values for unavailable catalogs.
    # Attach independent solver/sampling/execution documents.
    # Created HDF5 run-info group.
    # Whether the source run preparation left catalogs unavailable.
    inf=g.create_group('info'); unavailable=run.get('unavailable',False)
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'run_id':run['id'],'assembly_ref':'fixture_assembly' if not unavailable else '',
        'failure_stage':run.get('failure_stage',''),'failure_code':run.get('failure_code',''),
        'termination_reason':run.get('termination_reason',''),'elapsed_seconds_scope':'' if unavailable else 'solve_only','identity_axis_convention':('integration_step_zero_is_initial' if kind==0 else 'accepted_level_one_is_first') if not unavailable else '',
        'axis_name':'time' if kind==0 else 'proportional_load','axis_unit':'s' if kind==0 else '1'}.items():text(inf,name,value)
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'kind':kind,'run_outcome':run.get('outcome',0),'numerical_status':run.get('numerical',1),
        'observation_status':run.get('observation',2 if C==0 else 0),'logging_status':run.get('logging',0),
        'catalog_state':2 if unavailable else 0,'elapsed_seconds_present':0 if unavailable else 1,'last_accepted_present':0 if unavailable else 1,
        'last_recorded_present':int(K>0),'requested_bounds_present':0 if unavailable else 1,'identity_axis_present':0 if unavailable else 1}.items():ds(inf,name,value,'u1')
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'committed_count':K,'accepted_count':run.get('accepted_count',10 if kind==0 else 7)}.items():ds(inf,name,value,'<u8')
    # Actual retained time/load-axis values from the source description.
    axis=run.get('axis',[])
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'last_accepted_value':run.get('last_accepted',.1 if kind==0 else 1.),
        'last_recorded_value':float(axis[-1]) if K else 0.,'requested_begin':0.,'requested_end':.1 if kind==0 else 1.,'elapsed_seconds':.125}.items():
        ds(inf,name,0. if unavailable else value,'<f8')
    # Current authored property/case name, unchanged by normalization.
    # Current literal fixture field value.
    for name,value in {'solver_configuration':{'fixture':True},'sampling_configuration':{'fixture':True,'end_policy':'grid_only'} if kind==0 else {'capture':'fixture_levels'},'execution':{'timing_scope':'solve_only'}}.items():document(inf,name,value,state=2 if unavailable else 0)
    return inf

def catalogs(g,C,request_state):
    """Create channel/request catalogs under g for C channels and request_state.

    Uses at most two deterministic requests; writes effective JSON/digests only
    for included state. Returns None and owns created HDF5 objects through the file."""
    # Build deterministic channel-to-request mapping and identity/unit pools.
    # Write request metadata and optional exact effective JSON evidence.
    # Channel catalog descriptor/group under construction.
    # Request catalog descriptor/group under construction.
    # Request count, distinct from selected values or committed samples.
    ch=g.create_group('channels'); req=g.create_group('requests'); R=0 if C==0 else min(2,C)
    # Deterministic request group IDs, including Unicode text.
    groups=['pale_α','shaft'][:R]
    # Channel-to-request row mapping.
    # Channel ordinal used to construct deterministic mapping and units.
    which=[0 if c<(C+1)//2 else 1 for c in range(C)] if R==2 else [0]*C
    # Per-request authored identity strings.
    request_ids=['motion']*R
    ds(ch,'channel_index',list(range(C)),'<u8'); ds(ch,'request_index',which,'<u8')
    # Channel ordinal used to construct deterministic mapping and units.
    strings(ch,'scalar_id',[groups[which[c]]+'.motion.c'+str(c) for c in range(C)])
    # Current scalar/request mapping index.
    strings(ch,'group_id',[groups[k] for k in which]); strings(ch,'request_id',['motion']*C)
    # Channel ordinal used to construct deterministic mapping and units.
    strings(ch,'component_path',['c'+str(c) for c in range(C)]); strings(ch,'unit',[['m','m','rad','N','N*m'][c%5] for c in range(C)])
    strings(req,'group_id',groups); strings(req,'request_id',request_ids)
    strings(req,'schema_version',['fixture-schema-1']*R); strings(req,'construction_profile',['fixture-1']*R)
    ds(req,'effective_state',request_state,'u1')
    if request_state==0:
        # Compact effective-request JSON strings.
        # Current authored request group identity.
        js=[json.dumps({'fixture':True,'group_id':group,'request_id':'motion'},ensure_ascii=False,separators=(',',':')) for group in groups]
        # Current deterministic sample/artifact index.
        strings(req,'effective_json',js); ds(req,'effective_sha256',np.array([list(sha(j.encode())) for j in js],dtype='u1').reshape((R,32)),'u1')

def values(g,array,kind,sidecar):
    """Create encoded values under g for matrix array and run kind.

    sidecar is the output filename stem. Uses pinned reversible public zfpy,
    four-value frames and raw-on-tie choice; writes exact byte sidecars and returns
    per-frame encoding evidence. Tiny fixtures are deliberately materialized."""
    # Write logical shape and pinned reversible profile metadata.
    # Compress and independently decode every frame, selecting ZFP only when smaller.
    # Store canonical index/payload/verification datasets and independent exact-value sidecars.
    # Values descriptor/group selected for this operation.
    # Logical row count.
    # Logical inner matrix extent.
    v=g.create_group('values'); outer,inner=array.shape
    ds(v,'shape',[outer,inner],'<u8');text(v,'dtype','float64'); strings(v,'axis_names',['channel','sample'] if kind==0 else ['level','channel'])
    ds(v,'frame_schema',1,'<u4');ds(v,'nominal_frame_values',4,'<u4');ds(v,'row_profile_index',[0]*outer,'<u4')
    # Created pool/profile descriptor or selected artifact path.
    p=v.create_group('profiles')
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    # Explicit dtype or text entry used by the serialization step.
    for n,x,t in [('profile_index',[0],'<u4'),('mode',[0],'u1'),('codec_version',[5],'<u4'),('stream_word_bits',[64],'<u4'),('rounding',[0],'i1'),('mode_code',[2176],'<u8'),('tolerance_present',[0],'u1'),('requested_tolerance',[0.],'<f8'),('effective_tolerance',[0.],'<f8')]:ds(p,n,x,t)
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    for n,x in {'codec':'zfp','library_version':'1.0.1','library_revision':'f40868a6a1c190c802e7d8b5987064f044bf7812','byte_order':'little','execution':'serial'}.items():strings(p,n,[x])
    # Owned canonical frame records before packing.
    # Owned concatenated selected frame bytes.
    # Owned per-frame compression-choice evidence.
    records=[];packed=bytearray(); modes=[]
    # Logical matrix row; original value order is preserved.
    for row in range(outer):
        # First value index of this at-most-four-value frame.
        for begin in range(0,inner,4):
            # Contiguous little-endian binary64 input frame.
            # Owned exact bytes before publication or a deliberate mutation.
            # Public zfpy output including its complete header.
            source=np.ascontiguousarray(array[row,begin:begin+4],dtype='<f8'); raw=source.tobytes(); encoded=bytes(zfpy.compress_numpy(source,write_header=True))
            # Public-zfpy decode used to prove exact source-bit roundtrip.
            independent=zfpy.decompress_numpy(encoded)
            assert independent.tobytes()==raw
            # Raw/ZFP selector: compressed only when strictly smaller than raw.
            # Selected raw/ZFP frame bytes or their owned concatenation.
            encoding=1 if len(encoded)<len(raw) else 0; payload=encoded if encoding else raw
            records.append((row,begin,len(source),len(packed),len(payload),encoding,0,np.frombuffer(sha(payload),dtype='u1')))
            modes.append({'outer':row,'begin':begin,'count':len(source),'raw_bytes':len(raw),'zfp_bytes':len(encoded),'actual_encoding':encoding,'raw_tie':len(encoded)==len(raw)})
            packed.extend(payload)
    # New section index or owned canonical compound frame-index copy.
    idx=np.array(records,dtype=INDEX)
    ds(v,'frame_index',idx,INDEX);ds(v,'payload',np.frombuffer(packed,dtype='u1'),'u1');ds(v,'index_sha256',list(sha(idx.tobytes())),'u1')
    # Verification descriptor/group whose support is independently authored or repaired.
    verify=v.create_group('verification')
    # Current column name or bounded scalar extent selected by this activity.
    # Current literal column values or exact bits from this fixture table.
    # Explicit dtype or text entry used by the serialization step.
    # Current borrowed run or decoded frame descriptor.
    for n,x,t in [('max_abs_error',[0.]*len(idx),'<f8'),('rms_error',[0.]*len(idx),'<f8'),('verified_count',[int(r['count']) for r in idx],'<u8'),('bitwise_equal',[1]*len(idx),'u1')]:ds(verify,n,x,t)
    # Small independent byte fixtures for a C++ inspector, not a production transport.
    (OUT/(sidecar+'.index.bin')).write_bytes(idx.tobytes());(OUT/(sidecar+'.payload.bin')).write_bytes(packed)
    (OUT/(sidecar+'.expected.bin')).write_bytes(array.astype('<f8').tobytes())
    (OUT/(sidecar+'.shape.txt')).write_text(f'{outer} {inner}\n',encoding='ascii')
    return modes

def make(name,runs,optional=0,request_state=0,partial=False):
    """Write named HDF5 fixture for ordered runs and append positive manifest entry.

    optional sets input/log/context state; request_state controls effective JSON.
    partial retains an incomplete log fragment. Writes expected JSON and sidecars
    beneath OUT, replacing named artifacts only when this explicit author runs."""
    # Create fixed-layout format/optional evidence groups and ordered run index.
    # Create each unavailable or prepared run, catalogs, axes and frame storage.
    # Write owned expectations and register the positive case in MANIFEST.
    # Named artifact output path.
    # Owned expected run/value/byte oracle.
    path=OUT/(name+'.serene.h5'); expected=[]
    # Writable HDF5 file owned by the active context manager.
    with h5py.File(path,'w',libver=('v110','v110')) as f:
        # Created format-metadata group.
        fm=f.create_group('format')
        # Current column name or bounded scalar extent selected by this activity.
        # Current literal column values or exact bits from this fixture table.
        for n,x in {'name':'serene.outputs','file_id':'00000000-0000-4000-8000-000000000001','created_utc':'2026-11-08T00:00:00Z','writer_name':'independent-n01-fixture','writer_version':'0.1','write_state':'complete'}.items():text(fm,n,x)
        ds(fm,'version_major',1,'<u4');ds(fm,'version_minor',0,'<u4')
        strings(fm,'required_features',['serene-layout-1','utf8-pools-1','frames-77-le-1','zfp-5-le64-serial-1'])
        # Aggregate optional-section state group.
        sect=fm.create_group('sections')
        # Current column name or bounded scalar extent selected by this activity.
        for n in ['inputs','logs','context']:ds(sect,n,3 if partial and n=='logs' else optional,'u1')
        if optional in (0,3):
            # Created original/effective/override/resource document branch.
            ins=f.create_group('inputs');document(ins,'source_json',b'{ "fixture": true, "note": "exact source bytes" }\n')
            document(ins,'effective_json',{'fixture':True,'defaulted':True});document(ins,'overrides_json',{'argv':[]});document(ins,'resources_json',{'resources':[]})
            # Created execution-context branch.
            ctx=f.create_group('context');document(ctx,'execution_json',{'argv':['fixture author','--label','blade α'],'platform':'Windows fixture evidence','environment':{}})
            # Created retained JSONL log branch.
            # Owned exact bytes before publication or a deliberate mutation.
            lg=f.create_group('logs');raw=b'{"event":"begin","scope":"fixture"}\n{"event":"end","scope":"fixture"}\r\n'
            # Initial/terminal UTF-8 or JSONL byte boundaries.
            # Partial JSONL suffix excluded from complete-record boundaries.
            offsets=[0,raw.index(b'\n')+1,len(raw)];fragment=b'{"event":"partial' if partial else b'';raw+=fragment
            ds(lg,'records_jsonl',np.frombuffer(raw,dtype='u1'),'u1');ds(lg,'sha256',list(sha(raw)),'u1');ds(lg,'record_offsets',offsets,'<u8')
            ds(lg,'complete_record_count',2,'<u8');ds(lg,'trailing_fragment_bytes',len(fragment),'<u8');ds(lg,'dropped_count_present',0,'u1');ds(lg,'dropped_count',0,'<u8');text(lg,'schema','fixture-jsonl-1')
            document(lg,'info_json',{'association':'all indexed fixture runs','scope':'fixture-author'})
        # Created results branch containing index and kind-specific run groups.
        # Created ordered run-index group.
        results=f.create_group('results');catalog=results.create_group('index'); results.create_group('timeseries');results.create_group('steady_state')
        # Current borrowed run or decoded frame descriptor.
        ds(catalog,'run_index',list(range(len(runs))),'<u8');strings(catalog,'run_id',[r['id'] for r in runs]);strings(catalog,'run_name',[r.get('name','') for r in runs],[int('name'in r) for r in runs]);ds(catalog,'kind',[r['kind'] for r in runs],'u1')
        # Canonical group paths for the ordered runs.
        # Current dense row/frame/section ordinal in this activity.
        # Current borrowed run or decoded frame descriptor.
        paths=[f"/results/{'timeseries' if r['kind']==0 else 'steady_state'}/r{i:06d}" for i,r in enumerate(runs)];strings(catalog,'group_path',paths)
        # Current dense row/frame/section ordinal in this activity.
        # Current borrowed run or decoded frame descriptor.
        # Current run group path.
        for i,(r,rp) in enumerate(zip(runs,paths)):
            # Wire kind selecting the current run or section interpretation.
            # Private NumPy matrix/array copy or selected byte-range boundary.
            # Scalar-channel count implied by run kind and matrix orientation.
            # Committed sample/level count implied by matrix orientation.
            kind=r['kind'];a=np.asarray(r['array'],dtype='<f8');C=a.shape[0] if kind==0 else a.shape[1];K=a.shape[1] if kind==0 else a.shape[0]
            # Created or selected HDF5 group used by this activity.
            g=f.create_group(rp);run_info(g,r,C,K,kind)
            if r.get('unavailable'):expected.append({'run_id':r['id'],'kind':kind,'catalog_state':'unavailable'});continue
            catalogs(g,C,request_state);ds(g,'time' if kind==0 else 'load_levels',r['axis'],'<f8')
            # Exact retained integration-step or accepted-level identities.
            ids=r.get('ids',list(range(K))) if kind==0 else r.get('ids',list(range(1,K+1)))
            ds(g,'step_index' if kind==0 else 'level_index',ids,'<u8')
            # Deterministic per-run sidecar stem.
            # Owned per-frame compression-choice evidence.
            prefix=f'{name}.r{i:06d}';modes=values(g,a,kind,prefix)
            # Exact binary64 bits rendered into the independent JSON oracle.
            # Logical matrix row; original value order is preserved.
            expected.append({'run_id':r['id'],'kind':kind,'shape':list(a.shape),'axis':r['axis'],'identity_axis':ids,'bits_hex':[[f'{int(b):016x}' for b in row] for row in a.view('<u8')],'sidecar':prefix,'frames':modes,'run_outcome':r.get('outcome',0),'numerical_status':r.get('numerical',1)})
    # Expected-JSON artifact path or filename.
    ep=OUT/(name+'.expected.json');ep.write_text(json.dumps({'file':path.name,'runs':expected},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    MANIFEST['positive'].append({'file':path.name,'expected':ep.name,'description':name.replace('_',' ')})

def reset_index_digest(v):
    """Repair frame-index SHA-256 dataset in values group v; return None."""
    # Hash the current exact compound-record bytes and replace only the digest.
    v['index_sha256'][...]=np.frombuffer(sha(v['frame_index'][...].tobytes()),dtype='u1')
def replace_dataset(g,n,x,dtype):
    """Delete dataset n under g and recreate it from x with dtype; return None.

    Used for isolated fixture mutations requiring changed extent/type."""
    # Remove the old dataset and delegate exact typed recreation.
    del g[n];ds(g,n,x,dtype)
def replace_strings(g,n,items):
    """Replace string-pool child n under g with items; return the new group."""
    # Remove the old pool and rebuild exact byte/offset support.
    del g[n];return strings(g,n,items)
def negative(name,reason,mutator,base='mixed_timeseries'):
    """Copy base fixture to name, apply mutator and register expected reason.

    mutator receives the writable h5py file; artifacts are written beneath OUT.
    base defaults to the mixed transient case. Returns None."""
    # Copy pristine positive bytes, mutate the private file, and append negative metadata.
    # Named artifact output path.
    path=OUT/(name+'.serene.h5');shutil.copyfile(OUT/(base+'.serene.h5'),path)
    # Writable HDF5 file owned by the active context manager.
    with h5py.File(path,'r+') as f:mutator(f)
    MANIFEST['negative'].append({'file':path.name,'base':base+'.serene.h5','expected_error':reason,'scope':'one primary invariant; supporting digests/extents repaired where needed'})

# Explicit authoring activity: importing/running this historical-layout author creates/replaces fixture files.
# The documentation pass and ordinary fixture checks never execute this generator.
OUT.mkdir(exist_ok=True)
# Private NumPy matrix/array copy or selected byte-range boundary.
a=np.zeros((3,10),dtype='<f8')
a[1]=np.array([0x8000000000000000,1,0x8000000000000001,0x0010000000000000,0x3ff0000000000000,0x4000000000000000,0x3fd5555555555555,0xbff4000000000000,0x3ff0000000000001,0x400921fb54442d18],dtype='<u8').view('<f8')
a[2]=np.arange(10,dtype='<f8')/8
# Pristine transient run description with asymmetric exact value bits.
# Current deterministic sample/artifact index.
base={'id':'run/α','name':'same display name','kind':0,'array':a,'axis':[j*.01 for j in range(10)],'ids':list(range(10))}
# Private byte storage or selected byte-range boundary.
b=np.array([[0.,-0.,np.nextafter(0.,1.),1.,2.],[2.5,-3.,0.,4.,5.]],dtype='<f8')
# Static multi-level run description with sparse accepted-level IDs.
static={'id':'static β','name':'same display name','kind':1,'array':b,'axis':[.5,1.],'ids':[3,7]}
# Positive authoring: preserve exact signed-zero/subnormal/asymmetric values across transient, static and bundled layouts.
make('mixed_timeseries',[base]);make('static_levels',[static]);make('mixed_bundle',[base,static])
make('empty_selection',[{**base,'array':np.empty((0,0)),'axis':[],'ids':[]}])
make('failed_prefix',[{**base,'array':a[:,:2],'axis':[0.,.04],'ids':[0,4],'outcome':1,'numerical':3,'failure_stage':'integration','failure_code':'fixture_failure','termination_reason':'solver_failure','accepted_count':6,'last_accepted':.06}])
make('no_committed_samples',[{**base,'array':np.empty((3,0)),'axis':[],'ids':[],'outcome':2,'numerical':2,'observation':1,'failure_stage':'observation','failure_code':'fixture_failure','termination_reason':'observation_failure','accepted_count':0,'last_accepted':0.}])
make('unavailable_catalog',[{**base,'array':np.empty((0,0)),'axis':[],'ids':[],'unavailable':True,'outcome':4,'numerical':0,'observation':3,'logging':4,'accepted_count':0,'failure_stage':'preparation','failure_code':'fixture_failure','termination_reason':'preparation_failure'}],optional=2,request_state=2)
make('optional_disabled',[base],optional=1,request_state=1);make('optional_unavailable',[base],optional=2,request_state=2);make('partial_logs',[{**base,'logging':1,'outcome':3,'termination_reason':'logging_failure','failure_stage':'logging','failure_code':'partial_capture'}],partial=True)
make('failed_static',[{**static,'array':np.empty((0,5)),'axis':[],'ids':[],'outcome':1,'numerical':3,'failure_stage':'equilibrium','failure_code':'fixture_failure','termination_reason':'solver_failure','accepted_count':0,'last_accepted':0.}])
# First transient values-group path used by targeted mutators.
# First transient channel-group path used by targeted mutators.
# First transient run-group path used by targeted mutators.
V='/results/timeseries/r000000/values';CH='/results/timeseries/r000000/channels';RUN='/results/timeseries/r000000'
# Negative envelope/storage cases mutate one field or HDF5 object role while retaining the positive baseline.
negative('bad_major','unsupported_feature',lambda f:f['format/version_major'].__setitem__((),2))
negative('unknown_feature','unsupported_feature',lambda f:replace_strings(f['format'],'required_features',['serene-layout-1','utf8-pools-1','frames-77-le-1','zfp-5-le64-serial-1','unknown-required']))
negative('wrong_time_dtype','invalid_contract',lambda f:replace_dataset(f[RUN],'time',f[RUN+'/time'][...],'<f4'))
def bad_object(f):
    """Replace time dataset in writable f with a group, preserving the path."""
    # Delete the numeric object and create the wrong HDF5 object kind.
    del f[RUN+'/time'];f[RUN].create_group('time')
negative('wrong_object_kind','invalid_contract',bad_object)
def soft(f):
    """Replace time in writable f with an internal soft link to a copied dataset."""
    # Retain identical numeric bytes elsewhere, then change only link ownership semantics.
    f.copy(f[RUN+'/time'],f[RUN],name='unreferenced_time');del f[RUN+'/time'];f[RUN]['time']=h5py.SoftLink(RUN+'/unreferenced_time')
negative('soft_link','invalid_contract',soft)
def filtered(f):
    """Replace f payload dataset with an equivalent gzip-filtered dataset."""
    # Copy payload bytes and recreate storage with the unsupported filter.
    # Current literal column values or exact bits from this fixture table.
    x=f[V+'/payload'][...];del f[V+'/payload'];f[V].create_dataset('payload',data=x,compression='gzip')
negative('filtered_payload','unsupported_feature',filtered)
# Pool mutations isolate UTF-8, initial/terminal offsets and identity uniqueness.
negative('bad_utf8','invalid_contract',lambda f:f[CH+'/scalar_id/bytes'].__setitem__(0,255))
negative('offset_not_zero','invalid_contract',lambda f:f[CH+'/scalar_id/offsets'].__setitem__(0,1))
negative('offset_terminal','invalid_contract',lambda f:f[CH+'/scalar_id/offsets'].__setitem__(-1,int(f[CH+'/scalar_id/offsets'][-1])-1))
def duplicate(f):
    """Duplicate one scalar ID in writable f while rebuilding valid pool support."""
    # Decode current IDs, repeat the first identity, and replace the pool.
    # Created or selected HDF5 group used by this activity.
    # Private byte storage or selected byte-range boundary.
    # Current UTF-8 pool offset array.
    # Owned decoded scalar IDs before duplicating one identity.
    # Current dense row/frame/section ordinal in this activity.
    g=f[CH+'/scalar_id'];b=bytes(g['bytes'][...]);o=g['offsets'][...];s=[b[int(o[i]):int(o[i+1])].decode() for i in range(len(o)-1)];s[1]=s[0];replace_strings(f[CH],'scalar_id',s)
negative('duplicate_scalar_id','invalid_contract',duplicate)
def field(f,name,index,value):
    """Set frame field name at index to value in writable f and repair index digest."""
    # Copy compound records, change one field, store records and repair enclosing evidence.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    v=f[V];idx=v['frame_index'][...];idx[name][index]=value;v['frame_index'][...]=idx;reset_index_digest(v)
# Frame mutations repair enclosing index digests so the chosen reference/coverage invariant remains primary.
negative('bad_profile_index','invalid_contract',lambda f:field(f,'profile_index',0,9))
negative('coverage_overlap','invalid_contract',lambda f:field(f,'inner_begin',1,3))
negative('zero_frame_count','invalid_contract',lambda f:field(f,'count',0,0))
def bad_payload_digest(f):
    """Corrupt one frame payload digest in f while repairing the index digest."""
    # Flip the selected payload-hash byte and rehash the containing index.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    v=f[V];idx=v['frame_index'][...];idx['sha256'][0,0]^=1;v['frame_index'][...]=idx;reset_index_digest(v)
negative('bad_payload_digest','verification_failure',bad_payload_digest)
negative('bad_index_digest','verification_failure',lambda f:f[V+'/index_sha256'].__setitem__(0,int(f[V+'/index_sha256'][0])^1))
# Profile and payload cases separate unsupported metadata from header/count/raw-length failures.
negative('unsupported_profile','unsupported_profile',lambda f:f[V+'/profiles/mode'].__setitem__(0,99))
negative('dimension_limit','resource_limit',lambda f:f[V+'/shape'].__setitem__(1,2**40+1))
def header_count(f):
    """Replace f first frame with a public ZFP frame containing three values.

    Repairs payload size/hash and subsequent offsets while keeping indexed count."""
    # Compress a wrong-count zero frame and splice it into the payload.
    # Shift later byte offsets and repair payload/index support.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    # Replacement public-codec frame bytes.
    # Original first-frame byte extent.
    # Byte-size difference applied to later frame offsets.
    v=f[V];idx=v['frame_index'][...];payload=bytes(v['payload'][...]);new=bytes(zfpy.compress_numpy(np.zeros(3,dtype='<f8'),write_header=True));old_n=int(idx['byte_count'][0]);delta=len(new)-old_n
    # Selected raw/ZFP frame bytes or their owned concatenation.
    payload=new+payload[old_n:];idx['byte_count'][0]=len(new);idx['sha256'][0]=np.frombuffer(sha(new),dtype='u1')
    # Current dense row/frame/section ordinal in this activity.
    for i in range(1,len(idx)):idx['byte_offset'][i]=int(idx['byte_offset'][i])+delta
    replace_dataset(v,'payload',np.frombuffer(payload,dtype='u1'),'u1');v['frame_index'][...]=idx;reset_index_digest(v)
negative('header_count_mismatch','codec_failure',header_count)
def raw_length(f):
    """Append one byte to a raw frame in writable f, repairing hashes/offsets."""
    # Find a raw frame, insert one trailing zero, shift later offsets and repair support.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    # Current dense row/frame/section ordinal in this activity.
    # Current borrowed run or decoded frame descriptor.
    # Payload byte offset of the chosen raw frame.
    # Current column name or bounded scalar extent selected by this activity.
    # Created pool/profile descriptor or selected artifact path.
    v=f[V];idx=v['frame_index'][...];i=next(i for i,r in enumerate(idx) if r['encoding']==0);off=int(idx['byte_offset'][i]);n=int(idx['byte_count'][i]);p=bytes(v['payload'][...]);p=p[:off+n]+b'\0'+p[off+n:];idx['byte_count'][i]=n+1;idx['sha256'][i]=np.frombuffer(sha(p[off:off+n+1]),dtype='u1')
    # Current deterministic sample/artifact index.
    for j in range(i+1,len(idx)):idx['byte_offset'][j]=int(idx['byte_offset'][j])+1
    replace_dataset(v,'payload',np.frombuffer(p,dtype='u1'),'u1');v['frame_index'][...]=idx;reset_index_digest(v)
negative('raw_length','invalid_contract',raw_length)
def string_limit(f):
    """Replace f scalar-ID pool with one entry exceeding the one-MiB limit."""
    # Rebuild the pool so only the individual string byte limit is intentionally exceeded.
    replace_strings(f[CH],'scalar_id',['a'*(1048576+1),'second','third'])
negative('string_limit','resource_limit',string_limit)

# Empty selections still validate row-profile identity; decoded finite-value rules apply to both raw and ZFP frames.
negative('empty_row_profile_index','invalid_contract',lambda f:f[V+'/row_profile_index'].__setitem__(0,99),base='no_committed_samples')
def nonfinite_raw(f):
    """Insert a quiet-NaN binary64 value into a raw frame in writable f."""
    # Find a raw frame, replace its first value bits and repair payload/index digests.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    # Current dense row/frame/section ordinal in this activity.
    # Current borrowed run or decoded frame descriptor.
    # Payload byte offset of the chosen raw frame.
    # Current column name or bounded scalar extent selected by this activity.
    # Created pool/profile descriptor or selected artifact path.
    v=f[V];idx=v['frame_index'][...];i=next(i for i,r in enumerate(idx) if r['encoding']==0);off=int(idx['byte_offset'][i]);n=int(idx['byte_count'][i]);p=bytearray(v['payload'][...]);p[off:off+8]=struct.pack('<Q',0x7ff8000000000000);idx['sha256'][i]=np.frombuffer(sha(p[off:off+n]),dtype='u1');v['payload'][...]=np.frombuffer(p,dtype='u1');v['frame_index'][...]=idx;reset_index_digest(v)
negative('nonfinite_raw','invalid_contract',nonfinite_raw)
def nonfinite_zfp(f):
    """Replace f first encoded frame with public-ZFP encoded infinities."""
    # Encode four infinities, splice bytes, shift offsets and repair supporting hashes.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    # Replacement public-codec frame bytes.
    # Original first-frame byte extent.
    # Byte-size difference applied to later frame offsets.
    v=f[V];idx=v['frame_index'][...];payload=bytes(v['payload'][...]);new=bytes(zfpy.compress_numpy(np.full(4,np.inf,dtype='<f8'),write_header=True));old_n=int(idx['byte_count'][0]);delta=len(new)-old_n
    # Selected raw/ZFP frame bytes or their owned concatenation.
    payload=new+payload[old_n:];idx['byte_count'][0]=len(new);idx['sha256'][0]=np.frombuffer(sha(new),dtype='u1')
    # Current dense row/frame/section ordinal in this activity.
    for i in range(1,len(idx)):idx['byte_offset'][i]=int(idx['byte_offset'][i])+delta
    replace_dataset(v,'payload',np.frombuffer(payload,dtype='u1'),'u1');v['frame_index'][...]=idx;reset_index_digest(v)
negative('nonfinite_zfp','invalid_contract',nonfinite_zfp)
def null_time(f):
    """Replace time in f with a null-dataspace f64 dataset."""
    # Retain the expected name/type while removing its ordinary rank-one dataspace.
    del f[RUN+'/time'];f[RUN].create_dataset('time',shape=None,dtype='<f8')
negative('null_dataspace','invalid_contract',null_time)
negative('outcome_inconsistent','invalid_contract',lambda f:f[RUN+'/info/numerical_status'].__setitem__((),0))
def request_json(f,value):
    """Replace f first effective request with exact string value and repaired hashes.

    Malformed JSON content is intentional; pool structure remains consistent."""
    # Decode the pool, replace one text and rebuild pool/digest support.
    # Request catalog descriptor/group under construction.
    # Created or selected HDF5 group used by this activity.
    # Private byte storage or selected byte-range boundary.
    # Current UTF-8 pool offset array.
    # Owned pool text values before replacement.
    # Current dense row/frame/section ordinal in this activity.
    # Current decoded string/value item.
    req=f[RUN+'/requests'];g=req['effective_json'];b=bytes(g['bytes'][...]);o=g['offsets'][...];texts=[b[int(o[i]):int(o[i+1])].decode() for i in range(len(o)-1)];texts[0]=value;replace_strings(req,'effective_json',texts);req['effective_sha256'][...]=np.array([list(sha(s.encode())) for s in texts],dtype='u1')
# Effective request bytes stay exact while strict JSON rejection is exercised independently of their repaired digest.
negative('request_json_nan','invalid_contract',lambda f:request_json(f,'{"x":NaN}'))
negative('request_json_duplicate_key','invalid_contract',lambda f:request_json(f,'{"x":1,"x":2}'))


def long_expert(f):
    """Replace f first frame with an equivalent noncanonical long expert header.

    Public zfpy must still decode exact zeros; the strict profile should reject it."""
    # Repack equivalent reversible expert parameters and shift payload bits.
    # Prove public decoding, splice the new frame and repair offsets/index digest.
    # Values descriptor/group selected for this operation.
    # New section index or owned canonical compound frame-index copy.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    # Current column name or bounded scalar extent selected by this activity.
    # Original frame extent/bits or section identity before mutation.
    v=f[V];idx=v['frame_index'][...];payload=bytes(v['payload'][...]);n=int(idx['byte_count'][0]);old=int.from_bytes(payload[:n],'little')
    # Manually assembled equivalent long-expert ZFP mode bits.
    mode=15420
    # Manually assembled equivalent long-expert ZFP mode bits.
    mode=(mode<<7)+63;mode=(mode<<15)+16657;mode=(mode<<15)+0;mode=(mode<<12)+0xfff
    # Repacked bitstream integer with long expert mode and shifted payload bits.
    newint=(old&((1<<84)-1))|(mode<<84)|((old>>96)<<148)
    # Replacement public-codec frame bytes.
    new=newint.to_bytes(((n*8+52+63)//64)*8,'little')
    assert zfpy.decompress_numpy(new).tobytes()==np.zeros(4,dtype='<f8').tobytes()
    # Byte-size difference applied to later frame offsets.
    # Selected raw/ZFP frame bytes or their owned concatenation.
    delta=len(new)-n;payload=new+payload[n:];idx['byte_count'][0]=len(new);idx['sha256'][0]=np.frombuffer(sha(new),dtype='u1')
    # Current dense row/frame/section ordinal in this activity.
    for i in range(1,len(idx)):idx['byte_offset'][i]=int(idx['byte_offset'][i])+delta
    replace_dataset(v,'payload',np.frombuffer(payload,dtype='u1'),'u1');v['frame_index'][...]=idx;reset_index_digest(v)
negative('long_expert_header','unsupported_profile',long_expert)

# Expected binary fixtures and archive bytes are pinned, not silently regenerated by production tests.
# Authored artifact list included in the new manifest.
# Created pool/profile descriptor or selected artifact path.
files=[p for p in OUT.iterdir() if p.is_file() and p.name!='manifest.json']
# Created pool/profile descriptor or selected artifact path.
MANIFEST['files']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(files)}
(OUT/'manifest.json').write_text(json.dumps(MANIFEST,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'positive':len(MANIFEST['positive']),'negative':len(MANIFEST['negative']),'files':len(files),'public_zfpy_roundtrips':'all generated frames exact','scope':'fixture authoring, no production validation'}))
