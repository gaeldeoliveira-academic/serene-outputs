#include <serene/outputs/model.hpp>
int main(){
  using namespace serene::outputs;
  auto profile=CodecProfile::fixed_accuracy(1e-4);if(!profile)return 1;
  DiagnosticTraceDescription description;description.kind=DiagnosticKind::run_events;description.record_capacity=(1ull<<40)+1;description.event_names={"external.started"};
  if(!validate_diagnostic_description(description))return 2;
  ArchiveDescription archive;archive.file_id="00000000-0000-4000-8000-000000000010";archive.created_utc="2026-11-08T00:00:00Z";archive.run.run_id="model_only";archive.run.diagnostics.push_back(description);
  auto prepared=PreparedExport::prepare(std::move(archive));return prepared&&prepared->plan().section_capacity>0?0:3;
}
