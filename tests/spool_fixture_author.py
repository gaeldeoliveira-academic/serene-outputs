"""Independent single-run importer corpus; never modifies accepted fixtures."""
from pathlib import Path
import copy, hashlib, importlib.util, itertools, json, math, struct, sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('frozen_extension_author', ROOT/'tools/conformance/extensions_draft/author.py')
a = importlib.util.module_from_spec(spec); spec.loader.exec_module(a)
OUT = Path(sys.argv[1]).resolve(); OUT.mkdir(parents=True, exist_ok=False)
manifest = {'schema': 1, 'cases': []}
ORIGINAL = a.g.sha(b'{ "fixture": true, "note": "exact source bytes" }\r\n').hex()

def single(c, selected=0):
    kept = {v for (r, k), v in c.trace_ids.items() if r == selected} | set(c.source_ids)
    c.extra = {k:v for k,v in c.extra.items() if k in kept}
    c.trace_ids = {(0,k):v for (r,k),v in c.trace_ids.items() if r == selected}
    for sid in c.trace_ids.values(): c.extra[sid]['run_index'] = 0
    c.runs = [c.runs[selected]]; c.run_ids = [c.run_ids[selected]]; c.expected = [c.expected[selected]]
    ix = c.root['results']['index']
    c.replace(ix['run_index'], [0], shape=[1]); c.replace(ix['kind'], [c.runs[0]['info']['kind']], shape=[1])
    c.replace_pool(ix['run_id'], [c.runs[0]['info']['run_id']]); c.replace_pool(ix['run_name'], ['same name'])
    c.replace(ix['run_name']['present'], [1], shape=[1]); c.replace(ix['run_metadata_section'], c.run_ids, shape=[1])
    a.compact(c)
    return c

def emit(name, c, expected='ok', trusted=True, raw=None, authored=ORIGINAL, **extra):
    blob = a.encode(c) if raw is None else raw
    file = name+'.serene'; (OUT/file).write_bytes(blob)
    manifest['cases'].append(dict(file=file, expected=expected, trusted=trusted,
        sha256=hashlib.sha256(blob).hexdigest(), bytes=len(blob),
        authored_sha256=authored, run_id=c.runs[0]['info']['run_id'], **extra))

def negative(name, base, mutate, expected='invalid_contract'):
    c=copy.deepcopy(base); mutate(c); emit(name,c,expected)

legacy = single(a.base('legacy'))
emit('legacy', legacy, memory_small=True)
for n in range(1,4):
    for features in itertools.combinations(a.FEATURES,n):
        name='features_'+''.join('dpf'[a.FEATURES.index(f)] for f in features)
        emit(name,single(a.base(name,features)))
for features,label in [([a.D],'static_d'),([a.P],'static_p'),([a.D,a.P],'static_dp')]:
    emit(label,single(a.base(label,features),1))
d=single(a.base('diagnostics',[a.D])); p=single(a.base('provenance',[a.P])); f=single(a.base('fixed',[a.F]))
static=single(a.base('static',[a.D]),1)
emit('missing_original_untrusted', single(a.base('none')), trusted=False)
for state in (1,2):
    c=single(a.base('original_state',[a.P])); a.original_state(c,state)
    emit('original_state_'+str(state),c)
    emit('original_state_unbound_'+str(state),c,'unsupported_feature',trusted=False)
c=copy.deepcopy(legacy)
for branch in ('inputs','logs','context'):
    del c.root[branch]; c.root['format']['sections'][branch]=1
a.compact(c); emit('legacy_disabled',c); emit('legacy_disabled_unbound',c,'unsupported_feature',trusted=False)
c=copy.deepcopy(legacy); doc=c.root['inputs']['source_json']; doc['state']=3
c.replace(doc['bytes'],b'{"incomplete":',shape=[14]); c.root['format']['sections']['inputs']=3
emit('legacy_partial_original',c,'unsupported_feature')
c=copy.deepcopy(p); log=c.extra[c.source_ids[5]]
c.replace(log['records_jsonl'],b'',shape=[0]); c.replace(log['record_offsets'],[0],shape=[1]); log['complete_record_count']=0
emit('included_empty_log',c)
c=copy.deepcopy(p); log=c.extra[c.source_ids[5]]; raw=c.data(log['records_jsonl'])+b'{"fragment":'
c.replace(log['records_jsonl'],raw,shape=[len(raw)]); log.update(state=3,trailing_fragment_bytes=12); c.root['format']['sections']['logs']=3
emit('partial_log',c)
c=copy.deepcopy(legacy); doc=c.root['inputs']['source_json']; doc['media_type']='application/octet-stream'; c.replace(doc['bytes'],b'',shape=[0])
emit('included_empty_original',c,authored=hashlib.sha256(b'').hexdigest())
c=single(a.base('unavailable',[a.D],unavailable=True)); emit('unavailable_trace',c)
c=single(a.base('empty',[a.F],empty=True)); emit('fixed_empty_rows',c)
c=copy.deepcopy(static)
for kind in (1,3):c.replace_pool(a.traceobj(c,0,kind)['columns']['status_names'],['other.ok'])
c.replace_pool(a.traceobj(c,0,4)['columns']['event_names'],['other.start','other.end','other.fail']); emit('alternate_dictionary',c)
for value,label in [(2**53+1,'2pow53'),(2**64-1,'u64max')]:
    c=copy.deepcopy(d); a.setcol(c,0,0,'nonlinear_iterations',[value]*7)
    a.traceobj(c,0,0)['summaries']={'population':'retained_valid_records','fields':{'nonlinear_iterations':{'count':7,'min':value,'max':value,'mean':float(value),'median_present':1,'median':float(value)}}}
    emit('summary_'+label,c)
    negative('bad_summary_'+label,c,lambda c:a.traceobj(c,0,0)['summaries']['fields']['nonlinear_iterations'].__setitem__('mean',math.nextafter(float(value),-math.inf)))

negative('unknown_feature',d,lambda c:c.root['format']['required_features'].append('future-1'),'unsupported_feature')
negative('v10_extension',d,lambda c:c.root['format'].__setitem__('version_minor',0),'unsupported_feature')
negative('v11_empty',legacy,lambda c:c.root['format'].__setitem__('version_minor',1))
negative('bad_uuid',legacy,lambda c:c.root['format'].__setitem__('file_id','invalid'))
negative('bad_calendar',legacy,lambda c:c.root['format'].__setitem__('created_utc','2026-02-30T00:00:00Z'))
negative('orphan_section',p,lambda c:c.add(2,b'orphan'))
negative('source_alias',p,lambda c:c.extra[c.source_ids[1]]['document'].__setitem__('bytes',copy.deepcopy(c.extra[c.source_ids[0]]['document']['bytes'])))
negative('source_identity',p,lambda c:c.extra[c.source_ids[1]].__setitem__('source_index',99))
negative('source_digest',p,lambda c:c.replace(c.root['provenance']['authored_input_sha256'],bytes(32)))
negative('source_role',p,lambda c:c.replace(c.runs[0]['provenance']['references']['role'],[0,1,2,3,3,5]))
negative('source_kind',p,lambda c:c.replace(c.root['provenance']['sources']['kind'],[9,0,0,0,0,1]),'unsupported_feature')
negative('source_aggregate',p,lambda c:c.root['format']['sections'].__setitem__('logs',2))
negative('trace_alias',d,lambda c:c.runs[0]['diagnostics']['traces'][1].__setitem__('metadata_section',c.trace_ids[0,0]))
negative('trace_run',d,lambda c:a.traceobj(c,0,0).__setitem__('run_index',1))
negative('trace_count',d,lambda c:a.traceobj(c,0,0).__setitem__('omitted_count',1))
negative('trace_accepted',d,lambda c:c.runs[0]['info'].__setitem__('accepted_count',8))
negative('trace_record',d,lambda c:a.setcol(c,0,0,'record_index',[0]*7))
negative('metric_unknown',static,lambda c:a.setcol(c,0,1,'metric_valid',[127,63]),'unsupported_feature')
for k in (1,2,3):negative('metric_certified_'+str(k),static,lambda c,k=k:a.setcol(c,0,k,'metric_valid',[31,63]))
negative('static_criteria',static,lambda c:a.traceobj(c,0,1).pop('criteria'))
negative('static_dictionary',static,lambda c:c.replace_pool(a.traceobj(c,0,1)['columns']['status_names'],['ok','ok']))
negative('event_mask',d,lambda c:a.setcol(c,0,4,'identity_valid',[16,0]),'unsupported_feature')
negative('event_index',d,lambda c:a.setcol(c,0,4,'event_name_index',[0,3]))
negative('event_severity',d,lambda c:a.setcol(c,0,4,'severity',[4,1]))
negative('event_order',d,lambda c:a.setcol(c,0,4,'sequence',[0,0]))
negative('fixed_code',f,lambda c:c.replace(c.runs[0]['values']['profiles']['mode_code'],[2176]),'unsupported_profile')
def other_profile(c):
    for key,value in [('requested_tolerance',2.**-11),('effective_tolerance',2.**-11),('mode_code',3240)]:c.replace(c.runs[0]['values']['profiles'][key],[value])
negative('fixed_header_profile',f,other_profile,'codec_failure')
negative('fixed_zero',f,lambda c:c.replace(c.runs[0]['values']['profiles']['requested_tolerance'],[0.]))
negative('fixed_exponent',f,lambda c:c.replace(c.runs[0]['values']['profiles']['requested_tolerance'],[2.**-1074]),'unsupported_profile')
negative('fixed_bound',f,lambda c:c.replace(c.runs[0]['values']['verification']['max_abs_error'],[1.]*4))
negative('duplicate_original_key',legacy,lambda c:c.replace(c.root['inputs']['source_json']['bytes'],b'{"a":0,"a":1}',shape=[13]))
emit('multi_run',a.base('multi_run',[a.D]),'unsupported_feature')
raw=bytearray(a.encode(legacy)); raw[-1]^=1; emit('bad_directory_hash',legacy,'verification_failure',raw=raw)
raw=bytearray(a.encode(legacy)); struct.pack_into('<Q',raw,56,2); raw[96:128]=hashlib.sha256(raw[:96]).digest(); emit('unknown_flags',legacy,'unsupported_feature',raw=raw)
emit('short_file',legacy,'invalid_contract',raw=b'SERENE')
emit('tiny_budget',legacy,'resource_limit',workspace=4096,metadata=4096)
emit('wrong_binding_digest',legacy,'verification_failure',wrong_binding_digest=True)
emit('wrong_binding_extent',legacy,'verification_failure',wrong_binding_extent=True)
emit('wrong_authored_digest',legacy,'invalid_contract',wrong_authored_digest=True)
emit('mutation_after_open',legacy,mutation=True)
# All numeric history arrays and frame tables remain on disk. This file has over
# 1000 times the small file's numerical population with the same section graph.
n=16384
c=a.g.make('large',[{'id':'transient/α','name':'same name','kind':0,'array':a.np.zeros((2,n)),
                    'axis':[i*.01 for i in range(n)],'ids':list(range(n)),
                    'accepted_count':n-1,'last_accepted':(n-1)*.01}])
c.extra={};c.trace_ids={};c.source_ids=[];c.runs[0]['info']['requested_end']=(n-1)*.01;emit('large_history',c,memory_large=True)
manifest['author_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'cases':len(manifest['cases']),'manifest':str(OUT/'manifest.json')}))
