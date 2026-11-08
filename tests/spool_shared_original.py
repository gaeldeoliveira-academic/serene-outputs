"""Independent valid source-ID sharing case for merger role remapping."""
from pathlib import Path
import hashlib, importlib.util, json, sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('independent_extension_author',root/'tools/conformance/extensions_draft/author.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=False)
c=a.base('shared_original',[a.P]);c.runs=c.runs[:1];c.run_ids=c.run_ids[:1];c.expected=c.expected[:1]
index=c.root['results']['index'];c.replace(index['run_index'],[0],shape=[1]);c.replace(index['kind'],[0],shape=[1])
c.replace_pool(index['run_id'],['transient/α']);c.replace_pool(index['run_name'],['same name'])
c.replace(index['run_name']['present'],[1],shape=[1]);c.replace(index['run_metadata_section'],c.run_ids,shape=[1])
# Role0 original and role1 effective share one disabled source. A different
# spool's included common original must not enable this effective document.
del c.extra[c.source_ids.pop(1)]
for i,sid in enumerate(c.source_ids):c.extra[sid]['source_index']=i
c.extra[c.source_ids[0]]['document']={'state':1}
source=c.root['provenance']['sources'];c.replace(source['source_index'],list(range(5)),shape=[5])
c.replace(source['kind'],[0,0,0,0,1],shape=[5]);c.replace(source['metadata_section'],c.source_ids,shape=[5])
c.replace(c.runs[0]['provenance']['references']['source_index'],[0,0,1,2,3,4])
original=b'{ "fixture": true, "source": "original bytes" }\r\n'
digest=hashlib.sha256(original).digest();c.replace(c.root['provenance']['authored_input_sha256'],digest)
c.root['format']['sections']['inputs']=3;a.compact(c)
blob=a.encode(c);file='shared_original_roles.serene';(out/file).write_bytes(blob)
binding={'file':file,'sha256':hashlib.sha256(blob).hexdigest(),'bytes':len(blob),'authored_sha256':digest.hex(),
         'run_id':'transient/α','run_name':'same name','run_name_present':True,'kind':0,
         'producer_identity':'independent-spool-shared-original/v1','dependency_identity':'public-zfpy1.0.1.post0/pinned-zfp1.0.1',
         'trusted':True,'section_count':len(c.sections),'source_count':5,'original_state':1,
         'expected_merged_role0_state':0,'expected_merged_role1_state':1,
         'author_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(out/'binding.json').write_text(json.dumps(binding,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(binding))
