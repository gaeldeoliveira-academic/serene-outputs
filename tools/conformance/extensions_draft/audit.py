"""Independent candidate audit, not a production reader or resource qualification.

The auditor eagerly loads each tiny fixture; semantic checks are grouped into
the proposed public phases. It does not claim measured lazy I/O or bounded RSS.
No code is imported from author.py or from Serene production packages.
"""
from pathlib import Path
import hashlib, json, math, struct
from fractions import Fraction
import numpy as np
import zfpy

ROOT=Path(__file__).resolve().parents[3]; OUT=ROOT/'fixtures_extensions_draft'
BASE={'serene-container-le-1','json-descriptors-1','utf8-pools-1','frames-77-le-1','zfp-5-le64-serial-1'}
D='run-diagnostics-1'; P='provenance-sources-1'; F='timeseries-zfp-fixed-accuracy-1'; EXT={D,P,F}
TYPES={'u8':(2,'B',1),'u32':(3,'I',4),'u64':(4,'Q',8),'f64':(5,'d',8),'frames77':(6,None,77),'i8':(7,'b',1),'i32':(8,'i',4)}
METRICS=['force_residual_N','moment_residual_Nm','constraint_translation_m','constraint_rotation_rad','increment_translation_m','increment_rotation_rad']
FRAME=struct.Struct('<QQQQQBI32s')
class Failure(Exception):
    def __init__(self,code,rule): self.code=code; self.rule=rule; super().__init__(code+': '+rule)
def check(value,rule,code='invalid_contract'):
    if not value: raise Failure(code,rule)
def poszero(x): return struct.pack('<d',x)==bytes(8)
def sha(x): return hashlib.sha256(x).digest()

class Audit:
    def __init__(self,path):
        self.blob=path.read_bytes(); b=self.blob
        check(len(b)>=128 and sha(b[:96])==b[96:128],'bootstrap hash','verification_failure')
        h=struct.unpack('<8sHHIIIQQQQQ32s',b[:96]); self.minor=h[2]
        check(h[0]==b'SERENE\0\0' and h[1]==1 and h[3:6]==(0x01020304,128,64),'bootstrap')
        check(h[6]==len(b) and h[7]+64*h[8]==len(b) and h[9]==0 and h[10]==1,'extent')
        check(sha(b[h[7]:])==h[11],'directory digest','verification_failure')
        self.sections=[]; offset=128
        for i in range(h[8]):
            sid,kind,flags,start,n,digest=struct.unpack_from('<QIIQQ32s',b,h[7]+i*64)
            check(sid==i and kind in range(1,9) and flags==0 and start==offset,'directory structure')
            payload=b[start:start+n]; check(sha(payload)==digest,'section digest','verification_failure')
            self.sections.append((kind,payload)); offset+=n
        check(offset==h[7],'directory end'); self.root=self.meta(0); self.phase='open'; self.runs={}; self.traces={}; self.sources={}
        self.run_sections=[]; self.source_sections=[]; self.global_ownership={0}; self.scope_ownership=[]; self.used_sources=set(); self.used_fixed=False; self.used_diag=False

    def meta(self,sid):
        check(type(sid) is int and 0<=sid<len(self.sections) and self.sections[sid][0]==1,'metadata reference')
        return json.loads(self.sections[sid][1])

    def data(self,d,ty=None,shape=None):
        check(isinstance(d,dict) and set(d)=={'section','dtype','shape'},'descriptor grammar')
        sid=d['section']; tag=d['dtype']; check(tag in TYPES,'dtype'); kind,fmt,width=TYPES[tag]
        check(type(sid) is int and 0<=sid<len(self.sections),'section range')
        check(self.sections[sid][0]==kind,'descriptor kind')
        check(1<=len(d['shape'])<=2 and all(type(x) is int and 0<=x<=2**40 for x in d['shape']),'shape')
        check(math.prod(d['shape'])*width==len(self.sections[sid][1]),'descriptor extent')
        check(ty is None or tag==ty,'descriptor type'); check(shape is None or d['shape']==shape,'column shape')
        b=self.sections[sid][1]
        return b if fmt is None else list(struct.unpack('<'+fmt*(len(b)//width),b))

    def raw(self,d): self.data(d); return self.sections[d['section']][1]
    def pool(self,p):
        count=p['count']; data=bytes(self.data(p['bytes'],'u8')); offsets=self.data(p['offsets'],'u64',[count+1])
        check(offsets[0]==0 and offsets[-1]==len(data) and all(a<=b for a,b in zip(offsets,offsets[1:])),'pool offsets')
        return [data[a:b].decode('utf-8') for a,b in zip(offsets,offsets[1:])]

    def own(self,obj,seen):
        if isinstance(obj,dict):
            if set(obj)=={'section','dtype','shape'}:
                self.data(obj); sid=obj['section']; check(sid not in seen,'aliased data section'); seen.add(sid)
            else:
                for value in obj.values(): self.own(value,seen)
        elif isinstance(obj,list):
            for value in obj:self.own(value,seen)

    def open(self):
        fm=self.root['format']; tokens=fm['required_features']; self.features=set(tokens)
        check(self.minor in (0,1),'minor','unsupported_feature'); check(fm['version_major']==1 and fm['version_minor']==self.minor,'version agreement')
        check(len(tokens)==len(self.features),'duplicate feature'); check(self.features<=BASE|EXT,'unknown feature','unsupported_feature')
        if self.minor==0:check(self.features<=BASE,'feature unsupported in version 1.0','unsupported_feature')
        check(BASE<=self.features,'mandatory feature'); check((self.minor==0 and self.features==BASE) or (self.minor==1 and bool(self.features&EXT)),'minor/feature agreement')
        idx=self.root['results']['index']; self.run_sections=self.data(idx['run_metadata_section'],'u64'); self.kinds=self.data(idx['kind'],'u8'); self.run_ids=self.pool(idx['run_id'])
        check(len(self.run_sections)==len(set(self.run_sections)) and 0 not in self.run_sections,'run metadata uniqueness')
        self.own(idx,self.global_ownership); self.global_ownership.update(self.run_sections)
        if P in self.features:
            check('provenance' in self.root and not {'inputs','logs','context'}&self.root.keys(),'provenance layout')
            p=self.root['provenance']; s=p['sources']; self.source_sections=self.data(s['metadata_section'],'u64'); n=len(self.source_sections)
            check(1<=n<=1000000 and self.data(s['source_index'],'u64',[n])==list(range(n)),'source catalog')
            self.source_kinds=self.data(s['kind'],'u8',[n]); check(all(k in (0,1) for k in self.source_kinds),'source kind','unsupported_feature')
            check(len(set(self.source_sections))==n and not set(self.source_sections)&self.global_ownership,'source metadata uniqueness')
            self.original=p['original_source_index']; check(0<=self.original<n and self.source_kinds[self.original]==0,'original source')
            self.data(p['authored_input_sha256'],'u8',[32]); self.own(p,self.global_ownership); self.global_ownership.update(self.source_sections)
        else:
            check('provenance' not in self.root,'missing provenance feature')
            for name in ['inputs','logs','context']:
                if name in self.root:self.own(self.root[name],self.global_ownership)

    def select_run(self,i):
        self.phase='select_run' if i==0 else 'select_run_'+str(i)
        run=self.meta(self.run_sections[i]); self.runs[i]=run; seen=set()
        check(run['info']['kind']==self.kinds[i] and run['info']['run_id']==self.run_ids[i],'run identity')
        for name in ['info','channels','requests','values','time','step_index','load_levels','level_index']:
            if name in run:self.own(run[name],seen)
        if 'values' in run:self.profiles(run)
        if P in self.features:
            check('provenance' in run,'run provenance missing'); refs=run['provenance']['references']
            roles=self.data(refs['role'],'u8'); ids=self.data(refs['source_index'],'u64',[len(roles)]); order=self.data(refs['order'],'u64',[len(roles)])
            check(5<=len(roles)<=69 and roles==[0,1,2,3,4]+[5]*(len(roles)-5),'reference roles')
            check(order==[0]*5+list(range(len(roles)-5)),'reference source order')
            check(all(x<len(self.source_sections) for x in ids) and ids[0]==self.original,'reference source index')
            check(all(self.source_kinds[x]==(role==5) for role,x in zip(roles,ids)),'reference source kind')
            self.own(run['provenance'],seen); self.used_sources.update(ids)
        else:check('provenance' not in run,'missing provenance feature')
        if D in self.features:
            check('diagnostics' in run,'diagnostics envelope missing'); d=run['diagnostics']; entries=d['traces']
            check(len(entries)<=5 and len({x['kind'] for x in entries})==len(entries),'trace kinds unique')
            mids=[x['metadata_section'] for x in entries]
            check(len(set(mids))==len(mids) and not set(mids)&(self.global_ownership|seen),'trace metadata ownership')
            statuses=[]
            for entry in entries:
                sid=entry['metadata_section']; t=self.meta(sid); kind=entry['kind']
                check(kind in range(5),'trace kind','unsupported_feature'); check(t['trace_schema']==1 and t['kind']==kind and t['run_index']==i,'trace identity')
                self.trace_envelope(t)
                if kind==0 and t['eligible_count_present']:check(t['eligible_count']==run['info']['accepted_count'],'accepted population disagreement')
                self.traces[i,kind]=t; statuses.append(t)
            expected=3 if not statuses else (2 if any(t['requirement']==1 and t['capture_status'] in (2,3) for t in statuses) else (0 if all(t['capture_status']==0 for t in statuses) else (4 if all(t['capture_status'] in (1,4) for t in statuses) else 1)))
            check(d['status']==expected,'diagnostics aggregate'); self.used_diag|=bool(entries); seen.update(mids)
        else:check('diagnostics' not in run and run['info']['run_outcome']!=7,'missing diagnostics feature')
        self.scope_ownership.append(seen)

    def trace_envelope(self,t):
        status=t['capture_status']; r=t['record_count']; cap=t['record_capacity']; check(status in range(5) and t['requirement'] in (0,1),'trace status/requirement')
        check(type(cap) is int and 0<cap and 0<=r<=cap and r<=2**40,'trace capacity')
        check(t['availability']==[0,2,3,3,2][status],'trace availability')
        check(('columns' in t)==(status not in (1,4)),'trace columns availability')
        known=t['eligible_count_present']; check(known in (0,1) and known==t['omitted_count_present'],'count presence')
        eligible=t['eligible_count']; omitted=t['omitted_count']; first=t['first_missing_ordinal_present']; ordinal=t['first_missing_ordinal']
        check(first in (0,1) and (first or ordinal==0),'first missing presence')
        check((known and eligible==r+omitted) or (not known and eligible==omitted==0),'count evidence')
        check(t['capture_error_count']>0 if status==3 else t['capture_error_count']==0,'capture error count')
        if status==0:check(known==1 and omitted==0 and not first and not t['failure_code'],'complete count evidence')
        if status==1:check(r==0 and not first and (not known or eligible==omitted==0),'not started evidence')
        if status in (2,3):
            check(first and ordinal==r and (not known or omitted>=1) and bool(t['failure_code']),'partial evidence')
            if status==2:check(r==cap,'capacity prefix')
        if status==4:
            check(r==0 and bool(t['failure_code']) and (not first or ordinal==0),'unavailable evidence')
            if known:check((first and omitted>=1) or (not first and omitted==0),'unavailable missing identity')
        if status in (1,4):check('criteria' not in t and 'summaries' not in t,'unavailable metadata')

    def select_trace(self,i,k):
        self.phase=f'select_trace_{i}_{k}'; t=self.traces[i,k]
        if 'columns' not in t:return
        r=t['record_count']; cols=t['columns']; check(self.data(cols['record_index'],'u64',[r])==list(range(r)),'record ordinals')
        check(('criteria' in t)==(k in (1,2,3)),'criteria presence')
        typed={'record_index':'u64'}
        if k==0:typed.update(accepted_step='u64',time_s='f64',nonlinear_iterations='u64')
        elif k in (1,2,3):
            typed.update(attempt_id='u64',factor='f64',metric_valid='u8'); typed.update({m:'f64' for m in METRICS})
            if k==2:typed['accepted_level_id']='u64'
            else:
                typed.update(certified='u8',rolled_back='u8',status_index='u32')
                typed.update({'accepted_level_present':'u8','accepted_level_id':'u64','newton_record_count':'u64'} if k==1 else {'iteration_id':'u64','row_kind':'u8'})
        else:typed.update(sequence='u64',event_name_index='u32',severity='u8',identity_valid='u8',time_s='f64',accepted_step='u64',accepted_level='u64',attempt_id='u64')
        values={name:self.data(cols[name],ty,[r]) for name,ty in typed.items()}
        check(set(cols)==set(typed)|({'status_names'} if k in (1,3) else {'event_names'} if k==4 else set()),'fixed column grammar')
        for name,ty in typed.items():
            if ty=='f64':check(all(math.isfinite(x) for x in values[name]),'finite trace column')
        if k==0:
            check(values['accepted_step']==list(range(1,r+1)),'accepted step ordinals')
            check(all(a<b for a,b in zip(values['time_s'],values['time_s'][1:])),'accepted times')
        if k in (1,2,3):
            mask=values['metric_valid']; check(all(x<64 for x in mask),'unknown metric bit','unsupported_feature')
            certified=[1]*r if k==2 else values['certified']; check(all(x in (0,1) for x in certified),'certified flag')
            check(all(not flag or bits==63 for flag,bits in zip(certified,mask)),'certified metric validity')
            for bit,name in enumerate(METRICS):
                check(all((x>=0 if valid&(1<<bit) else poszero(x)) for x,valid in zip(values[name],mask)),'metric presence value')
            criteria=t['criteria']; check(criteria['norm']=='block_max_abs_component','norm')
            check(math.isfinite(criteria['evaluation_time_s']),'evaluation time')
            for name,value in criteria.items():
                if name in ('norm','evaluation_time_s'):continue
                check(math.isfinite(value) and (value>0 if 'limit' in name else value>=0),'criteria value')
            if k==1:check(values['attempt_id']==list(range(1,r+1)) and values['accepted_level_present']==certified,'attempt identities')
            if k==2:check(values['accepted_level_id']==list(range(r)),'accepted level identities')
            if k in (1,3):
                names=self.pool(cols['status_names']); check(1<=len(names)<=64 and len(set(names))==len(names) and all(names) and all(x<len(names) for x in values['status_index']),'status dictionary')
                check(all(x in (0,1) for x in values['rolled_back']),'rollback flags')
            if k==3:check(all(kind in (0,1) and (iteration>0 or kind==1) for kind,iteration in zip(values['row_kind'],values['iteration_id'])),'Newton identity')
        if k==4:
            names=self.pool(cols['event_names']); check(1<=len(names)<=64 and len(set(names))==len(names) and all(names) and all(x<len(names) for x in values['event_name_index']),'event dictionary')
            check(all(x in range(4) for x in values['severity']),'severity range')
            check(all(a<b for a,b in zip(values['sequence'],values['sequence'][1:])),'event sequence')
            check(all(x<16 for x in values['identity_valid']),'event mask','unsupported_feature')
            for bit,name in enumerate(['time_s','accepted_step','accepted_level','attempt_id']):
                check(all(valid&(1<<bit) or (poszero(x) if bit==0 else x==0) for x,valid in zip(values[name],values['identity_valid'])),'event absent placeholder')
        if 'summaries' in t:
            s=t['summaries']; allowed=['nonlinear_iterations'] if k==0 else (['newton_record_count']+METRICS if k==1 else METRICS if k in (2,3) else [])
            check(s['population']=='retained_valid_records' and bool(s['fields']) and set(s['fields'])<=set(allowed),'summary grammar')
            for name,v in s['fields'].items():
                a=values[name]
                if name in METRICS:a=[x for x,m in zip(a,values['metric_valid']) if m&(1<<METRICS.index(name))]
                check(v['count']==len(a),'summary count')
                check(v['median_present'] in (0,1),'median flag')
                if a:
                    check(v['min']==min(a) and v['max']==max(a),'summary exact extrema')
                    # Python integer->float conversion is correctly RN ties-even, including u64 max ->2^64.
                    lo,hi=float(min(a)),float(max(a)); check(math.isfinite(v['mean']) and lo<=v['mean']<=hi,'rounded mean interval')
                    check(math.isfinite(v['median']) and (lo<=v['median']<=hi if v['median_present'] else poszero(v['median'])),'rounded median interval')
                else:check(v['min']==v['max']==0 and poszero(v['mean']) and not v['median_present'] and poszero(v['median']),'empty summary')
        seen=set(); self.own(cols,seen); self.scope_ownership.append(seen)

    def profiles(self,run):
        v=run['values']; p=v['profiles']; modes=self.data(p['mode'],'u8'); n=len(modes)
        columns={name:self.data(p[name]) for name in ['profile_index','codec_version','stream_word_bits','rounding','mode_code','tolerance_present','requested_tolerance','effective_tolerance']}
        check(columns['profile_index']==list(range(n)) and all(len(a)==n for a in columns.values()),'profile columns')
        check(all(x<n for x in self.data(v['row_profile_index'],'u32',[v['shape'][0]])),'row profile index')
        for j,mode in enumerate(modes):
            check(mode in (0,1),'profile mode','unsupported_profile'); check(mode==0 or run['info']['kind']==0,'static fixed mode','unsupported_profile')
            tau=columns['requested_tolerance'][j]; eff=columns['effective_tolerance'][j]; code=columns['mode_code'][j]; present=columns['tolerance_present'][j]
            if mode==0:check(present==0 and poszero(tau) and poszero(eff) and code==2176,'reversible profile','unsupported_profile')
            else:
                check(F in self.features,'missing fixed feature'); check(present==1 and math.isfinite(tau) and tau>0,'fixed tolerance presence')
                e=math.frexp(tau)[1]-1; check(-1073<=e<=843,'fixed exponent','unsupported_profile')
                check(eff==math.ldexp(1.,e) and code==2177+e+1074,'fixed canonical profile','unsupported_profile'); self.used_fixed=True
        records=list(FRAME.iter_unpack(self.raw(v['frame_index']))); verify=v['verification']
        stats={key:self.data(value) for key,value in verify.items()}; check(all(len(a)==len(records) for a in stats.values()),'verification count')
        for j,record in enumerate(records):
            maximum,rms,count,equal=[stats[k][j] for k in ['max_abs_error','rms_error','verified_count','bitwise_equal']]
            check(count==record[2] and equal in (0,1),'frame verification evidence')
            if record[5]==0 or modes[record[6]]==0:check(poszero(maximum) and poszero(rms) and equal==1,'raw/exact verification')
            else:
                check(math.isfinite(maximum) and math.isfinite(rms) and 0<=rms<=maximum<=columns['requested_tolerance'][record[6]],'fixed error bound assertion')
                check(not equal or maximum==rms==0,'bitwise equality evidence')

    def source(self,i):
        self.phase='read_source'; s=self.meta(self.source_sections[i]); self.sources[i]=s
        check(s['source_schema']==1 and s['source_index']==i and s['kind']==self.source_kinds[i],'source identity')
        seen=set()
        if s['kind']==0:
            doc=s['document']; state=doc['state']; check(state in range(4),'document state')
            if i==self.original:
                check(state!=3,'partial original source')
                if state==0:check(sha(self.raw(doc['bytes']))==bytes(self.data(self.root['provenance']['authored_input_sha256'])),'original digest','invalid_contract')
            self.own(doc,seen)
        else:
            state=s['state']; check(state in range(4),'JSONL state')
            if state in (1,2):check(set(s)=={'source_schema','source_index','kind','state'},'absent log payload')
            else:
                data=self.raw(s['records_jsonl']); offsets=self.data(s['record_offsets'],'u64',[s['complete_record_count']+1])
                check(offsets[0]==0 and offsets[-1]+s['trailing_fragment_bytes']==len(data),'JSONL offsets')
                for a,b in zip(offsets,offsets[1:]):check(b>a and data[b-1]==10 and isinstance(json.loads(data[a:b]),dict),'complete JSONL object')
                check(state==3 or (s['trailing_fragment_bytes']==0 and s['dropped_count']==0),'included JSONL completeness')
                for name in ['records_jsonl','record_offsets','info_json']:self.own(s[name],seen)
        self.scope_ownership.append(seen)

    def verify_all(self):
        self.phase='verify_all'; seen=set(self.global_ownership)
        for scope in self.scope_ownership:
            check(not seen&scope,'global ownership alias'); seen.update(scope)
        check(seen==set(range(len(self.sections))),'orphan section')
        if D in self.features:check(self.used_diag,'unused diagnostics feature')
        if F in self.features:check(self.used_fixed,'unused fixed feature')
        if P in self.features:
            check(self.used_sources|{self.original}==set(range(len(self.source_sections))),'unused source descriptor')
            populations={'inputs':[],'context':[],'logs':[]}
            for run in self.runs.values():
                refs=run['provenance']['references']; roles=self.data(refs['role']); indices=self.data(refs['source_index'])
                for role,index in zip(roles,indices):
                    source=self.sources[index]; state=source['document']['state'] if source['kind']==0 else source['state']
                    populations['inputs' if role<4 else 'context' if role==4 else 'logs'].append(state)
            for name,states in populations.items():
                state=1 if not states or all(s==1 for s in states) else 0 if all(s==0 for s in states) else 2 if not any(s in (0,3) for s in states) and 2 in states else 3
                check(self.root['format']['sections'][name]==state,'aggregate source state')

    def numerical(self,expected):
        self.phase='read_values'
        frames=0
        for i in range(len(self.runs)):
            ex=expected['runs'][i] if expected else None
            run=self.runs[i]
            if 'values' not in run:continue
            v=run['values']; payload=self.raw(v['payload']); actual=[]; records=list(FRAME.iter_unpack(self.raw(v['frame_index'])))
            profiles=v['profiles']; modes=self.data(profiles['mode']); codes=self.data(profiles['mode_code'])
            for row,begin,count,off,n,encoding,pidx,digest in records:
                b=payload[off:off+n]; check(sha(b)==digest,'frame payload digest','verification_failure')
                if encoding:
                    check(((int.from_bytes(b[:12],'little')>>84)&4095)==codes[pidx],'canonical header code','codec_failure'); a=zfpy.decompress_numpy(b)
                else:a=np.frombuffer(b,dtype='<f8')
                check(len(a)==count and np.isfinite(a).all(),'decoded finite values'); actual.extend(int(x) for x in a.view('<u8')); frames+=1
            if ex:
                bits=[int(x,16) for x in ex['fixed_accuracy']['decoded_bits_hex']] if 'fixed_accuracy' in ex else [int(x,16) for row in ex['bits_hex'] for x in row]
                check(actual==bits,'independent decoded expectation')
        return frames

def run():
    manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf-8')); result={'status':'draft audit only','positive':[],'negative':[],'limitations':['Tiny fixtures loaded eagerly; validation phases are semantic scheduling checks, not measured lazy I/O or memory qualification.','No production reader/writer or installed package exercised.','1.0 baseline semantics inherited from accepted corpus; this audit focuses on extension deltas.']}
    for filename,entry in manifest['artifacts'].items():
        b=(OUT/filename).read_bytes(); assert len(b)==entry['bytes'] and hashlib.sha256(b).hexdigest()==entry['sha256'],filename
    for polarity in ['positive','negative']:
        for item in manifest[polarity]:
            a=Audit(OUT/item['file']); failed=None
            try:
                a.open()
                for i in range(len(a.run_sections)):a.select_run(i)
                for i,k in sorted(a.traces):a.select_trace(i,k)
                for i in range(len(a.source_sections)):a.source(i)
                a.verify_all()
                if polarity=='negative':a.numerical(None)
            except Failure as error:failed={'error':error.code,'phase':a.phase,'rule':error.rule}
            if polarity=='positive':
                assert failed is None,(item['file'],failed)
                frames=a.numerical(json.loads((OUT/item['expected']).read_text(encoding='utf-8')))
                result[polarity].append({'file':item['file'],'sections':len(a.sections),'frames':frames,'passed':True})
            else:
                assert failed is not None,(item['file'],'not rejected')
                assert failed['error']==item['expected_error'] and failed['phase']==item['detection_phase'],(item['file'],item,failed)
                result[polarity].append({'file':item['file'],**failed,'passed':True})
    for item in manifest['producer_oracles']:
        for x in json.loads((OUT/item['file']).read_text()):
            a,b,tau=[float.fromhex(x[k]) for k in ['source_hex','decoded_hex','tolerance_hex']]; d=abs(Fraction(a)-Fraction(b))
            assert (d<=Fraction(tau))==x['accept'] and str(int(d*2**1074))==x['exact_scaled_difference']
    result['bundle_binding_cases']=[]
    for item in json.loads((OUT/'bundle_manifest_binding_cases.json').read_text(encoding='utf-8')):
        binding=item['binding']; error=None
        required={'manifest_version','spool','spool_sha256','spool_bytes','authored_run_id','producer_identity','dependency_identity','authored_input_sha256','binding_origin'}
        if not required<=binding.keys():error='unsupported_merge_source'
        else:
            b=(OUT/binding['spool']).read_bytes()
            if len(b)!=binding['spool_bytes'] or hashlib.sha256(b).hexdigest()!=binding['spool_sha256']:error='verification_failure'
        assert error==item['expected_error'],item['name']
        result['bundle_binding_cases'].append({'name':item['name'],'error':error,'passed':True})
    result['manifest_sha256']=hashlib.sha256((OUT/'manifest.json').read_bytes()).hexdigest()
    result['counts']={key:len(result[key]) for key in ['positive','negative']}; result['sections']=sum(x['sections'] for x in result['positive']); result['frames']=sum(x['frames'] for x in result['positive'])
    (OUT/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8'); print(json.dumps({k:result[k] for k in ['counts','sections','frames','manifest_sha256']}))

if __name__=='__main__':run()
