"""Hash final review artifacts and confirm accepted source/install preservation."""
from pathlib import Path
import hashlib, importlib.metadata, json, platform, sys
ROOT=Path(__file__).resolve().parents[3]; DEST=ROOT/'spec/drafts/native-1.1'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((ROOT/'docs/custom_source_snapshot.json').read_text())['files']
install=json.loads((ROOT/'build/n04-custom-cmake-evidence.json').read_text())['installed_files']
source_bad=[p for p,h in source.items() if digest(ROOT/p)!=h]
install_bad=[p for p,v in install.items() if digest(ROOT/'build/install-custom'/p)!=v['sha256']]
assert not source_bad and not install_bad,(source_bad,install_bad)
assert digest(ROOT/'spec/FORMAT.md')=='1ac724b78d1b61fc289dd68489a181dbd92bd9f8f08d1684c5b27aa8503ff66c'
assert digest(ROOT/'fixtures_custom/manifest.json')=='910ab1b766e79e015a78d4060db31d84b2b59eb265c904d1b7361493a7ddbf7a'
paths=[DEST/'FORMAT-1.1-candidate.md',DEST/'FORMAT-1.0-baseline.md',DEST/'inputs.json',ROOT/'fixtures_extensions_draft/manifest.json',ROOT/'fixtures_extensions_draft/audit.json']
paths+=sorted((ROOT/'tools/conformance/extensions_draft').glob('*.py'))+[ROOT/'tools/conformance/extensions_draft/README.md']
audit=json.loads((ROOT/'fixtures_extensions_draft/audit.json').read_text())
record={'schema':1,'status':'candidate complete; awaiting independent reader review and root freeze/release',
 'artifacts':{p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in paths},
 'counts':audit['counts'],'sections':audit['sections'],'frames':audit['frames'],'bundle_binding_cases':len(audit['bundle_binding_cases']),
 'preserved':{'accepted_source_files_checked':len(source),'accepted_installed_files_checked':len(install),'mismatches':[],'format_1_0':digest(ROOT/'spec/FORMAT.md'),'fixture_manifest_1_0':digest(ROOT/'fixtures_custom/manifest.json')},
 'toolchain':{'platform':platform.platform(),'python':sys.version,'executable':sys.executable,'numpy':importlib.metadata.version('numpy'),'zfpy':importlib.metadata.version('zfpy')},
 'commands':['eic12/python.exe tools/conformance/extensions_draft/author.py','eic12/python.exe tools/conformance/extensions_draft/audit.py','eic12/python.exe tools/conformance/extensions_draft/write_draft.py','eic12/python.exe tools/conformance/extensions_draft/seal.py'],
 'limitations':['Independent tiny-corpus tooling only; no production library or installed extension implementation qualification.','Audit loads tiny artifacts eagerly; phase expectations do not prove measured lazy I/O, hostile input bounds or RSS.','New CMake/MSVC /MD and /MDd builds and fresh prefixes belong to subsequent implementation release; accepted build/install-custom untouched.','No commits, pushes, package/site publication or GUI changes.'],
 'unresolved_wire_questions':[],
 'implementation_details_for_followup':['Set exact C++ plan/API type names jointly with core under checked section/workspace/archive bounds.','Implement and fault-test generation/reference replacement under Windows exclusive ownership; other platforms remain unqualified.','Qualify real core histories, allocations, failed-publication retry, codec snapshot handoff and fresh installed feature combinations.']}
(DEST/'candidate_handoff.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'handoff_sha256':digest(DEST/'candidate_handoff.json'),'candidate_sha256':digest(DEST/'FORMAT-1.1-candidate.md'),'manifest_sha256':digest(ROOT/'fixtures_extensions_draft/manifest.json'),'audit_sha256':digest(ROOT/'fixtures_extensions_draft/audit.json'),'preserved_sources':len(source),'preserved_installed':len(install)}))
