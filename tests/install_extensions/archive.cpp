#include <serene/outputs/bundle.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
int main(){
  using namespace serene::outputs;
  std::string original="{\"installed_consumer\":true}";auto digest=sha256(std::as_bytes(std::span(original.data(),original.size())));
  ArchiveDescription d;d.file_id="00000000-0000-4000-8000-000000000011";d.created_utc="2026-11-08T00:00:00Z";d.run.run_id="installed_extensions";d.run.assembly_ref="blade";d.run.record_capacity=3;d.indexed_provenance=true;d.maximum_log_sources=0;
  d.run.requests.push_back({"tip","position","3.0-wip.4","1.0","{}"});d.run.channels.push_back({"tip.position.x","tip","position","x","m",0});auto fixed=CodecProfile::fixed_accuracy(1e-4);if(!fixed)return 1;d.run.profiles={*fixed};
  ExportLimits limits;limits.frame_values=4;limits.provenance_bytes_bound=4096;auto prepared=PreparedExport::prepare(d,limits);auto codec=Codec::create(4);if(!prepared||!codec)return 2;
  std::array<double,3>axis={0.,.04,.08},values={.3333333333333333,.123456789,.987654321};RunResultView run;run.axis=axis;run.values=*ValueView::strided(values,1,3,3,1);run.info.accepted_count=2;run.info.last_accepted_present=true;run.info.last_accepted_value=.08;run.info.requested_end=.08;
  IndexedProvenanceView provenance;provenance.authored_input_sha256=digest;provenance.documents[0]={SectionState::included,ByteSource::text(original),"application/json"};
  auto dir=std::filesystem::current_path()/("installed-extensions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);auto spoolpath=dir/"spool.serene";
  {auto session=ArchiveSession::open(*prepared,*codec,spoolpath);if(!session){std::cerr<<"open "<<status_name(session.error().code)<<' '<<session.error().operation<<'\n';return 3;}auto status=session->append_run(run,provenance);if(status)status=session->finalize();if(!status){std::cerr<<"spool "<<status_name(status.code)<<' '<<status.operation<<'\n';return 3;}}
  Sha256 hash;std::ifstream file(spoolpath,std::ios::binary);std::array<char,4096>tile{};while(file){file.read(tile.data(),tile.size());hash.update(std::as_bytes(std::span(tile.data(),static_cast<std::size_t>(file.gcount()))));}if(!file.eof())return 4;
  SpoolBinding binding{hash.finish(),digest,std::filesystem::file_size(spoolpath),d.run.run_id,"installed-consumer-0.2","pinned-zfp-1.0.1",true};auto spool=VerifiedSpool::open(spoolpath,binding);if(!spool)return 5;
  BundleDescription bundle;bundle.file_id="00000000-0000-4000-8000-000000000012";bundle.created_utc=d.created_utc;bundle.authored_input_sha256=digest;bundle.original_input=provenance.documents[0];bundle.runs.push_back({d.run.run_id,"",false,RunKind::timeseries,prepared->plan().section_capacity,prepared->plan().maximum_archive_bytes,5});
  auto plan=PreparedBundle::prepare(std::move(bundle));if(!plan)return 6;auto out=BundleSession::open(*plan,dir/"bundle.serene",{false,true});if(!out||!out->append_verified_spool(0,*spool)||!out->finalize())return 7;
  std::cout<<"Installed fixed-profile spool and verified bundle: "<<dir.string()<<'\n';return 0;
}
