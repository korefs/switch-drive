#include "switchdrive/network.hpp"
#include "switchdrive/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace switchdrive {
namespace {

constexpr double kKibibyte = 1024.0;
constexpr double kMebibyte = 1024.0 * kKibibyte;
constexpr double kGibibyte = 1024.0 * kMebibyte;
constexpr auto kRateSampleInterval = std::chrono::milliseconds(500);
constexpr double kRateSmoothing = 0.25;

bool parseUnsigned(const std::string& value, uint64_t& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const auto parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') return false;
    out = parsed;
    return true;
}

} // namespace

TransferMeter::TransferMeter(uint64_t initialBytes, Clock::time_point startedAt) : sampledBytes_(initialBytes), sampledAt_(startedAt) {}

TransferEstimate TransferMeter::sample(uint64_t received, uint64_t total, Clock::time_point now) {
    if (received < sampledBytes_) {
        sampledBytes_ = received;
        sampledAt_ = now;
        smoothedBytesPerSecond_ = 0;
        ready_ = false;
    }
    const auto elapsed = now - sampledAt_;
    if (elapsed >= kRateSampleInterval && received > sampledBytes_) {
        const double seconds = std::chrono::duration<double>(elapsed).count();
        const double instantaneous = static_cast<double>(received - sampledBytes_) / seconds;
        smoothedBytesPerSecond_ = ready_ ? kRateSmoothing * instantaneous + (1.0 - kRateSmoothing) * smoothedBytesPerSecond_ : instantaneous;
        sampledBytes_ = received;
        sampledAt_ = now;
        ready_ = std::isfinite(smoothedBytesPerSecond_) && smoothedBytesPerSecond_ > 0;
    }

    TransferEstimate estimate{smoothedBytesPerSecond_, 0, ready_};
    if (!estimate.ready) return estimate;
    const uint64_t remaining = received < total ? total - received : 0;
    const double seconds = std::ceil(static_cast<double>(remaining) / estimate.bytesPerSecond);
    const double maximum = static_cast<double>(std::numeric_limits<uint64_t>::max());
    estimate.etaSeconds = seconds >= maximum ? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(seconds);
    return estimate;
}

std::string formatDataSize(uint64_t bytes) {
    char text[64]{};
    if (bytes >= static_cast<uint64_t>(kGibibyte)) std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::SizeGiB), static_cast<double>(bytes) / kGibibyte);
    else if (bytes >= static_cast<uint64_t>(kMebibyte)) std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::SizeMiB), static_cast<double>(bytes) / kMebibyte);
    else if (bytes >= static_cast<uint64_t>(kKibibyte)) std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::SizeKiB), static_cast<double>(bytes) / kKibibyte);
    else std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::SizeBytes), static_cast<unsigned long long>(bytes));
    return text;
}

std::string formatDuration(uint64_t seconds) {
    char text[64]{};
    if (seconds >= 3600) std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::DurationHours), static_cast<unsigned long long>(seconds / 3600), static_cast<unsigned long long>((seconds % 3600) / 60));
    else if (seconds >= 60) std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::DurationMinutes), static_cast<unsigned long long>(seconds / 60), static_cast<unsigned long long>(seconds % 60));
    else std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::DurationSeconds), static_cast<unsigned long long>(seconds));
    return text;
}

std::string formatTransferProgress(uint64_t received, uint64_t total, const TransferEstimate& estimate) {
    const std::string current = formatDataSize(received);
    const std::string expected = formatDataSize(total);
    const double percentage = total ? std::min(100.0, static_cast<double>(received) * 100.0 / static_cast<double>(total)) : 0;
    char text[256]{};
    if (!estimate.ready) {
        std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::TransferCalculating), current.c_str(), expected.c_str(), percentage);
        return text;
    }
    const double maximum = static_cast<double>(std::numeric_limits<uint64_t>::max());
    const uint64_t rateBytes = estimate.bytesPerSecond >= maximum ? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(estimate.bytesPerSecond);
    const std::string rate = formatDataSize(rateBytes);
    const std::string eta = formatDuration(estimate.etaSeconds);
    std::snprintf(text, sizeof(text), i18n::tr(i18n::TextId::TransferEstimate), current.c_str(), expected.c_str(), percentage, rate.c_str(), eta.c_str());
    return text;
}

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
