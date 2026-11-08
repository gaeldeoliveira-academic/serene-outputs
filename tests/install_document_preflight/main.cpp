#include <serene/outputs/document.hpp>
#include <iostream>
int main(){
    using namespace serene::outputs;
    ExportCapacity capacity;capacity.scalar_capacity=16;capacity.record_capacity=100;capacity.metadata_bytes_bound=1024*1024;auto plan=plan_export_capacity(capacity);if(!plan)return 1;
    DocumentView document{SectionState::included,ByteSource::text("{\"key\":[0,1]}"),"application/json"};auto preflight=preflight_document(document);if(!preflight||preflight->kind!=DocumentPreflightKind::json_document||!preflight->parser_charge_bytes)return 2;
    auto limited=DocumentPreflightLimits{};limited.document_parser_budget_bytes=preflight->parser_charge_bytes-1;auto denied=preflight_document(document,limited);if(denied||denied.error().code!=StatusCode::resource_limit)return 3;
    document.state=SectionState::partial;document.bytes=ByteSource::text("{incomplete");auto partial=preflight_document(document);if(!partial||partial->kind!=DocumentPreflightKind::retained_bytes||partial->parser_charge_bytes)return 4;
    bool called=false;document.state=SectionState::disabled;document.bytes={{},&called,[](const void*p,std::uint64_t,std::span<std::byte>)noexcept{*const_cast<bool*>(static_cast<const bool*>(p))=true;return error(StatusCode::io_failure,99);},0};auto inactive=preflight_document(document);if(!inactive||inactive->kind!=DocumentPreflightKind::inactive||called)return 5;
    std::cout<<"Installed combined count planner and bounded document preflight passed\n";return 0;
}
