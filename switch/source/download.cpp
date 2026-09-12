#include "switchdrive/network.hpp"

#include <cstdlib>

namespace switchdrive {
namespace {

bool parseUnsigned(const std::string& value, uint64_t& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const auto parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') return false;
    out = parsed;
    return true;
}

} // namespace

RangeResponse validateRangeResponse(long status, const std::string& contentRange, uint64_t resumeAt, uint64_t expectedSize) {
    if (status == 416) return resumeAt == expectedSize ? RangeResponse::AlreadyComplete : RangeResponse::Reject;
    if (resumeAt == 0) return status == 200 ? RangeResponse::AcceptBody : RangeResponse::Reject;
    if (status != 206 || contentRange.rfind("bytes ", 0) != 0) return RangeResponse::Reject;
    const std::string value = contentRange.substr(6);
    const auto dash = value.find('-');
    const auto slash = value.find('/');
    if (dash == std::string::npos || slash == std::string::npos || dash >= slash) return RangeResponse::Reject;
    uint64_t first{}, last{}, total{};
    if (!parseUnsigned(value.substr(0, dash), first) || !parseUnsigned(value.substr(dash + 1, slash - dash - 1), last) || !parseUnsigned(value.substr(slash + 1), total)) return RangeResponse::Reject;
    if (first != resumeAt || last < first || total != expectedSize || last >= total) return RangeResponse::Reject;
    return RangeResponse::AcceptBody;
}

} // namespace switchdrive
