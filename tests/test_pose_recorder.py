"""Run C++ recorder checks and independently validate the emitted sidecar bytes.

Arguments name the compiled --exe, repository --source and --output-root.
Each invocation creates a unique owned output directory; complete publications
and retained partial files are checked separately. Assertions raise on a
semantic/readback mismatch, and the final JSON records this invocation only.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import uuid
# Parser for the executable, source checkout and fresh-output parent arguments.
p=argparse.ArgumentParser()
p.add_argument('--exe', required=True)
p.add_argument('--source', required=True)
p.add_argument('--output-root', required=True)
# Parsed command-line paths owned by this invocation.
a=p.parse_args()
# Repository root locating the independent checker and fixture catalog.
root=Path(a.source)
# Load the maintained independent checker from the explicitly selected checkout.
sys.path.insert(0,str(root/'tools/pose_sidecar'))
from check_sidecar import check
# Unique run directory; the C++ executable creates the final child itself.
out=Path(a.output_root)/('run-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+uuid.uuid4().hex[:8])
out.parent.mkdir(parents=True,exist_ok=True)
# First run the actual C++ writer/fault scenarios; stop if its assertions fail.
subprocess.run([a.exe,str(out),str(root/'fixtures_pose_sidecar/catalog.json')],check=True)
# Independent expected poses/stamps, read without rewriting fixture bytes.
expected=json.loads((root/'fixtures_pose_sidecar/expected.json').read_text())
# Owned readback evidence keyed by scenario name.
results={}
# Check completed sidecars; name chooses one writer scenario and selective frame dumps.
for name in ['transient','static','prepared_then_transient','no_frames','empty_population','byte_limit','long_bounded']:
    results[name]=check(out/(name+'.spose'),dump_frames=name in ('transient','static','prepared_then_transient'))
assert results['transient']['frames']==expected['transient_frames'], 'actual writer differs from independent expected poses/stamps'
assert results['transient']['complete'] and results['static']['complete']
assert results['prepared_then_transient']['frames'][1]['stamp']['events']==14
# Check recoverable partial prefixes without claiming final publication.
for name in ['abandoned','fault_1','fault_2','short_write','invalid_pose','invalid_quaternion','invalid_stamp','after_target']:
    # Owned incomplete-file validation result; its complete flag must remain false.
    result=check(out/(name+'.spose.partial'),allow_incomplete=True)
    assert not result['complete'],name
    results[name]=result
# Late close/publication failures retain a complete temporary file, still unpublished.
for name in ['fault_3','fault_4','collision']:
    # Owned complete-temporary validation result for the failed publication.
    result=check(out/(name+'.spose.partial'))
    assert result['complete'],name
    results[name]=result
# Write only this run's independent-readback evidence after all assertions pass.
(out/'independent-readback.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('independent writer readback passed:',out)
