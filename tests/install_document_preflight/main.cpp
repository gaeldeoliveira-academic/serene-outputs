#include <serene/outputs/document.hpp>
#include <iostream>
/** Exercise installed 0.2.2 count planning and document preflight; return zero or the failing consumer-stage code. */
int main(){
    // Plan capacities, admit complete JSON, reject a parser-budget shortage, then check partial/inactive byte semantics.

    using namespace serene::outputs;
    /* Count-only input for the exported model API. */ ExportCapacity capacity;capacity.scalar_capacity=16;capacity.record_capacity=100;capacity.metadata_bytes_bound=1024*1024;/* Successful installed count planner result. */ auto plan=plan_export_capacity(capacity);if(!plan)return 1;
    /* Borrowed JSON document reused for complete/partial/inactive cases. */ DocumentView document{SectionState::included,ByteSource::text("{\"key\":[0,1]}"),"application/json"};/* Complete JSON admission and parser-charge result. */ auto preflight=preflight_document(document);if(!preflight||preflight->kind!=DocumentPreflightKind::json_document||!preflight->parser_charge_bytes)return 2;
    /* Limits reduced below the reported parser charge. */ auto limited=DocumentPreflightLimits{};limited.document_parser_budget_bytes=preflight->parser_charge_bytes-1;/* Expected parser resource-limit outcome. */ auto denied=preflight_document(document,limited);if(denied||denied.error().code!=StatusCode::resource_limit)return 3;
    document.state=SectionState::partial;document.bytes=ByteSource::text("{incomplete");/* Partial bytes admitted without complete JSON parsing. */ auto partial=preflight_document(document);if(!partial||partial->kind!=DocumentPreflightKind::retained_bytes||partial->parser_charge_bytes)return 4;
    /* Callback-invocation sentinel for inactive documents. */ bool called=false;document.state=SectionState::disabled;document.bytes={{},&called,[](const void*p,std::uint64_t,std::span<std::byte>)noexcept{ /* Mark unexpected callback execution and return the injected failure. */ *const_cast<bool*>(static_cast<const bool*>(p))=true;return error(StatusCode::io_failure,99);},0};/* Disabled-document result that must not invoke its byte source. */ auto inactive=preflight_document(document);if(!inactive||inactive->kind!=DocumentPreflightKind::inactive||called)return 5;
    std::cout<<"Installed combined count planner and bounded document preflight passed\n";return 0;
}
