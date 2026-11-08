"""Seal the qualified0.2 Windows source/install/artifact closure without changing artifacts."""
from pathlib import Path
import hashlib,json,datetime

ROOT=Path(__file__).resolve().parents[1]
EXTERNAL=ROOT.parent/'serene-theory/validation/outputs_extensions_consumer'
DEST=ROOT/'build/extensions-msvc/qualification.json'
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''): h.update(chunk)
    return h.hexdigest()
def record(path,base=ROOT):
    return {'path':path.relative_to(base).as_posix(),'bytes':path.stat().st_size,'sha256':sha(path)}
def tree(base):
    return [record(p,base) for p in sorted(base.rglob('*')) if p.is_file() and '__pycache__' not in p.parts]
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def verify_record(path,entry):
    assert path.stat().st_size==entry['bytes'] and sha(path)==entry['sha256'],path

assert not DEST.exists(),'A qualification seal must use a new path.'
checkpoint=ROOT/'history/before_extensions/accepted_0_1_20261108'
old=load(checkpoint/'manifest.json');protected=0
for relative,entry in old['files'].items():
    verify_record(checkpoint/relative,entry)
    if relative=='spec/FORMAT.md' or relative.startswith(('fixtures_custom/','fixtures_extensions_draft/','spec/drafts/native-1.1/','build/install-custom/','build/g1-custom','build/n04-custom','docs/n0')):
        verify_record(ROOT/relative,entry);protected+=1
snapshot=load(ROOT/'docs/codec_extensions_snapshot.json')
for relative,digest in snapshot['source_files'].items():assert sha(ROOT/relative)==digest,relative
publication=load(ROOT/'docs/native_extensions_spec_publication.json')
for key in ('candidate','published'):assert sha(ROOT/publication[key])==publication[key+'_sha256']
candidate=(ROOT/publication['candidate']).read_bytes();published=(ROOT/publication['published']).read_bytes()
separator=b'\r\n\r\n' if b'\r\n\r\n' in candidate else b'\n\n'
assert candidate.split(separator,2)[2]==published.split(separator,2)[2]
installed=load(ROOT/'build/extensions-msvc/install-manifest.json')
for item in installed['files']:
    verify_record(ROOT/'build/install-extensions'/item['path'],item)
    verify_record(EXTERNAL/'prefix'/item['path'],item)

source=[]
for folder in ('include','src','cmake','third_party','tests','tools','docs'):
    source.extend(record(p) for p in sorted((ROOT/folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts)
source.extend(record(ROOT/p) for p in ('CMakeLists.txt','README.md','spec/FORMAT.md','spec/FORMAT-1.1.md'))
artifact_dirs=['build/extensions-writer-final-release','build/extensions-writer-final-debug','build/extensions-bundle-final-release','build/extensions-bundle-final-debug','build/extensions-memory-release','build/extensions-memory-debug','build/extensions-spool-fixtures-final','build/extensions-spool-shared-original-v2']
artifacts={d:tree(ROOT/d) for d in artifact_dirs}
readers={c:load(ROOT/f'build/extensions-spool-fixtures-final/reader-receipt-{c}.json') for c in ('Release','Debug')}
assert all(r['passed']==85 and r['failed']==0 and r['history_growth_bounded'] for r in readers.values())
bundles={c:load(ROOT/f'build/extensions-bundle-final-{c.lower()}/receipt.json') for c in ('Release','Debug')}
assert all(r['status']=='passed' and r['fault_cases']==8 and r['measured_peak_cpp_bytes']<=r['workspace_bytes'] for r in bundles.values())
consumer=[]
for component in ('model','codec','archive'):
    for configuration in ('Release','Debug'):
        exe=EXTERNAL/f'build-{component}/{configuration}/installed_extensions.exe'
        consumer.append({'component':component,'configuration':configuration,'result':'passed','executable':record(exe,EXTERNAL)})
assert all(sha(EXTERNAL/'source'/p.name)==sha(p) for p in (ROOT/'tests/install_extensions').iterdir() if p.is_file())
build=ROOT/'build/extensions-msvc'
buildfiles=[record(p) for p in sorted(build.iterdir()) if p.is_file() and (p.suffix=='.log' or p.name in ('CMakeCache.txt','install-manifest.json'))]
for config in ('Release','Debug'):
    buildfiles.extend(record(p) for p in sorted((build/config).iterdir()) if p.suffix in ('.lib','.exe'))
external_receipts=[]
for relative in ('cpp-original.json','cpp-rebuilt.json'):
    p=ROOT.parent/'serene-outputs-python/build/native-1.1-final'/relative
    external_receipts.append({'absolute_path':str(p),'sha256':sha(p),'bytes':p.stat().st_size})
result={
 'schema':1,'status':'qualified','package_version':'0.2.0','platform':'Windows x64 MSVC19.50 / Visual Studio18 2026','configurations':['Release /MD','Debug /MDd'],
 'sealed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'contract_acceptance':{'path':'../serene-theory/validation/native_output_extensions_contract_acceptance.json','sha256':'84147366e8534197fa1063db769b0060e3966aebfdc4f35757906bf342248c6b'},
 'checkpoint':{'path':str(checkpoint),'sha256':sha(checkpoint/'manifest.json'),'verified_files':len(old['files']),'protected_live_files_verified':protected},
 'source_files':source,'installed_manifest':record(build/'install-manifest.json'),'build_files':buildfiles,'artifacts':artifacts,
 'source_snapshot':record(ROOT/'docs/codec_extensions_snapshot.json'),'spec_publication':publication,
 'dependency':{'path':'build/dependencies-extensions/zfp-1.0.1.tar.gz','sha256':sha(ROOT/'build/dependencies-extensions/zfp-1.0.1.tar.gz'),'origin':'Exact pinned archive copied from outputs-python/vendor after former CFD cache path disappeared; no pin/revision change.'},
 'checks':{'writer_cases_per_configuration':8,'validator_checks_per_configuration':85,'bundle_fault_cases_per_configuration':8,'legacy_model_archive_regressions':'2/2 each configuration','codec':'Reused exact stable codec-owner qualification','external_consumers':consumer,'reader_memory':{c:{k:r[k] for k in ('small_heap_peak','large_heap_peak','small_workspace','large_workspace')} for c,r in readers.items()},'bundle_memory':{c:{k:r[k] for k in ('measured_peak_cpp_bytes','workspace_bytes','directory_bytes','catalog_bytes','spool_workspace_bytes','maximum_archive_bytes')} for c,r in bundles.items()}},
 'external_consumer_root':str(EXTERNAL),'external_consumer_source':tree(EXTERNAL/'source'),'independent_installed_python_receipts':external_receipts,
 'commands':['tools/build_extensions.cmd Release','tools/build_extensions.cmd Debug','test_extensions <fresh-dir> (Release,Debug)','test_bundle <fresh-dir> build/extensions-spool-shared-original-v2/binding.json (Release,Debug)','test_spool_reader build/extensions-spool-fixtures-final/manifest.json (Release,Debug)','measure_extensions 32 <fresh-file>; measure_extensions 65536 <fresh-file> (Release,Debug)','ctest -R serene_outputs_(model|archive)$ (Release,Debug)','cmake --install build/extensions-msvc --config <Release|Debug> --prefix build/install-extensions','tools/qualify_extensions_consumer.cmd; corrected consumer static method/required assembly metadata, rebuilt and passed archive Release/Debug'],
 'limitations':['Windows qualification only; Linux/macOS not run.','Initial merger requires trusted finalized single-run manifests; partial-original and multi-run imports are unsupported capabilities.','Memory figures are requested C++ live allocations, not allocator/process RSS.','Core solve/event/manifest generation acceptance is separate. No commits, pushes, GUI/HDF5 changes or remote publication.']}
DEST.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'path':str(DEST),'sha256':sha(DEST),'source_files':len(source),'artifact_files':sum(map(len,artifacts.values())),'protected_live_files_verified':protected}))
