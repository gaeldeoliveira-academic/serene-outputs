#include "synthetic_case.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Case {
    SyntheticCase source;
    std::array<std::vector<DiagnosticRecord>,5> records;
    std::vector<DiagnosticTraceView> traces;
    std::vector<LogsView> logs;
    std::string second_log{"{\"event\":\"second-source\"}\r\n{\"fragment\":"};
    explicit Case(bool is_static=false):source(2,is_static?3:32,is_static?RunKind::steady_state:RunKind::timeseries){
        source.description.writer_version="0.2.0";
        source.limits.provenance_bytes_bound=65536;
        source.info.accepted_count=source.axis.size()-1;
        source.info.last_accepted_value=source.axis.back();source.info.requested_end=source.axis.back();
        if(is_static){source.info.requested_begin=source.axis.front();source.info.identity_axis_convention="accepted_level_zero_is_initial";for(std::size_t i=0;i<source.identities.size();++i)source.identities[i]=i;}
        else for(std::size_t i=0;i<source.axis.size();++i)source.data[2*i+1]=.123456789*i+.3333333333333333;
    }
    void diagnostics(){
        auto&run=source.description.run;const bool stat=run.kind==RunKind::steady_state;
        for(unsigned k=stat?1:0;k<=4;++k){
            if(!stat&&k>0&&k<4)continue;
            DiagnosticTraceDescription d;d.kind=static_cast<DiagnosticKind>(k);d.record_capacity=k==4?2:stat?3:31;
            if(k==1||k==3)d.status_names={"success","numerical_failure"};
            if(k==4)d.event_names={"core.execution_started","core.execution_completed","core.execution_failed"};
            if(k>=1&&k<=3)d.criteria=StaticCriteria{-.25,.01,.01,.001,1.,1.,.011,.011,.001,.001,.001,.001};
            run.diagnostics.push_back(d);auto& rows=records[k];rows.resize(d.record_capacity);
            for(std::size_t i=0;i<rows.size();++i){auto&r=rows[i];r.record_index=i;
                if(k==0){r.accepted_step=i+1;r.time_s=.01*(i+1);r.nonlinear_iterations=i+2;}
                else if(k<=3){r.attempt_id=i+1;r.accepted_level_id=i;r.iteration_id=1;r.newton_record_count=1;r.factor=source.axis[i];r.certified=1;r.accepted_level_present=1;r.metric_valid=63;}
                else{r.sequence=i;r.event_name_index=static_cast<std::uint32_t>(i);r.severity=1;r.identity_valid=stat?(i?12:0):3;if(i){r.accepted_step=stat?0:31;r.time_s=stat?0.:.31;r.attempt_id=stat?3:0;r.accepted_level_id=stat?2:0;}}
            }
            DiagnosticTraceView v;v.kind=d.kind;v.availability=DiagnosticAvailability::included;v.capture_status=DiagnosticCaptureStatus::complete;
            v.records=DiagnosticRecords::memory(rows);v.counts_present=true;v.eligible_count=rows.size();traces.push_back(v);
        }
    }
    IndexedProvenanceView indexed(){
        auto p=source.provenance();IndexedProvenanceView v;
        v.authored_input_sha256=sha256(std::as_bytes(std::span(source.original.data(),source.original.size())));
        v.documents={p.inputs.source_json,p.inputs.effective_json,p.inputs.overrides_json,p.inputs.resources_json,p.context.execution_json};
        logs={p.logs,p.logs};logs[1].bytes=ByteSource::text(second_log);logs[1].state=SectionState::partial;v.logs=logs;return v;
    }
    RunResultView view(){auto v=source.view();v.diagnostics=traces;return v;}
};
void save(Case& c,const std::filesystem::path& directory,std::string name){
    c.source.description.run.run_id=name;
    auto p=PreparedExport::prepare(c.source.description,c.source.limits);check(bool(p),"prepare extension");
    auto codec=Codec::create(c.source.limits.frame_values);check(bool(codec),"codec");
    auto path=directory/(name+".serene");auto session=ArchiveSession::open(*p,*codec,path);check(bool(session),"open extension");
    auto status=c.source.description.indexed_provenance?session->append_run(c.view(),c.indexed()):session->append_run(c.view(),c.source.provenance());
    if(!status)throw std::runtime_error("append extension "+name+" "+std::string(status_name(status.code))+" op"+std::to_string(status.operation));
    status=session->finalize();check(bool(status),"finalize extension");
    check(std::filesystem::file_size(path)<=p->plan().maximum_archive_bytes,"archive bound");
    c.source.expected(directory/name);
}
}
int main(int argc,char**argv){try{
    auto dir=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/("extensions-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!std::filesystem::exists(dir),"fresh output path required");std::filesystem::create_directories(dir);
    for(unsigned mask=1;mask<8;++mask){Case c;
        if(mask&1)c.diagnostics();
        if(mask&2){c.source.description.indexed_provenance=true;c.source.description.maximum_log_sources=2;}
        if(mask&4){auto fixed=CodecProfile::fixed_accuracy(1e-4);check(bool(fixed),"profile");c.source.description.run.profiles={CodecProfile::reversible(),*fixed};c.source.description.run.row_profile_index={0,1};}
        save(c,dir,"features_"+std::to_string(mask));
    }
    Case stat(true);stat.diagnostics();stat.source.description.indexed_provenance=true;stat.source.description.maximum_log_sources=2;save(stat,dir,"static_all_traces");
    auto p=PreparedExport::prepare(stat.source.description,stat.source.limits);check(bool(p)&&p->plan().section_capacity>128,"graph replaces fixed directory cap");
    auto v=stat.view();stat.records[1][0].metric_valid=31;check(!p->validate(v),"certified stage requires all metrics");stat.records[1][0].metric_valid=63;
    stat.records[3][0].metric_valid=31;check(!p->validate(v),"certified Newton requires all metrics");stat.records[3][0].metric_valid=63;
    auto larger=stat.source.description;for(auto&t:larger.run.diagnostics)t.record_capacity=1000000;
    auto large=PreparedExport::prepare(larger,stat.source.limits);check(bool(large)&&large->plan().total_bytes==p->plan().total_bytes&&large->plan().maximum_archive_bytes>p->plan().maximum_archive_bytes,"trace capacity does not retain history in exporter");
    auto oversized_capacity=stat.source.description;for(auto&t:oversized_capacity.run.diagnostics)t.record_capacity=(1ull<<40)+1;
    auto oversized=PreparedExport::prepare(oversized_capacity,stat.source.limits);
    check(bool(oversized)&&bool(oversized->validate(stat.view())),"authored capacity above wire row limit permits small retained population");
    auto impossible=stat.source.description;impossible.run.diagnostics[0].record_capacity=UINT64_MAX;
    auto overflow=PreparedExport::prepare(impossible,stat.source.limits);check(!overflow&&overflow.error().code==StatusCode::resource_limit,"capacity bound overflow is planning resource limit");
    auto bad_retained=stat.view();auto copied_traces=stat.traces;copied_traces[0].records.count=(1ull<<40)+1;bad_retained.diagnostics=copied_traces;
    check(!oversized->validate(bad_retained),"retained count still respects wire row limit");
    auto small_limits=stat.source.limits;small_limits.workspace_budget_bytes=p->plan().total_bytes-1;check(!PreparedExport::prepare(stat.source.description,small_limits),"workspace rejects before export");
    auto wrong=stat.source.description;wrong.run.profiles={*CodecProfile::fixed_accuracy(.01)};check(!PreparedExport::prepare(wrong,stat.source.limits),"static fixed forbidden");
    Case mismatch;mismatch.source.description.indexed_provenance=true;mismatch.source.description.maximum_log_sources=2;
    auto pp=PreparedExport::prepare(mismatch.source.description,mismatch.source.limits);auto cc=Codec::create(4);auto ss=ArchiveSession::open(*pp,*cc,dir/"must_not_publish.serene");auto provenance=mismatch.indexed();provenance.authored_input_sha256={};
    check(ss->append_run(mismatch.view(),provenance).code==StatusCode::verification_failure,"included original digest checked");check(!std::filesystem::exists(dir/"must_not_publish.serene"),"failure does not publish");
    std::cout<<"8 extension archives, graph/admission/validity/original-digest checks passed: "<<dir.string()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
