#include "json_contract.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#include <serene/outputs/model.hpp>
#include <set>
#include <utility>
namespace serene::outputs {
namespace {
bool valid_uuid(std::string_view value) noexcept {
    if (value.size() != 36)
        return false;
    for (std::size_t j = 0; j < value.size(); j++) {
        if (j == 8 || j == 13 || j == 18 || j == 23) {
            if (value[j] != '-')
                return false;
        } else if (!((value[j] >= '0' && value[j] <= '9') || (value[j] >= 'a' && value[j] <= 'f')))
            return false;
    }
    return true;
}
bool valid_utc(std::string_view s) noexcept {
    if (s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':' || s.back() != 'Z')
        return false;
    auto number = [&](std::size_t start, std::size_t n) {
        int v = 0;
        for (std::size_t j = 0; j < n; j++) {
            char c = s[start + j];
            if (c < '0' || c > '9')
                return -1;
            v = 10 * v + c - '0';
        }
        return v;
    };
    int y = number(0, 4), m = number(5, 2), d = number(8, 2), hh = number(11, 2),
        mm = number(14, 2), ss = number(17, 2);
    if (y < 0 || m < 1 || m > 12 || d < 1 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 ||
        ss > 59)
        return false;
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (d > maximum)
        return false;
    if (s.size() > 20) {
        if (s[19] != '.' || s.size() < 22)
            return false;
        for (std::size_t j = 20; j < s.size() - 1; j++)
            if (s[j] < '0' || s[j] > '9')
                return false;
    }
    return true;
}
} // namespace
bool valid_utf8(std::string_view s, bool forbid_nul) noexcept {
    if (s.size() > 1048576)
        return false;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 128) {
            if (!c && forbid_nul)
                return false;
            continue;
        }
        unsigned need;
        std::uint32_t code, min;
        if (c >= 0xc2 && c <= 0xdf) {
            need = 1;
            code = c & 31;
            min = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            need = 2;
            code = c & 15;
            min = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            need = 3;
            code = c & 7;
            min = 0x10000;
        } else
            return false;
        if (s.size() - i < need)
            return false;
        for (unsigned j = 0; j < need; j++) {
            auto q = static_cast<unsigned char>(s[i++]);
            if ((q & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (q & 63);
        }
        if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return !(s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xef &&
             static_cast<unsigned char>(s[1]) == 0xbb && static_cast<unsigned char>(s[2]) == 0xbf);
}
Status validate_json_object(std::string_view text) noexcept {
    auto admission = json_contract::preflight(text);
    if (!admission)
        return admission;
    try {
        if (!valid_utf8(text, false))
            return error(StatusCode::invalid_contract, 1);
        bool duplicate = false;
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &value) {
            if (depth > 16) {
                duplicate = true;
                return false;
            }
            if (event == nlohmann::json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == nlohmann::json::parse_event_t::object_end) {
                if (!keys.empty())
                    keys.pop_back();
            } else if (event == nlohmann::json::parse_event_t::key) {
                if (keys.empty() || !keys.back().insert(value.get<std::string>()).second)
                    duplicate = true;
            }
            return true;
        };
        auto parsed = nlohmann::json::parse(text, callback, false);
        return (!duplicate && parsed.is_object()) ? Status{}
                                                  : error(StatusCode::invalid_contract, 1);
    } catch (const std::bad_alloc &) {
        return error(StatusCode::resource_limit, 1);
    } catch (...) {
        return error(StatusCode::invalid_contract, 1);
    }
}
Status ByteSource::read(std::uint64_t offset, std::span<std::byte> out) const noexcept {
    std::uint64_t end;
    if (!checked_add(offset, out.size(), end) || end > size)
        return error(StatusCode::invalid_contract, 2);
    if (callback)
        return callback(context, offset, out);
    if (size > contiguous.size())
        return error(StatusCode::invalid_contract, 2);
    if (!out.empty())
        std::memcpy(out.data(), contiguous.data() + offset, out.size());
    return {};
}
std::expected<ValueView, Status> ValueView::strided(std::span<const double> s, std::uint64_t o,
                                                    std::uint64_t i, std::uint64_t os,
                                                    std::uint64_t is) noexcept {
    std::uint64_t n;
    if (o > (1ull << 40) || i > (1ull << 40) || !checked_multiply(o, i, n))
        return std::unexpected(error(StatusCode::resource_limit, 3));
    if (o && i) {
        std::uint64_t a, b, last;
        if ((o > 1 && os == 0) || (i > 1 && is == 0) || !checked_multiply(o - 1, os, a) ||
            !checked_multiply(i - 1, is, b) || !checked_add(a, b, last) || last >= s.size())
            return std::unexpected(error(StatusCode::invalid_contract, 3));
    }
    return ValueView{s, nullptr, nullptr, o, i, os, is};
}
std::expected<ValueView, Status> ValueView::borrowed(const void *c, Fill f, std::uint64_t o,
                                                     std::uint64_t i) noexcept {
    std::uint64_t n;
    if (!f || o > (1ull << 40) || i > (1ull << 40) || !checked_multiply(o, i, n))
        return std::unexpected(error(StatusCode::invalid_contract, 3));
    return ValueView{{}, c, f, o, i, 0, 0};
}
Status ValueView::fill(std::uint64_t row, std::uint64_t begin, std::span<double> d) const noexcept {
    std::uint64_t end;
    if (row >= outer || !checked_add(begin, d.size(), end) || end > inner)
        return error(StatusCode::invalid_contract, 4);
    if (callback)
        return callback(context, row, begin, d);
    auto v = strided(storage, outer, inner, outer_stride, inner_stride);
    if (!v)
        return v.error();
    for (std::size_t k = 0; k < d.size(); k++)
        d[k] = storage[row * outer_stride + (begin + k) * inner_stride];
    return {};
}
std::expected<WorkspacePlan,Status> plan_export_capacity(const ExportCapacity& c,ExportLimits l) noexcept {
    auto invalid=[](){return std::unexpected(error(StatusCode::invalid_contract,5));};
    auto limited=[](){return std::unexpected(error(StatusCode::resource_limit,5));};
    if(static_cast<unsigned>(c.kind)>1 || l.frame_values<4 || l.frame_values>maximum_frame_values || l.frame_values%4 ||
       !l.workspace_budget_bytes || !l.metadata_budget_bytes || l.index_tile_records<1 || l.index_tile_records>65536 ||
       l.copy_tile_bytes<4096 || l.copy_tile_bytes>1048576 || !c.profile_capacity || c.maximum_log_sources>64 ||
       c.diagnostics.size()>5 || !c.metadata_bytes_bound || l.provenance_bytes_bound>maximum_extent)return invalid();
    if(c.scalar_capacity>1000000 || c.profile_capacity>1000000 || c.record_capacity>(1ull<<40) ||
       c.metadata_bytes_bound>l.metadata_budget_bytes || c.metadata_bytes_bound>268435456)return limited();
    unsigned kinds=0;
    for(const auto&trace:c.diagnostics){
        const auto k=static_cast<unsigned>(trace.kind);
        if(k>4 || !trace.record_capacity || (kinds&(1u<<k)))return invalid();
        if((k==0&&c.kind!=RunKind::timeseries)||(k>=1&&k<=3&&c.kind!=RunKind::steady_state))
            return std::unexpected(error(StatusCode::unsupported_feature,5));
        kinds|=1u<<k;
    }
    WorkspacePlan plan;plan.metadata_bytes=c.metadata_bytes_bound;
    const auto C=c.scalar_capacity,K=c.record_capacity;
    const auto outer=c.kind==RunKind::timeseries?C:K,inner=c.kind==RunKind::timeseries?K:C;
    std::uint64_t values{},rounded_inner{};
    if(!checked_multiply(C,K,values)||!checked_add(inner,l.frame_values-1,rounded_inner)||
       !checked_multiply(outer,rounded_inner/l.frame_values,plan.maximum_frames)||plan.maximum_frames>(1ull<<32))return limited();
    plan.gather_bytes=plan.decode_bytes=8*l.frame_values;
    plan.codec_bytes=encoded_capacity(l.frame_values)+4096;
    plan.index_bytes=77*l.index_tile_records;plan.copy_bytes=l.copy_tile_bytes;
    plan.section_capacity=61+(c.effective_requests_included?3:0);
    const std::uint64_t sources=c.indexed_provenance?5+c.maximum_log_sources:0;
    plan.source_capacity=c.indexed_provenance?sources:6;
    const std::uint64_t provenance_sections=c.indexed_provenance?17+4*c.maximum_log_sources:8;
    if(!checked_add(plan.section_capacity,provenance_sections,plan.section_capacity))return limited();
    for(const auto&trace:c.diagnostics)
        if(!checked_add(plan.section_capacity,diagnostic_section_capacity(trace.kind),plan.section_capacity))return limited();
    if(plan.section_capacity>1048576 || !checked_multiply(plan.section_capacity,64,plan.directory_bytes))return limited();
    plan.serialization_bytes=1048576;plan.maximum_archive_bytes=128;
    auto bound=[&](std::uint64_t count,std::uint64_t width){std::uint64_t bytes{};return checked_multiply(count,width,bytes)&&checked_add(plan.maximum_archive_bytes,bytes,plan.maximum_archive_bytes);};
    if(!bound(plan.section_capacity,64)||!bound(values,8)||!bound(plan.maximum_frames,102)||!bound(K,16)||!bound(outer,4)||
       !bound(c.profile_capacity,139)||!bound(plan.metadata_bytes,2)||!bound(l.provenance_bytes_bound,9)||
       !bound(2+sources+c.diagnostics.size(),1048576)||!bound(1+c.maximum_log_sources,8))return limited();
    constexpr std::uint64_t row_bytes[]={32,96,81,88,54};
    for(const auto&trace:c.diagnostics)if(!bound(trace.record_capacity,row_bytes[static_cast<unsigned>(trace.kind)]))return limited();
    plan.total_bytes=plan.metadata_bytes;
    for(auto bytes:{plan.gather_bytes,plan.decode_bytes,plan.codec_bytes,plan.index_bytes,plan.copy_bytes,plan.directory_bytes,plan.serialization_bytes})
        if(!checked_add(plan.total_bytes,bytes,plan.total_bytes))return limited();
    if(plan.total_bytes>l.workspace_budget_bytes)return limited();
    return plan;
}
PreparedExport::PreparedExport(PreparedExport &&x) noexcept {
    *this = std::move(x);
}
PreparedExport &PreparedExport::operator=(PreparedExport &&x) noexcept {
    if (this != &x) {
        description_ = std::move(x.description_);
        limits_ = x.limits_;
        plan_ = x.plan_;
        valid_ = std::exchange(x.valid_, false);
        gather_ = std::move(x.gather_);
        decode_ = std::move(x.decode_);
        index_ = std::move(x.index_);
        copy_ = std::move(x.copy_);
        directory_ = std::move(x.directory_);
        serialization_ = std::move(x.serialization_);
    }
    return *this;
}
std::expected<PreparedExport, Status> PreparedExport::prepare(ArchiveDescription d,
                                                              ExportLimits l) {
    try {
        if (l.frame_values < 4 || l.frame_values > maximum_frame_values || l.frame_values % 4 ||
            !l.workspace_budget_bytes || !l.metadata_budget_bytes || l.index_tile_records < 1 ||
            l.index_tile_records > 65536 || l.copy_tile_bytes < 4096 || l.copy_tile_bytes > 1048576)
            return std::unexpected(error(StatusCode::invalid_contract, 5));
        auto &run = d.run;
        if (run.profiles.empty()) run.profiles.push_back(CodecProfile::reversible());
        auto C = run.channels.size();
        auto R = run.requests.size();
        if (C > 1000000 || R > 1000000 || run.record_capacity > (1ull << 40))
            return std::unexpected(error(StatusCode::resource_limit, 5));
        if (run.profiles.size()>1000000 || run.diagnostics.size()>5 ||
            d.maximum_log_sources>64 || (!run.row_profile_index.empty()&&run.row_profile_index.size()!=C) ||
            l.provenance_bytes_bound>maximum_extent)
            return std::unexpected(error(StatusCode::invalid_contract,5));
        for (auto profile:run.profiles)
            if (run.kind==RunKind::steady_state && profile.mode()!=CodecMode::reversible)
                return std::unexpected(error(StatusCode::unsupported_profile,5));
        for (auto index:run.row_profile_index)
            if(index>=run.profiles.size())return std::unexpected(error(StatusCode::invalid_contract,5));
        for(std::size_t j=0;j<run.diagnostics.size();++j) {
            const auto& trace=run.diagnostics[j];
            if(auto status=validate_diagnostic_description(trace);!status)return std::unexpected(status);
            const auto k=static_cast<unsigned>(trace.kind);
            if((k==0&&run.kind!=RunKind::timeseries)||(k>=1&&k<=3&&run.kind!=RunKind::steady_state))
                return std::unexpected(error(StatusCode::unsupported_feature,5));
            for(std::size_t i=0;i<j;++i)if(run.diagnostics[i].kind==trace.kind)
                return std::unexpected(error(StatusCode::invalid_contract,5));
        }
        if (!valid_uuid(d.file_id) || !valid_utc(d.created_utc) || d.writer_name.empty() ||
            d.writer_version.empty() || run.run_id.empty())
            return std::unexpected(error(StatusCode::invalid_contract, 5));
        if (static_cast<unsigned>(run.kind) > 1 ||
            static_cast<unsigned>(run.effective_requests) > 2)
            return std::unexpected(error(StatusCode::invalid_contract, 5));
        WorkspacePlan plan;
        std::uint64_t channel_storage{}, request_storage{};
        if (!checked_multiply(run.channels.capacity(), sizeof(ChannelDescription),
                              channel_storage) ||
            !checked_multiply(run.requests.capacity(), sizeof(RequestDescription),
                              request_storage) ||
            !checked_add(sizeof(PreparedExport), channel_storage, plan.metadata_bytes) ||
            !checked_add(plan.metadata_bytes, request_storage, plan.metadata_bytes))
            return std::unexpected(error(StatusCode::resource_limit, 5));
        auto add_text = [&](const std::string &t) {
            std::uint64_t storage{}, out{};
            if (!valid_utf8(t) || !checked_add(t.capacity(), 1, storage) ||
                !checked_add(plan.metadata_bytes, storage, out))
                return false;
            // SSO capacity may already reside inside sizeof(string); counting it again is
            // conservative.
            plan.metadata_bytes = out;
            return true;
        };
        for (auto *s : {&d.file_id, &d.created_utc, &d.writer_name, &d.writer_version, &run.run_id,
                        &run.run_name, &run.assembly_ref})
            if (!add_text(*s))
                return std::unexpected(error(StatusCode::invalid_contract, 5));
        std::uint64_t extra{};
        for(auto pair:{std::pair<std::uint64_t,std::uint64_t>{run.profiles.capacity(),sizeof(CodecProfile)},
                       {run.row_profile_index.capacity(),sizeof(std::uint32_t)},
                       {run.diagnostics.capacity(),sizeof(DiagnosticTraceDescription)}})
            if(!checked_multiply(pair.first,pair.second,extra)||!checked_add(plan.metadata_bytes,extra,plan.metadata_bytes))
                return std::unexpected(error(StatusCode::resource_limit,5));
        for(const auto& trace:run.diagnostics)for(const auto* names:{&trace.status_names,&trace.event_names}) {
            if(!checked_multiply(names->capacity(),sizeof(std::string),extra)||!checked_add(plan.metadata_bytes,extra,plan.metadata_bytes))
                return std::unexpected(error(StatusCode::resource_limit,5));
            for(const auto& name:*names)if(!add_text(name))return std::unexpected(error(StatusCode::invalid_contract,5));
        }
        if (!run.run_name_present && !run.run_name.empty())
            return std::unexpected(error(StatusCode::invalid_contract, 5));
        std::set<std::pair<std::string, std::string>> request_keys;
        std::set<std::string> scalar_keys;
        for (const auto &q : run.requests) {
            if (q.group_id.empty() || q.request_id.empty() ||
                !request_keys.emplace(q.group_id, q.request_id).second)
                return std::unexpected(error(StatusCode::invalid_contract, 5));
            for (auto *s : {&q.group_id, &q.request_id, &q.schema_version, &q.construction_profile,
                            &q.effective_json})
                if (!add_text(*s))
                    return std::unexpected(error(StatusCode::invalid_contract, 5));
            if (run.effective_requests == SectionState::included) {
                auto st = json_contract::preflight(q.effective_json, l.metadata_budget_bytes);
                if (st)
                    st = validate_json_object(q.effective_json);
                if (!st)
                    return std::unexpected(st);
            }
        }
        for (const auto &c : run.channels) {
            if (c.scalar_id.empty() || c.request_index >= R ||
                !scalar_keys.insert(c.scalar_id).second)
                return std::unexpected(error(StatusCode::invalid_contract, 5));
            const auto &q = run.requests[c.request_index];
            if (c.group_id != q.group_id || c.request_id != q.request_id)
                return std::unexpected(error(StatusCode::invalid_contract, 5));
            for (auto *s : {&c.scalar_id, &c.group_id, &c.request_id, &c.component_path, &c.unit})
                if (!add_text(*s))
                    return std::unexpected(error(StatusCode::invalid_contract, 5));
        }
        if (plan.metadata_bytes > l.metadata_budget_bytes || plan.metadata_bytes > 268435456)
            return std::unexpected(error(StatusCode::resource_limit, 5));
        std::array<DiagnosticCapacity,5> trace_capacities{};
        for(std::size_t i=0;i<run.diagnostics.size();++i)trace_capacities[i]={run.diagnostics[i].kind,run.diagnostics[i].record_capacity};
        ExportCapacity capacity{run.kind,C,run.record_capacity,run.profiles.size(),run.effective_requests==SectionState::included,
            d.indexed_provenance,d.maximum_log_sources,plan.metadata_bytes,std::span(trace_capacities).first(run.diagnostics.size())};
        auto arithmetic=plan_export_capacity(capacity,l);if(!arithmetic)return std::unexpected(arithmetic.error());plan=*arithmetic;
        PreparedExport p;
        p.description_ = std::move(d);
        p.limits_ = l;
        p.plan_ = plan;
        p.gather_.resize(l.frame_values);
        p.decode_.resize(l.frame_values);
        p.index_.resize(plan.index_bytes);
        p.copy_.resize(plan.copy_bytes);
        p.directory_.resize(plan.directory_bytes);
        p.serialization_.resize(plan.serialization_bytes);
        p.valid_ = true;
        return p;
    } catch (const std::bad_alloc &) {
        return std::unexpected(error(StatusCode::resource_limit, 5));
    } catch (...) {
        return std::unexpected(error(StatusCode::invalid_contract, 5));
    }
}
Status PreparedExport::validate(const RunResultView &v) const noexcept {
    if (!valid_)
        return error(StatusCode::invalid_state, 6);
    const auto &i = v.info;
    const auto &r = description_.run;
    const auto C = r.channels.size();
    const auto K = v.axis.size();
    if (static_cast<unsigned>(i.outcome) > 7 || static_cast<unsigned>(i.numerical) > 3 ||
        static_cast<unsigned>(i.observation) > 3 || static_cast<unsigned>(i.logging) > 4 ||
        !(i.catalog_state == SectionState::included ||
          i.catalog_state == SectionState::unavailable))
        return error(StatusCode::invalid_contract, 6);
    auto diagnostics=validate_diagnostics(r.diagnostics,v.diagnostics,i.accepted_count);
    if(!diagnostics)return diagnostics.error();
    if(i.outcome==RunOutcome::diagnostic_failure && *diagnostics!=DiagnosticStatus::failed)
        return error(StatusCode::invalid_contract,6);
    if (!std::isfinite(i.last_accepted_value) || !std::isfinite(i.requested_begin) ||
        !std::isfinite(i.requested_end) || !std::isfinite(i.elapsed_seconds) ||
        i.elapsed_seconds < 0)
        return error(StatusCode::invalid_contract, 6);
    if ((!i.last_accepted_present &&
         (i.last_accepted_value != 0 || std::signbit(i.last_accepted_value))) ||
        (!i.requested_bounds_present &&
         (i.requested_begin != 0 || i.requested_end != 0 || std::signbit(i.requested_begin) ||
          std::signbit(i.requested_end))) ||
        (!i.elapsed_seconds_present && (i.elapsed_seconds != 0 || std::signbit(i.elapsed_seconds))))
        return error(StatusCode::invalid_contract, 6);
    if (i.requested_bounds_present && i.requested_begin > i.requested_end)
        return error(StatusCode::invalid_contract, 6);
    if (i.elapsed_seconds_present == i.elapsed_seconds_scope.empty() ||
        v.identity_axis_present == i.identity_axis_convention.empty())
        return error(StatusCode::invalid_contract, 6);
    for (auto t : {i.failure_stage, i.failure_code, i.termination_reason, i.elapsed_seconds_scope,
                   i.identity_axis_convention, i.axis_name, i.axis_unit})
        if (!valid_utf8(t))
            return error(StatusCode::invalid_contract, 6);
    if (i.catalog_state == SectionState::unavailable) {
        if (!(i.outcome == RunOutcome::preparation_failure || i.outcome == RunOutcome::skipped ||
              i.outcome == RunOutcome::cancelled) ||
            i.numerical != NumericalStatus::not_started ||
            i.observation != ObservationStatus::not_started || K || v.identity_axis_present ||
            i.last_accepted_present || i.accepted_count)
            return error(StatusCode::invalid_contract, 6);
        return {};
    }
    if (i.outcome == RunOutcome::completed &&
        (i.numerical != NumericalStatus::target_reached ||
         !(i.observation == ObservationStatus::complete ||
           i.observation == ObservationStatus::disabled) ||
         !(i.logging == LoggingStatus::complete || i.logging == LoggingStatus::disabled)))
        return error(StatusCode::invalid_contract, 6);
    if (r.assembly_ref.empty())
        return error(StatusCode::invalid_contract, 6);
    if (K > r.record_capacity || (C == 0 && K))
        return error(StatusCode::invalid_contract, 6);
    auto expected_outer = r.kind == RunKind::timeseries ? C : K;
    auto expected_inner = r.kind == RunKind::timeseries ? K : C;
    if (v.values.outer != expected_outer || v.values.inner != expected_inner)
        return error(StatusCode::invalid_contract, 6);
    auto vv = v.values.callback
                  ? ValueView::borrowed(v.values.context, v.values.callback, v.values.outer,
                                        v.values.inner)
                  : ValueView::strided(v.values.storage, v.values.outer, v.values.inner,
                                       v.values.outer_stride, v.values.inner_stride);
    if (!vv)
        return vv.error();
    if (v.identity_axis_present ? v.accepted_indices.size() != K : !v.accepted_indices.empty())
        return error(StatusCode::invalid_contract, 6);
    for (std::size_t j = 0; j < K; j++) {
        if (!std::isfinite(v.axis[j]) ||
            (r.kind == RunKind::timeseries && j && v.axis[j] <= v.axis[j - 1]))
            return error(StatusCode::invalid_contract, 6);
        if (v.identity_axis_present && j && v.accepted_indices[j] <= v.accepted_indices[j - 1])
            return error(StatusCode::invalid_contract, 6);
    }
    if (r.kind == RunKind::timeseries && (i.axis_name != "time" || i.axis_unit != "s"))
        return error(StatusCode::invalid_contract, 6);
    return {};
}
} // namespace serene::outputs
