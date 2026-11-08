"""Independent 1.1 draft corpus author. Never imports production Serene code."""
from pathlib import Path
import copy, hashlib, importlib.util, itertools, json, math, struct
from fractions import Fraction
import numpy as np
import zfpy

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'fixtures_extensions_draft'
BASE = ROOT / 'tools/conformance/custom_author.py'
spec = importlib.util.spec_from_file_location('independent_g0c', BASE)
g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
D, P, F = 'run-diagnostics-1', 'provenance-sources-1', 'timeseries-zfp-fixed-accuracy-1'
FEATURES = [D, P, F]
STATUSES=['success','invalid_configuration','unsupported','capacity_exceeded','not_prepared','numerical_failure','native_linear_failure','native_nonlinear_failure','native_exception','output_failure','logging_failure','quarantined','diagnostic_failure']
METRICS = ['force_residual_N', 'moment_residual_Nm', 'constraint_translation_m',
           'constraint_rotation_rad', 'increment_translation_m', 'increment_rotation_rad']
CRITERIA = {'norm':'block_max_abs_component','evaluation_time_s':-0.25,
 'force_absolute_N':0.01,'moment_absolute_Nm':0.01,'relative_tolerance':0.001,
 'force_scale_N':1.0,'moment_scale_Nm':1.0,'force_limit_N':0.011,'moment_limit_Nm':0.011,
 'constraint_translation_limit_m':0.001,'constraint_rotation_limit_rad':0.001,
 'increment_translation_limit_m':0.001,'increment_rotation_limit_rad':0.001}
MANIFEST = {'schema':1,'status':'unaccepted extension contract candidate',
 'base_format_sha256':hashlib.sha256((ROOT/'spec/FORMAT.md').read_bytes()).hexdigest(),
 'base_author_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),
 'base_fixture_manifest_sha256':hashlib.sha256((ROOT/'fixtures_custom/manifest.json').read_bytes()).hexdigest(),
 'positive':[], 'negative':[], 'producer_oracles':[], 'bundle_import':[]}

def add_meta(c, value):
    sid = c.add(1,b''); c.extra[sid] = value; return sid

def encode(c):
    c.seal()
    for sid,obj in c.extra.items(): c.sections[sid] = (1,g.js(obj))
    body=bytearray(); directory=bytearray(); offset=128
    for sid,(kind,data) in enumerate(c.sections):
        directory.extend(g.DIRECTORY.pack(sid,kind,0,offset,len(data),g.sha(data)))
        body.extend(data); offset += len(data)
    minor=c.root['format']['version_minor']
    h=g.HEADER96.pack(b'SERENE\0\0',1,minor,0x01020304,128,64,offset+len(directory),offset,len(c.sections),0,1,g.sha(directory))
    return h+g.sha(h)+body+directory

def base(name, features=(), unavailable=False, empty=False):
    a=np.array([[0.,0.,0.,0.,0.,0.,0.,0.],
                [1.125,-2.25,3.5,0.125,0.3333333333333333,4.,-0.,8.]],dtype='<f8')
    b=np.array([[0.,-0.,0.25,1.,2.],[2.5,-3.,0.,4.,5.]],dtype='<f8')
    transient={'id':'transient/α','name':'same name','kind':0,'array':a,'axis':[i*.01 for i in range(8)],
               'ids':list(range(8)),'accepted_count':7,'last_accepted':.07}
    static={'id':'static β','name':'same name','kind':1,'array':b,'axis':[.5,1.],
            'ids':[0,1],'accepted_count':1,'last_accepted':1.}
    if empty:
        transient.update(array=np.empty((2,0)),axis=[],ids=[],outcome=2,numerical=2,observation=1,
          accepted_count=0,last_accepted=0.,failure_stage='observation',failure_code='fixture_failure',termination_reason='observation_failure')
    if unavailable:
        transient.update(array=np.empty((0,0)),axis=[],ids=[],unavailable=True,outcome=4,numerical=0,
          observation=3,logging=4,accepted_count=0,failure_stage='preparation',failure_code='fixture_failure',termination_reason='preparation_failure')
    c=g.make(name,[transient,static]); c.extra={}; c.trace_ids={}; c.source_ids=[]
    c.root['format']['writer_name']='independent-extensions-draft'; c.root['format']['writer_version']='candidate-1'
    c.root['format']['version_minor']=int(bool(features)); c.root['format']['required_features']=g.FEATURES+list(features)
    c.runs[1]['info']['identity_axis_convention']='accepted_level_zero_is_initial'
    c.runs[0]['info']['requested_end']=0.07 if not unavailable else 0.0
    c.runs[1]['info']['requested_begin']=0.5
    if D in features: diagnostics(c,unavailable)
    if P in features: provenance(c)
    if F in features: fixed(c,math.ldexp(1.,-12))
    return c

def trace(c, run_index, kind, R=2, status=0, requirement=0):
    unavailable=status in (1,4)
    t={'trace_schema':1,'kind':kind,'run_index':run_index,'requirement':requirement,
       'availability':2 if unavailable else (0 if status==0 else 3),'capture_status':status,
       'record_capacity':max(2,R),'record_count':0 if unavailable else R,
       'eligible_count_present':1,'eligible_count':0 if unavailable else R+int(status!=0),
       'omitted_count_present':1,'omitted_count':int(not unavailable and status!=0),
       'capture_error_count':int(status==3),'first_missing_ordinal_present':int(status in (2,3)),
       'first_missing_ordinal':R if status in (2,3) else 0,
       'failure_code':'' if status==0 else 'fixture_'+['complete','not_started','capacity','capture_error','source_unavailable'][status]}
    if not unavailable:
        cols={'record_index':c.arr('u64',list(range(R)))}
        def col(name,ty,vals): cols[name]=c.arr(ty,vals)
        if kind==0:
            col('accepted_step','u64',list(range(1,R+1))); col('time_s','f64',[.01*(i+1) for i in range(R)])
            col('nonlinear_iterations','u64',[2+i for i in range(R)])
        elif kind in (1,2,3):
            col('attempt_id','u64',list(range(1,R+1))); col('factor','f64',[.5+i*.5 for i in range(R)])
            if kind==2: col('accepted_level_id','u64',list(range(R)))
            else:
                col('certified','u8',[1]*R); col('rolled_back','u8',[0]*R)
                cols['status_names']=c.pool(STATUSES); col('status_index','u32',[0]*R)
                if kind==1:
                    col('accepted_level_present','u8',[1]*R); col('accepted_level_id','u64',list(range(R)))
                    col('newton_record_count','u64',[1]*R)
                else:
                    col('iteration_id','u64',[1]*R); col('row_kind','u8',[0]*R)
            col('metric_valid','u8',[63]*R)
            for name in METRICS: col(name,'f64',[0.]*R)
            t['criteria']=copy.deepcopy(CRITERIA)
        else:
            col('sequence','u64',list(range(R))); cols['event_names']=c.pool(['core.execution_started','core.execution_completed','core.execution_failed'])
            col('event_name_index','u32',([0,1] if R==2 else [0]*R)); col('severity','u8',[1]*R)
            col('identity_valid','u8',([3,3] if run_index==0 else [0,12]) if R==2 else [0]*R)
            info=c.runs[run_index]['info']
            for name,ty,end in [('time_s','f64',info['last_accepted_value'] if run_index==0 else 0.),('accepted_step','u64',info['accepted_count'] if run_index==0 else 0),('accepted_level','u64',info['accepted_count'] if run_index==1 else 0),('attempt_id','u64',2 if run_index==1 else 0)]: col(name,ty,[0,end] if R==2 else [0]*R)
        t['columns']=cols
    sid=add_meta(c,t); c.trace_ids[run_index,kind]=sid; return {'kind':kind,'metadata_section':sid}

def diagnostics(c,unavailable=False):
    for i,run in enumerate(c.runs):
        kinds=[0,4] if i==0 else [1,2,3,4]
        entries=[trace(c,i,k,R=(7 if k==0 else 2),status=1 if unavailable and i==0 else 0) for k in kinds]
        run['diagnostics']={'status':4 if unavailable and i==0 else 0,'traces':entries}

def transient_prefix(c,count,accepted,target=0.07):
    run=c.runs[0]; ex=c.expected[0]; bits=[row[:count] for row in ex['bits_hex']]
    array=np.array([[int(x,16) for x in row] for row in bits],dtype='<u8').view('<f8')
    run['values'],frames=g.values(c,array,0,ex['sidecar'])
    axis=[i*.01 for i in range(count)]; ids=list(range(count))
    c.replace(run['time'],axis,shape=[count]); c.replace(run['step_index'],ids,shape=[count])
    run['info'].update(committed_count=count,accepted_count=accepted,last_accepted_value=accepted*.01,
      last_recorded_present=int(count>0),last_recorded_value=axis[-1] if count else 0.,requested_end=target)
    ex.update(shape=[2,count],axis=axis,identity_axis=ids,bits_hex=bits,frames=frames)
    c.sidecars[ex['sidecar']+'.axis.bin']=np.array(axis,dtype='<f8').tobytes()
    c.sidecars[ex['sidecar']+'.identity.bin']=np.array(ids,dtype='<u8').tobytes()
    if (0,4) in c.trace_ids:
        setcol(c,0,4,'time_s',[0.,accepted*.01]); setcol(c,0,4,'accepted_step',[0,accepted])

def provenance(c):
    old=[c.root['inputs'][key] for key in ['source_json','effective_json','overrides_json','resources_json']]
    old.append(c.root['context']['execution_json'])
    digest=g.sha(c.data(old[0]['bytes']))
    for i,doc in enumerate(old):
        c.source_ids.append(add_meta(c,{'source_schema':1,'source_index':i,'kind':0,'document':doc}))
    log=dict(c.root['logs']); log.update(source_schema=1,source_index=5,kind=1,state=0)
    c.source_ids.append(add_meta(c,log))
    for name in ['inputs','context','logs']: del c.root[name]
    c.root['provenance']={'schema':1,'authored_input_sha256':c.arr('u8',digest),'original_source_index':0,
      'sources':{'source_index':c.arr('u64',list(range(6))),'kind':c.arr('u8',[0]*5+[1]),'metadata_section':c.arr('u64',c.source_ids)}}
    for run in c.runs:
        run['provenance']={'references':{'role':c.arr('u8',list(range(6))),
          'source_index':c.arr('u64',list(range(6))),'order':c.arr('u64',[0]*6)}}

def original_state(c,state):
    # Remove the original data section through graph compaction so this is a positive retention case.
    c.extra[c.source_ids[0]]['document']={'state':state}
    c.root['format']['sections']['inputs']=3
    compact(c)

def compact(c):
    # All references are schema-known and authored here. Do not use this on negative alias/orphan cases.
    c.seal()
    for sid,obj in c.extra.items(): c.sections[sid]=(1,g.js(obj))
    keep={0,*c.run_ids,*c.extra.keys()}
    def scan(x):
        if isinstance(x,dict):
            if 'section' in x: keep.add(x['section'])
            for val in x.values(): scan(val)
        elif isinstance(x,list):
            for val in x: scan(val)
    for x in [c.root,*c.runs,*c.extra.values()]: scan(x)
    mapping={old:i for i,old in enumerate(sorted(keep))}
    def fix(x):
        if isinstance(x,dict):
            if 'section' in x: x['section']=mapping[x['section']]
            if 'metadata_section' in x and isinstance(x['metadata_section'],int): x['metadata_section']=mapping[x['metadata_section']]
            for val in x.values(): fix(val)
        elif isinstance(x,list):
            for val in x: fix(val)
    oldsections=c.sections; c.sections=[oldsections[i] for i in sorted(keep)]
    for x in [c.root,*c.runs,*c.extra.values()]: fix(x)
    c.run_ids=[mapping[i] for i in c.run_ids]; c.source_ids=[mapping[i] for i in c.source_ids]
    c.extra={mapping[i]:v for i,v in c.extra.items()}; c.trace_ids={k:mapping[v] for k,v in c.trace_ids.items()}
    c.replace(c.root['results']['index']['run_metadata_section'],c.run_ids)
    for expected,sid in zip(c.expected,c.run_ids): expected['metadata_section']=sid
    if 'provenance' in c.root: c.replace(c.root['provenance']['sources']['metadata_section'],c.source_ids)

def only_first_run(c):
    assert not c.extra
    c.runs=c.runs[:1]; c.run_ids=c.run_ids[:1]; c.expected=c.expected[:1]
    index=c.root['results']['index']; c.replace(index['run_index'],[0],shape=[1]); c.replace(index['kind'],[0],shape=[1])
    c.replace_pool(index['run_id'],[c.runs[0]['info']['run_id']]); c.replace_pool(index['run_name'],['same name'])
    c.replace(index['run_name']['present'],[1],shape=[1]); c.replace(index['run_metadata_section'],c.run_ids,shape=[1])
    c.sidecars={name:data for name,data in c.sidecars.items() if '.r000000.' in name}; compact(c)

def fixed(c,tau,raw_only=False):
    run=c.runs[0]
    if 'values' not in run: return
    v=run['values']; p=v['profiles']; e=math.frexp(tau)[1]-1
    for key,value in [('mode',1),('tolerance_present',1),('requested_tolerance',tau),('effective_tolerance',math.ldexp(1.,e)),('mode_code',2177+e+1074)]: c.replace(p[key],[value])
    expected=c.expected[0]; source=np.array([[int(x,16) for x in row] for row in expected['bits_hex']],dtype='<u8').view('<f8')
    if not source.size: source=np.empty(v['shape'],dtype='<f8')
    records=[]; payload=bytearray(); maxima=[]; rms=[]; equal=[]; decoded=[]
    for row in range(v['shape'][0]):
        for begin in range(0,v['shape'][1],4):
            a=source[row,begin:begin+4].copy(); compressed=bytes(zfpy.compress_numpy(a,tolerance=tau,write_header=True))
            mode=(int.from_bytes(compressed[:12],'little')>>84)&4095
            assert mode==2177+e+1074,(e,mode)
            b=zfpy.decompress_numpy(compressed)
            errors=[abs(Fraction(float(x))-Fraction(float(y))) for x,y in zip(a,b)]
            assert all(x<=Fraction(tau) for x in errors)
            raw=a.tobytes(); use=int(not raw_only and len(compressed)<len(raw)); actual=compressed if use else raw
            if not use: b=a; errors=[Fraction(0)]*len(a)
            maximum=max(errors,default=Fraction(0)); upper=float(maximum)
            if Fraction(upper)<maximum: upper=math.nextafter(upper,math.inf)
            maxima.append(upper); rms.append(0. if not upper else upper*math.sqrt(sum(float(x/Fraction(upper))**2 for x in errors)/len(errors)))
            equal.append(int(a.tobytes()==b.tobytes())); decoded.extend(int(x) for x in b.view('<u8'))
            records.append((row,begin,len(a),len(payload),len(actual),use,0,g.sha(actual))); payload.extend(actual)
    index=b''.join(g.FRAME.pack(*r) for r in records)
    c.replace(v['frame_index'],index,shape=[len(records)]); c.replace(v['payload'],payload,shape=[len(payload)])
    for key,data in [('max_abs_error',maxima),('rms_error',rms),('verified_count',[r[2] for r in records]),('bitwise_equal',equal)]: c.replace(v['verification'][key],data,shape=[len(records)])
    expected['fixed_accuracy']={'requested_tolerance_hex':tau.hex(),'effective_exponent':e,'decoded_bits_hex':[f'{x:016x}' for x in decoded]}
    prefix=expected['sidecar']; c.sidecars[prefix+'.index.bin']=index; c.sidecars[prefix+'.payload.bin']=bytes(payload)

def emit(name,c,error=None,phase=None,reason=None,base_name=None):
    blob=encode(c); filename=name+'.serene'; (OUT/filename).write_bytes(blob)
    item={'file':filename,'features':c.root['format']['required_features'][5:]}
    if error:
        item.update(expected_error=error,detection_phase=phase,reason=reason,base=base_name)
        MANIFEST['negative'].append(item)
    else:
        for path,data in c.sidecars.items(): (OUT/path).write_bytes(data)
        expectation={'file':filename,'runs':c.expected,'features':item['features'],'trace_metadata_sections':list(c.trace_ids.values()),'source_metadata_sections':c.source_ids}
        exp=name+'.expected.json'; (OUT/exp).write_text(json.dumps(expectation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        item['expected']=exp; MANIFEST['positive'].append(item)

def negative(name,c,mutator,error='invalid_contract',phase='select_run',reason=''):
    d=copy.deepcopy(c); mutator(d); emit(name,d,error,phase,reason)

def setcol(c,run,kind,column,vals): c.replace(c.extra[c.trace_ids[run,kind]]['columns'][column],vals)
def traceobj(c,run=1,kind=1): return c.extra[c.trace_ids[run,kind]]

def main():
    OUT.mkdir(exist_ok=True)
    bases={}
    for n in range(1,4):
        for features in itertools.combinations(FEATURES,n):
            name='features_'+''.join('dpf'[FEATURES.index(f)] for f in features)
            c=base(name,features); emit(name,c); bases[features]=c
    baseline=base('unchanged_1_0'); emit('unchanged_1_0',baseline)
    partial=base('v10_partial_original'); doc=partial.root['inputs']['source_json']; doc['state']=3
    fragment=b'{"original_input_incomplete":'; partial.replace(doc['bytes'],fragment,shape=[len(fragment)])
    partial.root['format']['sections']['inputs']=3; only_first_run(partial); emit('v10_partial_original',partial)
    MANIFEST['bundle_import'].append({'spool':'v10_partial_original.serene','without_binding':'unsupported_merge_source','with_binding':'unsupported_merge_source','reason':'Valid 1.0 partial original evidence is outside initial bundle-import capability; never malformed.'})
    disabled=base('v10_original_disabled')
    for name in ['inputs','logs','context']:
        del disabled.root[name]; disabled.root['format']['sections'][name]=1
    only_first_run(disabled); emit('v10_original_disabled',disabled)
    diag=bases[(D,)]; prov=bases[(P,)]; lossy=bases[(F,)]; allthree=bases[tuple(FEATURES)]
    for name,c in [('unavailable_catalog_diagnostics',base('unavailable_catalog_diagnostics',[D],unavailable=True)),
                   ('fixed_empty_rows',base('fixed_empty_rows',[F],empty=True))]: emit(name,c)
    raw=base('fixed_all_raw',[F]); fixed(raw,math.ldexp(1.,-12),True); emit('fixed_all_raw',raw)
    for e in [-1073,843]:
        name='fixed_exponent_'+str(e).replace('-','minus_'); c=base(name,[F]); fixed(c,math.ldexp(1.,e)); emit(name,c)
    c=base('fixed_nonpower_upper',[F]); fixed(c,math.nextafter(math.ldexp(1.,844),0.)); emit('fixed_nonpower_upper',c)
    for state in [1,2]:
        name='original_'+('disabled' if state==1 else 'unavailable'); c=base(name,[P]); original_state(c,state); emit(name,c)
    for state,label in [(0,'empty_complete'),(2,'optional_capacity'),(3,'required_capture_failure')]:
        name='diagnostics_'+label; c=base(name,[D]); old=c.trace_ids[0,0]
        replacement=trace(c,0,0,R=0 if state==0 else 2,status=state,requirement=int(state==3))
        c.runs[0]['diagnostics']['traces'][0]=replacement; del c.extra[old]
        c.runs[0]['diagnostics']['status']=2 if state==3 else int(state==2)
        if state==3:
            transient_prefix(c,3,3)
            c.runs[0]['info'].update(run_outcome=7,numerical_status=2,failure_stage='diagnostics',failure_code='fixture_capture_error',termination_reason='diagnostic_failure')
            c.expected[0].update(run_outcome=7,numerical_status=2)
            setcol(c,0,4,'event_name_index',[0,2]); setcol(c,0,4,'severity',[1,3])
        if state==0: transient_prefix(c,1,0,target=0.)
        if state==2:
            t=traceobj(c,0,0); t['eligible_count']=7; t['omitted_count']=5
        compact(c); emit(name,c)
    c=base('partial_unknown_totals',[D]); old=c.trace_ids[0,0]
    entry=trace(c,0,0,R=2,status=2); c.runs[0]['diagnostics']['traces'][0]=entry; del c.extra[old]
    t=traceobj(c,0,0); t.update(eligible_count_present=0,eligible_count=0,omitted_count_present=0,omitted_count=0)
    c.runs[0]['diagnostics']['status']=1; compact(c); emit('partial_unknown_totals',c)
    c=base('source_unavailable_trace',[D],unavailable=True); old=c.trace_ids[0,0]
    entry=trace(c,0,0,R=0,status=4); c.runs[0]['diagnostics']['traces'][0]=entry; del c.extra[old]
    compact(c); emit('source_unavailable_trace',c)
    c=base('alternate_source_dictionaries',[D])
    for kind in [1,3]:c.replace_pool(traceobj(c,1,kind)['columns']['status_names'],['fixture.mechanical_ok'])
    for i in [0,1]:c.replace_pool(traceobj(c,i,4)['columns']['event_names'],['fixture.start','fixture.end','fixture.abort'])
    emit('alternate_source_dictionaries',c)
    c=base('diagnostic_summaries',[D]); t=traceobj(c,0,0)
    t['summaries']={'population':'retained_valid_records','fields':{'nonlinear_iterations':{'count':7,'min':2,'max':8,'mean':5.,'median_present':1,'median':5.}}}; emit('diagnostic_summaries',c)
    for value,label in [(2**53+1,'above_2pow53'),(2**64-1,'max_u64')]:
        name='summary_'+label; c=base(name,[D]); t=traceobj(c,0,0)
        setcol(c,0,0,'nonlinear_iterations',[value]*7)
        t['summaries']={'population':'retained_valid_records','fields':{'nonlinear_iterations':{'count':7,'min':value,'max':value,'mean':float(value),'median_present':1,'median':float(value)}}}
        emit(name,c)
        negative(name+'_bad_mean',c,lambda c:traceobj(c,0,0)['summaries']['fields']['nonlinear_iterations'].__setitem__('mean',math.nextafter(float(value),-math.inf)),phase='select_trace_0_0')
    # Explicitly independent partial logs; included-empty + unavailable aggregates to partial.
    c=base('separate_partial_logs',[P]); log=c.extra[c.source_ids[5]]; fragment=b'{"partial":'
    c.replace(log['records_jsonl'],c.data(log['records_jsonl'])+fragment,shape=[len(c.data(log['records_jsonl']))+len(fragment)])
    log['state']=3; log['trailing_fragment_bytes']=len(fragment); c.root['format']['sections']['logs']=3; emit('separate_partial_logs',c)
    c=base('included_empty_log',[P]); log=c.extra[c.source_ids[5]]
    c.replace(log['records_jsonl'],b'',shape=[0]); c.replace(log['record_offsets'],[0],shape=[1]); log['complete_record_count']=0
    emit('included_empty_log',c)
    def second_log(c,state):
        i=len(c.source_ids); obj={'source_schema':1,'source_index':i,'kind':1,'state':state}
        if state in (0,3):
            raw=b'{"event":"other"}\n'+(b'{"fragment":' if state==3 else b'')
            obj.update(records_jsonl=c.arr('u8',raw),record_offsets=c.arr('u64',[0,len(b'{"event":"other"}\n')]),complete_record_count=1,
              trailing_fragment_bytes=len(raw)-len(b'{"event":"other"}\n'),dropped_count_present=0,dropped_count=0,schema='fixture-other-jsonl-1',info_json=c.document({'scope':'second original source'}))
        c.source_ids.append(add_meta(c,obj)); p=c.root['provenance']['sources']
        c.replace(p['source_index'],list(range(i+1)),shape=[i+1]); c.replace(p['kind'],[0]*5+[1]*(i-4),shape=[i+1]); c.replace(p['metadata_section'],c.source_ids,shape=[i+1])
        r=c.runs[1]['provenance']['references']; c.replace(r['role'],[0,1,2,3,4,5,5],shape=[7]); c.replace(r['source_index'],[0,1,2,3,4,5,i],shape=[7]); c.replace(r['order'],[0]*6+[1],shape=[7])
        c.root['format']['sections']['logs']=3
    c=base('two_original_partial_logs',[P]); second_log(c,3); emit('two_original_partial_logs',c)
    c=base('included_empty_and_unavailable_logs',[P]); log=c.extra[c.source_ids[5]]
    c.replace(log['records_jsonl'],b'',shape=[0]); c.replace(log['record_offsets'],[0],shape=[1]); log['complete_record_count']=0
    second_log(c,2); emit('included_empty_and_unavailable_logs',c)
    negative('empty_log_wrong_unavailable_aggregate',c,lambda c:c.root['format']['sections'].__setitem__('logs',2),phase='verify_all')
    c=base('declared_unused_diagnostics'); c.root['format']['version_minor']=1; c.root['format']['required_features'].append(D)
    for run in c.runs: run['diagnostics']={'status':3,'traces':[]}
    emit('declared_unused_diagnostics',c,'invalid_contract','verify_all','feature must be used')
    c=base('declared_unused_fixed'); c.root['format']['version_minor']=1; c.root['format']['required_features'].append(F)
    emit('declared_unused_fixed',c,'invalid_contract','verify_all','feature must be used')
    negative('v11_no_extension',baseline,lambda c:c.root['format'].__setitem__('version_minor',1),phase='open')
    negative('v10_extension_token',diag,lambda c:c.root['format'].__setitem__('version_minor',0),'unsupported_feature','open')
    negative('unknown_feature',diag,lambda c:c.root['format']['required_features'].append('future-required-1'),'unsupported_feature','open')
    negative('duplicate_feature',diag,lambda c:c.root['format']['required_features'].append(D),phase='open')
    negative('unknown_minor',diag,lambda c:c.root['format'].__setitem__('version_minor',2),'unsupported_feature','open')
    negative('missing_diagnostics_token',allthree,lambda c:c.root['format']['required_features'].remove(D),phase='select_run')
    negative('missing_provenance_token',allthree,lambda c:c.root['format']['required_features'].remove(P),phase='open')
    negative('missing_fixed_token',allthree,lambda c:c.root['format']['required_features'].remove(F),phase='select_run')
    negative('mixed_provenance_layout',prov,lambda c:c.root.__setitem__('inputs',{}),phase='open')
    negative('unknown_source_kind',prov,lambda c:c.replace(c.root['provenance']['sources']['kind'],[9,0,0,0,0,1]),'unsupported_feature','open')
    negative('source_metadata_alias',prov,lambda c:c.replace(c.root['provenance']['sources']['metadata_section'],[c.source_ids[0]]*6),phase='open')
    negative('source_root_cycle',prov,lambda c:c.replace(c.root['provenance']['sources']['metadata_section'],[0]+c.source_ids[1:]),phase='open')
    negative('source_identity_mismatch',prov,lambda c:c.extra[c.source_ids[1]].__setitem__('source_index',99),phase='read_source')
    negative('original_digest_mismatch',prov,lambda c:c.replace(c.root['provenance']['authored_input_sha256'],bytes(32)),phase='read_source')
    negative('original_partial',prov,lambda c:c.extra[c.source_ids[0]]['document'].__setitem__('state',3),phase='read_source')
    negative('reference_duplicate_role',prov,lambda c:c.replace(c.runs[1]['provenance']['references']['role'],[0,1,2,3,3,5]),phase='select_run_1')
    negative('reference_missing_original',prov,lambda c:c.replace(c.runs[1]['provenance']['references']['source_index'],[1,1,2,3,4,5]),phase='select_run_1')
    negative('reference_wrong_source_kind',prov,lambda c:c.replace(c.runs[1]['provenance']['references']['source_index'],[0,5,2,3,4,5]),phase='select_run_1')
    negative('reference_noncontiguous_log_order',prov,lambda c:c.replace(c.runs[1]['provenance']['references']['order'],[0,0,0,0,0,1]),phase='select_run_1')
    negative('aggregate_state_lies',prov,lambda c:c.root['format']['sections'].__setitem__('logs',2),phase='verify_all')
    negative('orphan_binary_section',prov,lambda c:c.add(2,b'owned by nobody'),phase='verify_all')
    negative('source_binary_alias',prov,lambda c:c.extra[c.source_ids[1]]['document'].__setitem__('bytes',copy.deepcopy(c.extra[c.source_ids[0]]['document']['bytes'])),phase='verify_all')
    def unused_source(c):
        sid=add_meta(c,{'source_schema':1,'source_index':6,'kind':0,'document':{'state':1}}); c.source_ids.append(sid)
        p=c.root['provenance']['sources']; c.replace(p['source_index'],list(range(7)),shape=[7]); c.replace(p['kind'],[0]*5+[1,0],shape=[7]); c.replace(p['metadata_section'],c.source_ids,shape=[7])
    negative('unused_source_descriptor',prov,unused_source,phase='verify_all')
    negative('trace_metadata_alias',diag,lambda c:c.runs[1]['diagnostics']['traces'][1].__setitem__('metadata_section',c.trace_ids[1,1]),phase='select_run_1')
    negative('trace_run_index_not_remapped',diag,lambda c:traceobj(c).__setitem__('run_index',0),phase='select_run_1')
    negative('trace_kind_mismatch',diag,lambda c:traceobj(c).__setitem__('kind',3),phase='select_run_1')
    negative('trace_availability_mismatch',diag,lambda c:traceobj(c).__setitem__('availability',2),phase='select_run_1')
    negative('trace_complete_missing_rows',diag,lambda c:traceobj(c).__setitem__('omitted_count',1),phase='select_run_1')
    negative('trace_unknown_count_placeholder',diag,lambda c:traceobj(c).__setitem__('eligible_count_present',0),phase='select_run_1')
    negative('trace_capacity_below_count',diag,lambda c:traceobj(c).__setitem__('record_capacity',1),phase='select_run_1')
    negative('trace_first_missing_on_complete',diag,lambda c:traceobj(c).__setitem__('first_missing_ordinal_present',1),phase='select_run_1')
    negative('trace_complete_error_count',diag,lambda c:traceobj(c).__setitem__('capture_error_count',1),phase='select_run_1')
    negative('accepted_population_disagrees',diag,lambda c:c.runs[0]['info'].__setitem__('accepted_count',8),phase='select_run')
    negative('trace_duplicate_record_index',diag,lambda c:setcol(c,1,1,'record_index',[0,0]),phase='select_trace_1_1')
    negative('trace_metric_unknown_bit',diag,lambda c:setcol(c,1,1,'metric_valid',[127,63]),'unsupported_feature','select_trace_1_1')
    for kind in [1,2,3]:
        negative('certified_kind'+str(kind)+'_missing_metric',diag,lambda c,k=kind:setcol(c,1,k,'metric_valid',[31,63]),phase='select_trace_1_'+str(kind))
    def invalid_nonzero(c):
        setcol(c,1,3,'certified',[0,1]); setcol(c,1,3,'metric_valid',[62,63]); setcol(c,1,3,METRICS[0],[1.,0.])
    negative('invalid_metric_nonzero',diag,invalid_nonzero,phase='select_trace_1_3')
    negative('static_criteria_missing',diag,lambda c:traceobj(c).pop('criteria'),phase='select_trace_1_1')
    negative('transient_criteria_forbidden',diag,lambda c:traceobj(c,0,0).__setitem__('criteria',copy.deepcopy(CRITERIA)),phase='select_trace_0_0')
    negative('static_negative_limit',diag,lambda c:traceobj(c)['criteria'].__setitem__('force_limit_N',-1.),phase='select_trace_1_1')
    negative('trace_summary_empty',diag,lambda c:traceobj(c).__setitem__('summaries',{'population':'retained_valid_records','fields':{}}),phase='select_trace_1_1')
    negative('trace_status_index_oob',diag,lambda c:setcol(c,1,1,'status_index',[99,0]),phase='select_trace_1_1')
    negative('trace_status_dictionary_duplicate',diag,lambda c:c.replace_pool(traceobj(c,1,1)['columns']['status_names'],['success','success']),phase='select_trace_1_1')
    negative('trace_event_dictionary_duplicate',diag,lambda c:c.replace_pool(traceobj(c,0,4)['columns']['event_names'],['event','event','other']),phase='select_trace_0_4')
    negative('trace_event_index_oob',diag,lambda c:setcol(c,0,4,'event_name_index',[0,3]),phase='select_trace_0_4')
    negative('trace_event_unknown_mask',diag,lambda c:setcol(c,0,4,'identity_valid',[16,0]),'unsupported_feature','select_trace_0_4')
    negative('trace_event_absent_negative_zero',diag,lambda c:setcol(c,1,4,'time_s',[-0.,0.]),phase='select_trace_1_4')
    negative('trace_event_sequence_repeat',diag,lambda c:setcol(c,0,4,'sequence',[0,0]),phase='select_trace_0_4')
    for e in [-1074,844,1023]:
        def profile(c,e=e):
            p=c.runs[0]['values']['profiles']; c.replace(p['requested_tolerance'],[math.ldexp(1.,e)])
        negative('fixed_exponent_reject_'+str(e).replace('-','minus_'),lossy,profile,'unsupported_profile','select_run')
    negative('fixed_tolerance_zero',lossy,lambda c:c.replace(c.runs[0]['values']['profiles']['requested_tolerance'],[0.]),'invalid_contract','select_run')
    negative('fixed_mode_code_mismatch',lossy,lambda c:c.replace(c.runs[0]['values']['profiles']['mode_code'],[2176]),'unsupported_profile','select_run')
    negative('fixed_effective_mismatch',lossy,lambda c:c.replace(c.runs[0]['values']['profiles']['effective_tolerance'],[.5]),'unsupported_profile','select_run')
    negative('fixed_presence_missing',lossy,lambda c:c.replace(c.runs[0]['values']['profiles']['tolerance_present'],[0]),phase='select_run')
    negative('fixed_static_forbidden',allthree,lambda c:c.replace(c.runs[1]['values']['profiles']['mode'],[1]),'unsupported_profile','select_run_1')
    negative('fixed_verification_above_tolerance',lossy,lambda c:c.replace(c.runs[0]['values']['verification']['max_abs_error'],[1.]*4),phase='select_run')
    negative('fixed_raw_verification_nonzero',raw,lambda c:c.replace(c.runs[0]['values']['verification']['max_abs_error'],[1e-6]*4),phase='select_run')
    def header_mismatch(c):
        v=c.runs[0]['values']; records=[list(x) for x in g.FRAME.iter_unpack(c.data(v['frame_index']))]
        index=next(i for i,x in enumerate(records) if x[5]==1); row=records[index]; payload=bytearray(c.data(v['payload']))
        head=int.from_bytes(payload[row[3]:row[3]+12],'little'); head=(head&((1<<84)-1))|(2176<<84)
        payload[row[3]:row[3]+12]=head.to_bytes(12,'little'); row[7]=g.sha(payload[row[3]:row[3]+row[4]])
        c.replace(v['payload'],payload); c.replace(v['frame_index'],b''.join(g.FRAME.pack(*x) for x in records))
    negative('fixed_header_mode_mismatch',lossy,header_mismatch,'codec_failure','read_values')
    def other_valid_profile(c):
        p=c.runs[0]['values']['profiles']
        for name,value in [('requested_tolerance',math.ldexp(1.,-11)),('effective_tolerance',math.ldexp(1.,-11)),('mode_code',2177+1074-11)]:c.replace(p[name],[value])
    negative('fixed_valid_header_other_profile',lossy,other_valid_profile,'codec_failure','read_values')
    # Exact-real comparator cases are producer rejection oracles, not malformed wire files.
    cases=[]
    for label,a,b,tau in [('equal_bound',1.,0.,1.),('one_ulp_over',math.nextafter(1.,math.inf),0.,1.),
      ('subnormal_equal',math.ldexp(1.,-1073),math.ldexp(1.,-1074),math.ldexp(1.,-1074)),
      ('opposite_huge',float.fromhex('0x1.fffffffffffffp+1023'),-float.fromhex('0x1.fffffffffffffp+1023'),1.),
      ('signed_zero',-0.,0.,math.ldexp(1.,-1073)),('rounded_subtraction_trap',1.,-math.ldexp(1.,-53),1.)]:
        diff=abs(Fraction(a)-Fraction(b)); cases.append({'name':label,'source_hex':a.hex(),'decoded_hex':b.hex(),'tolerance_hex':tau.hex(),'accept':diff<=Fraction(tau),'exact_scaled_difference':str(int(diff*2**1074)),'bitwise_equal':struct.pack('<d',a)==struct.pack('<d',b)})
    oracle='exact_real_bound_oracle.json'; (OUT/oracle).write_text(json.dumps(cases,indent=2)+'\n'); MANIFEST['producer_oracles'].append({'file':oracle,'scope':'33-limb exact-real bound decisions; independent Python Fraction/integer oracle'})
    for item in MANIFEST['positive']:
        if item['file']=='v10_original_disabled.serene':
            rawbytes=(OUT/item['file']).read_bytes(); MANIFEST['bundle_import'].append({'spool':item['file'],'manifest_version':1,'spool_sha256':hashlib.sha256(rawbytes).hexdigest(),'spool_bytes':len(rawbytes),'authored_run_id':'transient/α','producer_identity':'independent-extensions-draft/candidate-1','dependency_identity':'public-zfpy1.0.1.post0/pinned-zfp1.0.1','authored_input_sha256':g.sha(b'{ "fixture": true, "note": "exact source bytes" }\r\n').hex(),'binding_origin':'captured before solve; immutable producer assertion','without_binding':'unsupported_merge_source','with_mismatched_spool_hash':'verification_failure'})
    binding=next(x for x in MANIFEST['bundle_import'] if x['spool']=='v10_original_disabled.serene')
    binding_cases=[]
    for name,field,value,error in [('valid',None,None,None),('missing_presolve_digest','authored_input_sha256',None,'unsupported_merge_source'),('wrong_spool_extent','spool_bytes',1,'verification_failure'),('wrong_spool_digest','spool_sha256','00'*32,'verification_failure'),('missing_producer_identity','producer_identity',None,'unsupported_merge_source')]:
        item=copy.deepcopy(binding)
        if field:
            if value is None:item.pop(field)
            else:item[field]=value
        binding_cases.append({'name':name,'binding':item,'expected_error':error})
    (OUT/'bundle_manifest_binding_cases.json').write_text(json.dumps(binding_cases,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    MANIFEST['artifacts']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(OUT.iterdir()) if p.is_file() and p.name not in ['manifest.json','audit.json']}
    (OUT/'manifest.json').write_text(json.dumps(MANIFEST,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'positive':len(MANIFEST['positive']),'negative':len(MANIFEST['negative']),'artifacts':len(MANIFEST['artifacts'])}))

if __name__=='__main__': main()
