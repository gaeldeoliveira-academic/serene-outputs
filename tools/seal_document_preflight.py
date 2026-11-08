"""Seal the public document-preflight amendment and preservation of both prior installs."""
from pathlib import Path
import hashlib,json,datetime
R=Path(__file__).resolve().parents[1];B=R/'build/document-preflight-msvc';Q=B/'qualification.json'
E=R.parent/'serene-theory/validation/outputs_document_preflight_consumer'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def entry(p,base=R):return {'path':p.relative_to(base).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}
def verify(p,e):assert p.stat().st_size==e['bytes'] and sha(p)==e['sha256'],p
def tree(p):return [entry(f,p) for f in sorted(p.rglob('*')) if f.is_file() and '__pycache__' not in f.parts]
assert not Q.exists(),'Never overwrite a qualification seal.'
checkpoint=R/'history/before_document_preflight/qualified_0_2_1_20261108';old=load(checkpoint/'manifest.json')
for name,e in old['files'].items():verify(checkpoint/name,e)
prior=R/'build/capacity-planner-msvc/qualification.json'
assert sha(prior)==old['qualification_sha256']=='9a23372149eef1669d9c7d8243c2b934063421db77849d10918bcafa497867ee'
protected=[]
for prefix,manifest,digest in [('install-extensions','extensions-msvc','6fa1c36a5a43459ae7da39bd77cb38b896e0ac4c36ca46c53e792d71cee06ac5'),('install-capacity-planner','capacity-planner-msvc','ab8ca85075442747070b76aa210bf1b09d1c4694267329b3e886a6a5fbd7bad8')]:
 p=R/f'build/{manifest}/install-manifest.json';assert sha(p)==digest
 entries=load(p)['files']
 for e in entries:verify(R/f'build/{prefix}'/e['path'],e)
 protected.append({'prefix':f'build/{prefix}','manifest_sha256':digest,'verified_files':len(entries)})
installed=load(B/'install-manifest.json')
for e in installed['files']:
 verify(R/'build/install-document-preflight'/e['path'],e);verify(E/'prefix'/e['path'],e)
source=[]
for folder in ('include','src','cmake','third_party','tests','tools','docs'):
 source.extend(entry(p) for p in sorted((R/folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts)
source.extend(entry(R/p) for p in ('CMakeLists.txt','README.md','spec/FORMAT.md','spec/FORMAT-1.1.md'))
before={e['path']:e for e in load(prior)['source_files']};changed=[e for e in source if e['path'] in before and e['sha256']!=before[e['path']]['sha256']];added=[e for e in source if e['path'] not in before]
assert {e['path'] for e in changed}=={'CMakeLists.txt','README.md','cmake/BUILDING.md','src/spool_reader.hpp','src/spool_reader.cpp'}
snapshot=load(R/'docs/codec_extensions_snapshot.json')
for name,digest in snapshot['source_files'].items():assert sha(R/name)==digest,name
review=B/'document-preflight-independent-review.json';assert sha(review)=='ae44a266a0998db6f41eed424009be17944953d7586c805e3ef9427823d47723'
case_receipts=[];artifacts=[]
for d in sorted(B.iterdir()):
 if d.is_dir() and (d.name.startswith('document-preflight-') or d.name.startswith('bundle-tests-')):
  artifacts.extend(entry(p) for p in sorted(d.rglob('*')) if p.is_file())
  if d.name.startswith('document-preflight-'):
   r=load(d/'receipt.json');assert r=={'status':'passed','cases':28,'actual_spool_equivalence_cases':17};case_receipts.append(entry(d/'receipt.json'))
assert len(case_receipts)==2
for config in ('Release','Debug'):
 artifacts.extend(entry(p) for p in sorted((B/config).iterdir()) if p.suffix in ('.exe','.lib'))
 log=R/f'build/document-preflight-{config.lower()}-tests.log';assert '100% tests passed' in log.read_text()
assert (R/'build/document-preflight-consumer.log').read_text().count('100% tests passed')==2
assert all(sha(E/'source'/p.name)==sha(p) for p in (R/'tests/install_document_preflight').iterdir() if p.is_file())
receipt={'schema':1,'status':'qualified','package_version':'0.2.2','scope':'Additive public bounded document preflight using existing spool parser/charge; combined with unchanged0.2.1 count planner',
 'sealed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'api':{'header':entry(R/'include/serene/outputs/document.hpp'),'function':'std::expected<DocumentPreflight,Status> preflight_document(const DocumentView&,DocumentPreflightLimits={}) noexcept','component':'archive','kinds':['inactive','retained_bytes','json_document'],'result_fields':['source_bytes','parser_charge_bytes','workspace_bytes','sha256'],'policy':'Retained producer media_type must be nonempty valid UTF-8; actual wire acceptance unchanged.'},
 'source_files':source,'delta_from_qualified_0_2_1':{'changed':changed,'added':added},'preservation':{'checkpoint_manifest':entry(checkpoint/'manifest.json'),'checkpoint_files_verified':len(old['files']),'prior_qualification_sha256':sha(prior),'old_installs':protected,'codec_files_unchanged':len(snapshot['source_files']),'model_planner_unchanged':True,'wire_specs_unchanged':True},
 'new_install_manifest':entry(B/'install-manifest.json'),'build_artifacts':artifacts,'logs':[entry(p) for p in sorted((R/'build').glob('document-preflight-*.log'))],
 'tests':{'Release':'2/2 focused groups passed','Debug':'2/2 focused groups passed','document_checks_per_configuration':28,'actual_spool_equivalences_per_configuration':17,'bundle_regressions':'existing retention, role, verified-copy, retry and8faultcases','case_receipts':case_receipts,'installed_archive_consumer':'Release/Debug1/1 each; combined count planner and public document API'},
 'independent_review':entry(review),'consumer_root':str(E),'consumer_source':tree(E/'source'),'consumer_executables':[entry(E/f'build/{c}/installed_document_preflight.exe',E) for c in ('Release','Debug')],
 'commands':['tools/build_document_preflight.cmd Release','tools/build_document_preflight.cmd Debug',"ctest --test-dir build/document-preflight-msvc -C <Release|Debug> -R 'serene_outputs_(document_preflight|bundle)$' --output-on-failure",'cmake --install build/document-preflight-msvc --config <Release|Debug> --prefix build/install-document-preflight','tools/qualify_document_preflight_consumer.cmd'],
 'limitations':['Windows MSVC only.','Standalone document preflight does not establish whole-spool resident metadata/workspace admission.','document_bytes limits included JSON materialization; all retained content also has4GiB protocol extent.','Metadata-object/JSONL-record grammars are separate and unchanged.','No Python/package/codec/fullhistorical test reruns and no Git mutations.'],
 'post_test_change':'Final public-header comments only clarified computed hashes and nonempty producer media policy; no executable/API-layout change.'}
Q.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'path':str(Q),'sha256':sha(Q),'changed_files':len(changed),'added_files':len(added),'old_installed_files_verified':sum(p['verified_files'] for p in protected)}))
