#include "synthetic_case.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
namespace {
/** Throw borrowed message when ok is false. */
void check(bool ok,const char* message){
    // Route a failed fixture or admission assertion through main's exception handler.
if(!ok)throw std::runtime_error(message);}
/** Owned extension scenario used by save and main. Synthetic values, typed diagnostic rows and log strings back borrowed views; prepare/append must finish before these owners are mutated or destroyed. */ struct Case {
/** Owned base metadata, numeric histories and provenance bytes. */     SyntheticCase source;
/** Per-kind owned diagnostic rows indexed by wire DiagnosticKind. */     std::array<std::vector<DiagnosticRecord>,5> records;
/** Borrowed views over the corresponding records arrays. */     std::vector<DiagnosticTraceView> traces;
/** Log descriptors rebuilt by indexed(); bytes remain in source or second_log. */     std::vector<LogsView> logs;
/** Second log source ending with a deliberately partial JSON record. */     std::string second_log{"{\"event\":\"second-source\"}\r\n{\"fragment\":"};
    /** Construct a two-channel owned extension fixture. is_static selects accepted-level metadata; otherwise values include nontrivial fixed-accuracy candidates. */
explicit Case(bool is_static=false):source(2,is_static?3:32,is_static?RunKind::steady_state:RunKind::timeseries){
    // Create the matching static/transient SyntheticCase and align accepted counts with retained initial states.
    // Normalize static accepted identities or fill transient values that exercise lossy compression.

        source.description.writer_version="0.2.0";
        source.limits.provenance_bytes_bound=65536;
        source.info.accepted_count=source.axis.size()-1;
        source.info.last_accepted_value=source.axis.back();source.info.requested_end=source.axis.back();
        if(is_static){source.info.requested_begin=source.axis.front();source.info.identity_axis_convention="accepted_level_zero_is_initial";for(/* Record or sample index in the selected owned buffer. */ std::size_t i=0;i<source.identities.size();++i)source.identities[i]=i;}
        else for(/* Record or sample index in the selected owned buffer. */ std::size_t i=0;i<source.axis.size();++i)source.data[2*i+1]=.123456789*i+.3333333333333333;
    }
    /** Populate owned declarations, rows and borrowed trace views for every applicable kind. Called once per fixture before preparation; views require records to remain stable. */
void diagnostics(){
    // Choose applicable trace kinds, dictionaries, capacities and immutable static criteria.
    // Fill each typed row with valid identity/metric fields, then bind a complete captured trace view.

        /* Run description receiving diagnostic declarations. */ auto&run=source.description.run;/* Whether this fixture uses static accepted-level semantics. */ const bool stat=run.kind==RunKind::steady_state;
        for(/* Wire diagnostic-kind code, skipping inapplicable transient/static kinds. */ unsigned k=stat?1:0;k<=4;++k){
            if(!stat&&k>0&&k<4)continue;
            /* Owned declaration for this kind's capacity and dictionaries. */ DiagnosticTraceDescription d;d.kind=static_cast<DiagnosticKind>(k);d.record_capacity=k==4?2:stat?3:31;
            if(k==1||k==3)d.status_names={"success","numerical_failure"};
            if(k==4)d.event_names={"core.execution_started","core.execution_completed","core.execution_failed"};
            if(k>=1&&k<=3)d.criteria=StaticCriteria{-.25,.01,.01,.001,1.,1.,.011,.011,.001,.001,.001,.001};
            run.diagnostics.push_back(d);/* Owned row vector for the current diagnostic kind. */ auto& rows=records[k];rows.resize(d.record_capacity);
            for(/* Record or sample index in the selected owned buffer. */ std::size_t i=0;i<rows.size();++i){/* Current typed diagnostic row. */ auto&r=rows[i];r.record_index=i;
                if(k==0){r.accepted_step=i+1;r.time_s=.01*(i+1);r.nonlinear_iterations=i+2;}
                else if(k<=3){r.attempt_id=i+1;r.accepted_level_id=i;r.iteration_id=1;r.newton_record_count=1;r.factor=source.axis[i];r.certified=1;r.accepted_level_present=1;r.metric_valid=63;}
                else{r.sequence=i;r.event_name_index=static_cast<std::uint32_t>(i);r.severity=1;r.identity_valid=stat?(i?12:0):3;if(i){r.accepted_step=stat?0:31;r.time_s=stat?0.:.31;r.attempt_id=stat?3:0;r.accepted_level_id=stat?2:0;}}
            }
            /* Complete captured trace borrowing the filled row vector. */ DiagnosticTraceView v;v.kind=d.kind;v.availability=DiagnosticAvailability::included;v.capture_status=DiagnosticCaptureStatus::complete;
            v.records=DiagnosticRecords::memory(rows);v.counts_present=true;v.eligible_count=rows.size();traces.push_back(v);
        }
    }
    /** Return indexed provenance borrowing source documents and this object's retained log vector. Rebuilds two log associations, including a partial second source. */
IndexedProvenanceView indexed(){
    // Bind five document roles and their authored-input digest.
    // Build two stable log sources and return a view that borrows them.

        /* Borrowed legacy provenance view or owned prepared exporter, according to scope. */ auto p=source.provenance();/* Indexed role/source view returned to append. */ IndexedProvenanceView v;
        v.authored_input_sha256=sha256(std::as_bytes(std::span(source.original.data(),source.original.size())));
        v.documents={p.inputs.source_json,p.inputs.effective_json,p.inputs.overrides_json,p.inputs.resources_json,p.context.execution_json};
        logs={p.logs,p.logs};logs[1].bytes=ByteSource::text(second_log);logs[1].state=SectionState::partial;v.logs=logs;return v;
    }
    /** Return a source result view augmented with borrowed diagnostic traces; valid while this Case storage remains unchanged. */
RunResultView view(){
    // Copy the source result view, attach this fixture's trace span, and return it.
/* Borrowed result view copied before adding diagnostics. */ auto v=source.view();v.diagnostics=traces;return v;}
};
/** Write mutable fixture c as name under fresh directory, using indexed or legacy provenance as declared. Updates run_id, checks the planned file bound and writes independent expected sidecars. */
void save(Case& c,const std::filesystem::path& directory,std::string name){
    // Assign run identity and prepare the exporter and codec.
    // Append the selected provenance form, finalize without clobbering, check file size and emit sidecars.

    c.source.description.run.run_id=name;
    /* Borrowed legacy provenance view or owned prepared exporter, according to scope. */ auto p=PreparedExport::prepare(c.source.description,c.source.limits);check(bool(p),"prepare extension");
    /* Codec sized to the fixture frame capacity. */ auto codec=Codec::create(c.source.limits.frame_values);check(bool(codec),"codec");
    /* New final archive pathname. */ auto path=directory/(name+".serene");/* Owning archive write session. */ auto session=ArchiveSession::open(*p,*codec,path);check(bool(session),"open extension");
    /* Append outcome; reused for final publication status. */ auto status=c.source.description.indexed_provenance?session->append_run(c.view(),c.indexed()):session->append_run(c.view(),c.source.provenance());
    if(!status)throw std::runtime_error("append extension "+name+" "+std::string(status_name(status.code))+" op"+std::to_string(status.operation));
    status=session->finalize();check(bool(status),"finalize extension");
    check(std::filesystem::file_size(path)<=p->plan().maximum_archive_bytes,"archive bound");
    c.source.expected(directory/name);
}
}
/** Write feature-combination archives and test extension admission boundaries. argv optionally names a new directory; returns zero on success or one on exception. */
int main(int argc,char**argv){
    // Write every nonempty subset of the three extension features plus all static diagnostic kinds.
    // Probe certified-row validity, authored-versus-retained capacity and workspace/overflow admission.
    // Reject static fixed accuracy and an included original whose digest disagrees with its binding.
try{
    /* Fresh output directory for this invocation. */ auto dir=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/("extensions-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!std::filesystem::exists(dir),"fresh output path required");std::filesystem::create_directories(dir);
    for(/* Nonempty feature subset: diagnostics, indexed provenance, fixed accuracy. */ unsigned mask=1;mask<8;++mask){/* Fresh transient fixture for this feature combination. */ Case c;
        if(mask&1)c.diagnostics();
        if(mask&2){c.source.description.indexed_provenance=true;c.source.description.maximum_log_sources=2;}
        if(mask&4){/* Admitted fixed-accuracy profile for the second channel. */ auto fixed=CodecProfile::fixed_accuracy(1e-4);check(bool(fixed),"profile");c.source.description.run.profiles={CodecProfile::reversible(),*fixed};c.source.description.run.row_profile_index={0,1};}
        save(c,dir,"features_"+std::to_string(mask));
    }
    /* Static fixture with all applicable diagnostic kinds. */ Case stat(true);stat.diagnostics();stat.source.description.indexed_provenance=true;stat.source.description.maximum_log_sources=2;save(stat,dir,"static_all_traces");
    /* Borrowed legacy provenance view or owned prepared exporter, according to scope. */ auto p=PreparedExport::prepare(stat.source.description,stat.source.limits);check(bool(p)&&p->plan().section_capacity>128,"graph replaces fixed directory cap");
    /* Borrowed result view copied before adding diagnostics. */ auto v=stat.view();stat.records[1][0].metric_valid=31;check(!p->validate(v),"certified stage requires all metrics");stat.records[1][0].metric_valid=63;
    stat.records[3][0].metric_valid=31;check(!p->validate(v),"certified Newton requires all metrics");stat.records[3][0].metric_valid=63;
    /* Copy whose authored trace capacities are enlarged without retaining rows. */ auto larger=stat.source.description;for(/* Trace declaration whose capacity is adjusted. */ auto&t:larger.run.diagnostics)t.record_capacity=1000000;
    /* Plan for enlarged authored capacities. */ auto large=PreparedExport::prepare(larger,stat.source.limits);check(bool(large)&&large->plan().total_bytes==p->plan().total_bytes&&large->plan().maximum_archive_bytes>p->plan().maximum_archive_bytes,"trace capacity does not retain history in exporter");
    /* Description with capacities beyond the wire retained-row limit. */ auto oversized_capacity=stat.source.description;for(/* Trace declaration whose capacity is adjusted. */ auto&t:oversized_capacity.run.diagnostics)t.record_capacity=(1ull<<40)+1;
    /* Expected valid plan for small retained populations under large authored capacities. */ auto oversized=PreparedExport::prepare(oversized_capacity,stat.source.limits);
    check(bool(oversized)&&bool(oversized->validate(stat.view())),"authored capacity above wire row limit permits small retained population");
    /* Description with an unrepresentable upper byte bound. */ auto impossible=stat.source.description;impossible.run.diagnostics[0].record_capacity=UINT64_MAX;
    /* Expected checked-planning resource-limit failure. */ auto overflow=PreparedExport::prepare(impossible,stat.source.limits);check(!overflow&&overflow.error().code==StatusCode::resource_limit,"capacity bound overflow is planning resource limit");
    /* Result view whose retained trace population is made invalid. */ auto bad_retained=stat.view();/* Owned descriptor copy used without mutating the valid fixture. */ auto copied_traces=stat.traces;copied_traces[0].records.count=(1ull<<40)+1;bad_retained.diagnostics=copied_traces;
    check(!oversized->validate(bad_retained),"retained count still respects wire row limit");
    /* Workspace budget one byte below the valid plan. */ auto small_limits=stat.source.limits;small_limits.workspace_budget_bytes=p->plan().total_bytes-1;check(!PreparedExport::prepare(stat.source.description,small_limits),"workspace rejects before export");
    /* Static description given a forbidden fixed-accuracy profile. */ auto wrong=stat.source.description;wrong.run.profiles={*CodecProfile::fixed_accuracy(.01)};check(!PreparedExport::prepare(wrong,stat.source.limits),"static fixed forbidden");
    /* Indexed fixture with intentionally wrong authored-input digest. */ Case mismatch;mismatch.source.description.indexed_provenance=true;mismatch.source.description.maximum_log_sources=2;
    /* Prepared digest-mismatch fixture. */ auto pp=PreparedExport::prepare(mismatch.source.description,mismatch.source.limits);/* Codec for the digest-mismatch writer. */ auto cc=Codec::create(4);/* Unpublished session expected to fail before commit. */ auto ss=ArchiveSession::open(*pp,*cc,dir/"must_not_publish.serene");/* Borrowed indexed provenance whose authored binding is corrupted. */ auto provenance=mismatch.indexed();provenance.authored_input_sha256={};
    check(ss->append_run(mismatch.view(),provenance).code==StatusCode::verification_failure,"included original digest checked");check(!std::filesystem::exists(dir/"must_not_publish.serene"),"failure does not publish");
    std::cout<<"8 extension archives, graph/admission/validity/original-digest checks passed: "<<dir.string()<<'\n';return 0;
}catch(/* First failing assertion or writer error. */ const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
