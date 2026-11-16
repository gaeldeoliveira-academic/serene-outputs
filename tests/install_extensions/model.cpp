#include <serene/outputs/model.hpp>
/** Exercise profile factories and diagnostic planning through the installed 0.2.0 model target; return zero or a failed-stage code. */
int main(){
    // Admit a fixed profile and large authored trace capacity, then prepare a model-only export graph.

  using namespace serene::outputs;
  /* Admitted immutable fixed-accuracy profile. */ auto profile=CodecProfile::fixed_accuracy(1e-4);if(!profile)return 1;
  /* Event trace with authored capacity beyond the retained wire row limit. */ DiagnosticTraceDescription description;description.kind=DiagnosticKind::run_events;description.record_capacity=(1ull<<40)+1;description.event_names={"external.started"};
  if(!validate_diagnostic_description(description))return 2;
  /* Owned metadata for a model-only preparation probe. */ ArchiveDescription archive;archive.file_id="00000000-0000-4000-8000-000000000010";archive.created_utc="2026-11-08T00:00:00Z";archive.run.run_id="model_only";archive.run.diagnostics.push_back(description);
  /* Prepared graph expected to contain sections. */ auto prepared=PreparedExport::prepare(std::move(archive));return prepared&&prepared->plan().section_capacity>0?0:3;
}
