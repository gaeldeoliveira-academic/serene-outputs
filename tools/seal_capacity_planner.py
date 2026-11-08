"""Seal the narrow model-only planner amendment, preserving the qualified0.2 closure."""
from pathlib import Path
import hashlib,json,datetime,sys
R=Path(__file__).resolve().parents[1]
Q=R/'build/capacity-planner-msvc/qualification.json'
E=R.parent/'serene-theory/validation/outputs_capacity_planner_consumer'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def entry(p,base=R):return {'path':p.relative_to(base).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}
def files(base):return [entry(p,base) for p in sorted(base.rglob('*')) if p.is_file() and '__pycache__' not in p.parts]
def verify(p,e):assert p.stat().st_size==e['bytes'] and sha(p)==e['sha256'],p
assert not Q.exists(),'Use a new seal path; never overwrite qualification.'
checkpoint=R/'history/before_capacity_planner/qualified_0_2_0_20261108';old=load(checkpoint/'manifest.json')
for name,e in old['files'].items():verify(checkpoint/name,e)
prior=R/'build/extensions-msvc/qualification.json'
assert sha(prior)==old['qualification_sha256']=='32e4b55d2321322de2f550486fb7218ed3f8d8c4b7eb9beef9854a8e8e723778'
old_install=load(R/'build/extensions-msvc/install-manifest.json')
assert sha(R/'build/extensions-msvc/install-manifest.json')=='6fa1c36a5a43459ae7da39bd77cb38b896e0ac4c36ca46c53e792d71cee06ac5'
for e in old_install['files']:verify(R/'build/install-extensions'/e['path'],e)
installed=load(R/'build/capacity-planner-msvc/install-manifest.json')
for e in installed['files']:
 verify(R/'build/install-capacity-planner'/e['path'],e);verify(E/'prefix'/e['path'],e)
snapshot=load(R/'docs/codec_extensions_snapshot.json')
for name,digest in snapshot['source_files'].items():assert sha(R/name)==digest,name
source=[]
for directory in ('include','src','cmake','third_party','tests','tools','docs'):
 source.extend(entry(p) for p in sorted((R/directory).rglob('*')) if p.is_file() and '__pycache__' not in p.parts)
source.extend(entry(R/p) for p in ('CMakeLists.txt','README.md','spec/FORMAT.md','spec/FORMAT-1.1.md'))
before={e['path']:e for e in load(prior)['source_files']}
changed=[e for e in source if e['path'] in before and e['sha256']!=before[e['path']]['sha256']]
added=[e for e in source if e['path'] not in before]
assert {e['path'] for e in changed}=={'CMakeLists.txt','README.md','cmake/BUILDING.md','include/serene/outputs/model.hpp','src/model.cpp'}
build=R/'build/capacity-planner-msvc';artifacts=[]
for config in ('Release','Debug'):
 artifacts.extend(entry(p) for p in sorted((build/config).iterdir()) if p.suffix in ('.lib','.exe'))
for d in sorted(build.glob('bundle-tests-*')):artifacts.extend(entry(p) for p in sorted(d.rglob('*')) if p.is_file())
logs=[entry(p) for p in sorted((R/'build').glob('capacity-planner-*.log'))]
assert '100% tests passed' in (R/'build/capacity-planner-release-tests.log').read_text()
assert '100% tests passed' in (R/'build/capacity-planner-debug-tests.log').read_text()
assert (R/'build/capacity-planner-consumer.log').read_text().count('100% tests passed')==2
review=Path(sys.argv[1]) if len(sys.argv)>1 else None
receipt={'schema':1,'status':'qualified','package_version':'0.2.1','scope':'Count/capacity model planner; checked formulas shared with exact preparation; no wire/schema/Python/codec semantic changes',
 'sealed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'api':{'header':entry(R/'include/serene/outputs/model.hpp'),'source':entry(R/'src/model.cpp'),'function':'std::expected<WorkspacePlan,Status> plan_export_capacity(const ExportCapacity&,ExportLimits={}) noexcept','new_types':['DiagnosticCapacity','ExportCapacity'],'added_plan_field':'source_capacity','ownership':'Borrowed count span; no retained state or allocation; actual preparations enforce all ceilings before activation'},
 'source_files':source,'delta_from_qualified_0_2':{'changed':changed,'added':added},
 'preservation':{'checkpoint_manifest':entry(checkpoint/'manifest.json'),'checkpoint_files_verified':len(old['files']),'prior_qualification_sha256':sha(prior),'old_install_manifest_sha256':sha(R/'build/extensions-msvc/install-manifest.json'),'old_installed_files_verified':len(old_install['files']),'shared_codec_files_unchanged':len(snapshot['source_files'])},
 'new_install_manifest':entry(R/'build/capacity-planner-msvc/install-manifest.json'),'build_artifacts':artifacts,'logs':logs,
 'tests':{'Release':'4/4 focused CTest groups passed','Debug':'4/4 focused CTest groups passed','formula_matrix':8,'planning_cpp_allocations':0,'boundaries':['empty counts','exact/one-byte-short metadata/workspace ceilings','2^32 frames','diagnostic u64 byte-bound overflow without clamping','provenance extent overflow','trace applicability/duplicate kind','69 normalized source maximum'],'installed_model_only_consumer':'Release/Debug1/1 each; strict absence of codec/archive imported targets'},
 'consumer_root':str(E),'consumer_source':files(E/'source'),'consumer_executables':[entry(E/f'build/{c}/installed_capacity_planner.exe',E) for c in ('Release','Debug')],
 'commands':['tools/build_capacity_planner.cmd Release','tools/build_capacity_planner.cmd Debug',"ctest --test-dir build/capacity-planner-msvc -C <Release|Debug> -R 'serene_outputs_(capacity_planner|model|archive|bundle)$' --output-on-failure",'cmake --install build/capacity-planner-msvc --config <Release|Debug> --prefix build/install-capacity-planner','tools/qualify_capacity_planner_consumer.cmd'],
 'independent_review':{'absolute_path':str(review),'sha256':sha(review)} if review else None,
 'limitations':['Windows MSVC qualification only.','Count bounds do not replace exact catalog/criteria/profile/JSON or spool parser admission.','No Python packaging, codec-suite rerun, historical audit rerun, commits or pushes.','Do not mix0.2.0/0.2.1 model headers and libraries.']}
Q.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'path':str(Q),'sha256':sha(Q),'changed_files':len(changed),'added_files':len(added),'old_installed_files_verified':len(old_install['files'])}))
