#include "switchdrive/network.hpp"
#include "switchdrive/i18n.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#ifdef __SWITCH__
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace switchdrive {
namespace {

size_t append(void* contents, size_t size, size_t count, void* user) {
    auto* text = static_cast<std::string*>(user);
    text->append(static_cast<char*>(contents), size * count);
    return size * count;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

struct DownloadContext {
    LocalFile* output{};
    uint64_t resumeAt{};
    uint64_t expectedSize{};
    uint64_t nextOffset{};
    long status{};
    std::string etag;
    std::string contentRange;
    std::string rejection;
    std::function<bool(const std::string&)> headersAccepted;
    std::function<bool(uint64_t)> progress;
    bool bodyAllowed{};
    bool alreadyComplete{};
    bool stopped{};
    bool rejected{};
};

int pump(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* activity = static_cast<const ActivityCallback*>(user);
    return continueHttpActivity(activity) ? 0 : 1;
}

size_t writeDownload(void* contents, size_t size, size_t count, void* pointer) {
    auto* context = static_cast<DownloadContext*>(pointer);
    const size_t bytes = size * count;
    if (context->alreadyComplete) return bytes;
    if (!context->bodyAllowed) {
        context->rejected = true;
        if (context->rejection.empty()) context->rejection = i18n::tr(i18n::TextId::HttpWriteNotAllowed);
        return 0;
    }
    std::string error;
    if (!context->output->writeAt(context->nextOffset, contents, bytes, error)) {
        context->rejected = true;
        context->rejection = error;
        return 0;
    }
    context->nextOffset += bytes;
    if (context->progress && !context->progress(context->nextOffset)) {
        context->stopped = true;
        return 0;
    }
    return bytes;
}

size_t captureHeader(char* buffer, size_t size, size_t count, void* user) {
    auto* response = static_cast<HttpClient::Response*>(user);
    std::string line(buffer, size * count);
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        const std::string key = lower(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key == "etag") response->etag = value;
        if (key == "content-range") response->contentRange = value;
    }
    return size * count;
}

size_t captureDownloadHeader(char* buffer, size_t size, size_t count, void* user) {
    auto* context = static_cast<DownloadContext*>(user);
    std::string line(buffer, size * count);
    if (line.rfind("HTTP/", 0) == 0) {
        const auto space = line.find(' ');
        context->status = space == std::string::npos ? 0 : std::strtol(line.c_str() + space + 1, nullptr, 10);
        context->etag.clear();
        context->contentRange.clear();
        context->bodyAllowed = false;
        context->alreadyComplete = false;
        context->rejected = false;
        context->rejection.clear();
        return size * count;
    }
    if (line == "\r\n" || line == "\n") {
        // libcurl follows redirects. Only a final response can carry a body.
        if (context->status >= 100 && context->status < 400 && context->status != 200 && context->status != 206) return size * count;
        const RangeResponse range = validateRangeResponse(context->status, context->contentRange, context->resumeAt, context->expectedSize);
        if (range == RangeResponse::Reject) {
            context->rejected = true;
            context->rejection = i18n::tr(i18n::TextId::ServerRangeNotConfirmed);
            return size * count;
        }
        if (range == RangeResponse::AlreadyComplete) {
            context->alreadyComplete = true;
            return size * count;
        }
        if (context->headersAccepted && !context->headersAccepted(context->etag)) {
            context->rejected = true;
            context->rejection = i18n::tr(i18n::TextId::ResponseSaveFailed);
            return size * count;
        }
        context->bodyAllowed = true;
        return size * count;
    }
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        const std::string key = lower(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key == "etag") context->etag = value;
        if (key == "content-range") context->contentRange = value;
    }
    return size * count;
}

std::string encode(CURL* curl, const std::string& value) {
    char* result = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string output = result ? result : "";
    curl_free(result);
    return output;
}

bool configure(CURL* curl, const std::vector<std::string>& headers, curl_slist*& list, std::string&, const ActivityCallback* activity) {
    list = nullptr;
    for (const auto& header : headers) list = curl_slist_append(list, header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchDrive/0.2");
    if (activity && *activity) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, pump);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, activity);
    }
    return true;
}

json_t* parse(const std::string& text, std::string& error) {
    json_error_t details{};
    json_t* root = json_loadb(text.data(), text.size(), 0, &details);
    if (!root) error = i18n::tr(i18n::TextId::InvalidServiceJson);
    return root;
}

std::string str(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : "";
}

std::string stringOrInteger(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    if (json_is_string(value)) return json_string_value(value);
    if (json_is_integer(value)) return std::to_string(json_integer_value(value));
    return {};
}

uint64_t integerOrString(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    if (json_is_integer(value)) return static_cast<uint64_t>(json_integer_value(value));
    if (json_is_string(value)) return std::strtoull(json_string_value(value), nullptr, 10);
    return 0;
}

std::string base64(const std::string& input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t value = (static_cast<unsigned char>(input[i]) << 16) |
            (i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) << 8 : 0) |
            (i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0);
        out += alphabet[(value >> 18) & 63]; out += alphabet[(value >> 12) & 63];
        out += i + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=';
        out += i + 2 < input.size() ? alphabet[value & 63] : '=';
    }
    return out;
}

void setHttpStatusError(long status, std::string& error) {
    char message[96]{};
    std::snprintf(message, sizeof(message), i18n::tr(i18n::TextId::HttpRequestFailed), status);
    error = message;
}

} // namespace

bool HttpClient::get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = i18n::tr(i18n::TextId::CurlUnavailable);
        return false;
    }
    curl_slist* list = nullptr;
    configure(curl, headers, list, error, &activity_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, captureHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &out);
    const auto result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        if (result == CURLE_ABORTED_BY_CALLBACK) error = i18n::tr(i18n::TextId::OperationCancelled);
        else error = curl_easy_strerror(result);
        return false;
    }
    if (out.status < 200 || out.status >= 300) { setHttpStatusError(out.status, error); return false; }
    return true;
}

bool HttpClient::post(const std::string& url, const std::string& body, const std::vector<std::string>& headers, Response& out, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = i18n::tr(i18n::TextId::CurlUnavailable);
        return false;
    }
    auto all = headers;
    all.emplace_back("Content-Type: application/json");
    curl_slist* list = nullptr;
    configure(curl, all, list, error, &activity_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    const auto result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        if (result == CURLE_ABORTED_BY_CALLBACK) error = i18n::tr(i18n::TextId::OperationCancelled);
        else error = curl_easy_strerror(result);
        return false;
    }
    if (out.status < 200 || out.status >= 300) { setHttpStatusError(out.status, error); return false; }
    return true;
}

bool HttpClient::del(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) { error = i18n::tr(i18n::TextId::CurlUnavailable); return false; }
    curl_slist* list = nullptr; configure(curl, headers, list, error, &activity_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    const auto result = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_slist_free_all(list); curl_easy_cleanup(curl);
    if (result != CURLE_OK) { error = result == CURLE_ABORTED_BY_CALLBACK ? i18n::tr(i18n::TextId::OperationCancelled) : curl_easy_strerror(result); return false; }
    if (out.status < 200 || out.status >= 300) { setHttpStatusError(out.status, error); return false; }
    return true;
}

bool HttpClient::download(const std::string& url, const std::vector<std::string>& headers, LocalFile& output, uint64_t resumeAt, uint64_t expectedSize, const std::string& ifRange, std::function<bool(const std::string&)> headersAccepted, std::function<bool(uint64_t)> progress, DownloadResult& result, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = i18n::tr(i18n::TextId::CurlUnavailable);
        return false;
    }
    auto all = headers;
    if (resumeAt) all.emplace_back("Range: bytes=" + std::to_string(resumeAt) + "-");
    if (resumeAt && !ifRange.empty()) all.emplace_back("If-Range: " + ifRange);
    curl_slist* list = nullptr;
    configure(curl, all, list, error, &activity_);
    DownloadContext context{&output, resumeAt, expectedSize, resumeAt, 0, {}, {}, {}, std::move(headersAccepted), std::move(progress)};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeDownload);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, captureDownloadHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);
    const auto curlResult = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpStatus);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    result.bytesWritten = context.nextOffset;
    result.etag = context.etag;
    if (context.stopped) {
        result.status = DownloadStatus::Paused;
        error = i18n::tr(i18n::TextId::DownloadPaused);
        return false;
    }
    if (context.rejected) {
        result.status = DownloadStatus::RangeRejected;
        error = context.rejection.empty() ? i18n::tr(i18n::TextId::RangeDenied) : context.rejection;
        return false;
    }
    if (curlResult != CURLE_OK) {
        result.status = DownloadStatus::Failed;
        if (curlResult == CURLE_ABORTED_BY_CALLBACK) error = i18n::tr(i18n::TextId::OperationCancelled);
        else error = curl_easy_strerror(curlResult);
        return false;
    }
    if (context.alreadyComplete) {
        result.status = DownloadStatus::AlreadyComplete;
        return true;
    }
    if (!context.bodyAllowed || context.nextOffset != expectedSize) {
        result.status = DownloadStatus::Failed;
        error = i18n::tr(i18n::TextId::DownloadSizeMismatch);
        return false;
    }
    result.status = DownloadStatus::Completed;
    return true;
}

bool AuthClient::begin(const std::string& consoleKey, std::string& id, std::string& url, std::string& qrUrl, std::string& code, std::string& pollSecret, std::string& error) const {
    HttpClient::Response response;
    if (!http_.post(serviceUrl_ + "/v1/pairings", "{\"consolePublicKey\":\"" + consoleKey + "\"}", {}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    id = str(root, "id"); url = str(root, "verificationUri"); qrUrl = str(root, "verificationUriComplete"); code = str(root, "code"); pollSecret = str(root, "pollSecret");
    json_decref(root);
    if (id.empty() || url.empty() || qrUrl.empty() || code.empty() || pollSecret.empty()) {
        error = i18n::tr(i18n::TextId::PairingResponseIncomplete);
        return false;
    }
    return true;
}

bool AuthClient::poll(const std::string& id, const std::string& pollSecret, Account& account, std::string& error) const {
    HttpClient::Response response;
    if (!http_.get(serviceUrl_ + "/v1/pairings/" + id, {"X-Pairing-Secret: " + pollSecret}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    if (str(root, "status") != "approved") {
        json_decref(root);
        error = i18n::tr(i18n::TextId::AwaitingAuthorization);
        return false;
    }
    json_t* data = json_object_get(root, "account");
    account.id = str(data, "id"); account.email = str(data, "email"); account.displayName = str(data, "displayName");
    json_decref(root);
    return !account.id.empty();
}

bool AuthClient::claim(const std::string& id, const std::string& pollSecret, std::string& session, Account& account, std::string& error) const {
    HttpClient::Response response;
    if (!http_.post(serviceUrl_ + "/v1/pairings/" + id + "/claim", "", {"X-Pairing-Secret: " + pollSecret}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    session = str(root, "sessionToken");
    json_t* data = json_object_get(root, "account");
    account.id = str(data, "id"); account.email = str(data, "email"); account.displayName = str(data, "displayName");
    json_decref(root);
    return !session.empty() && !account.id.empty();
}

bool AuthClient::accounts(const std::string& session, std::vector<Account>& accounts, std::string& error) const {
    HttpClient::Response response;
    if (!http_.get(serviceUrl_ + "/v1/accounts", {"Authorization: Bearer " + session}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    json_t* rows = json_object_get(root, "accounts");
    size_t index; json_t* row;
    json_array_foreach(rows, index, row) accounts.push_back({str(row, "id"), str(row, "email"), str(row, "displayName")});
    json_decref(root);
    return true;
}

bool AuthClient::accessToken(const std::string& session, const std::string& accountId, std::string& token, std::string& error) const {
    HttpClient::Response response;
    if (!http_.post(serviceUrl_ + "/v1/accounts/" + accountId + "/access-token", "", {"Authorization: Bearer " + session}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    token = str(root, "accessToken");
    json_decref(root);
    if (token.empty()) {
        error = i18n::tr(i18n::TextId::AccessTokenMissing);
        return false;
    }
    return true;
}

bool DriveClient::list(const std::string& accessToken, const std::string& folderId, bool sharedWithMe, const std::string& pageToken, std::vector<RemoteEntry>& files, std::string& nextPage, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = i18n::tr(i18n::TextId::CurlUnavailable);
        return false;
    }
    const std::string query = sharedWithMe ? "sharedWithMe and trashed = false" : "'" + folderId + "' in parents and trashed = false";
    std::string url = "https://www.googleapis.com/drive/v3/files?q=" + encode(curl, query) + "&pageSize=100&orderBy=folder,name&supportsAllDrives=true&includeItemsFromAllDrives=true&fields=nextPageToken,files(id,name,mimeType,size,version,md5Checksum,resourceKey,capabilities(canDownload),shortcutDetails(targetId,targetResourceKey))";
    if (!pageToken.empty()) url += "&pageToken=" + encode(curl, pageToken);
    curl_easy_cleanup(curl);
    HttpClient::Response response;
    if (!http_.get(url, {"Authorization: Bearer " + accessToken}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    nextPage = str(root, "nextPageToken");
    json_t* rows = json_object_get(root, "files");
    size_t index; json_t* row;
    json_array_foreach(rows, index, row) {
        RemoteEntry file;
        file.id = str(row, "id"); file.name = str(row, "name"); file.mimeType = str(row, "mimeType");
        file.size = integerOrString(row, "size"); file.revision = stringOrInteger(row, "version"); file.checksum = {ChecksumKind::Md5,str(row, "md5Checksum")};
        file.resourceKey = str(row, "resourceKey"); file.folder = file.mimeType == "application/vnd.google-apps.folder";
        file.shortcut = file.mimeType == "application/vnd.google-apps.shortcut";
        json_t* capabilities = json_object_get(row, "capabilities");
        file.canDownload = json_is_true(json_object_get(capabilities, "canDownload"));
        json_t* shortcut = json_object_get(row, "shortcutDetails");
        file.shortcutTargetId = str(shortcut, "targetId");
        files.push_back(std::move(file));
    }
    json_decref(root);
    return true;
}

std::string DriveClient::mediaUrl(const RemoteEntry& file) const {
    return "https://www.googleapis.com/drive/v3/files/" + file.id + "?alt=media" + (file.resourceKey.empty() ? "" : "&resourceKey=" + file.resourceKey);
}

bool parseHomeStorageHealthPayload(const std::string& payload, HomeStorageHealth& health, std::string& error) {
    json_t* root = parse(payload, error); if (!root) return false;
    const bool valid = json_is_object(root) && str(root, "service") == "switch-drive-home-storage";
    HomeStorageHealth parsed; parsed.instanceId = str(root, "instanceId"); parsed.name = str(root, "name"); parsed.protocolVersion = static_cast<int>(integerOrString(root, "protocolVersion")); parsed.httpPort = static_cast<int>(integerOrString(root, "httpPort")); parsed.authRequired = json_is_true(json_object_get(root, "authRequired")); json_decref(root);
    if (!valid || parsed.protocolVersion != 1 || parsed.instanceId.empty() || parsed.name.empty() || parsed.httpPort < 1 || parsed.httpPort > 65535) { error = i18n::tr(i18n::TextId::InvalidServiceJson); return false; }
    health = std::move(parsed); return true;
}

bool parseHomeStorageCatalogPayload(const std::string& payload, const std::string& providerId, std::vector<RemoteEntry>& files, std::string& next, std::string& error) {
    json_t* root = parse(payload, error); if (!root) return false;
    json_t* rows = json_object_get(root, "items");
    if (!json_is_object(root) || !json_is_array(rows)) { json_decref(root); error = i18n::tr(i18n::TextId::InvalidServiceJson); return false; }
    std::vector<RemoteEntry> parsed; const auto nextCursor = str(root, "nextCursor"); size_t index; json_t* row;
    json_array_foreach(rows, index, row) {
        RemoteEntry file; file.id = str(row, "id"); file.providerId = providerId; file.name = str(row, "name"); file.mimeType = str(row, "kind"); file.folder = file.mimeType == "folder"; file.size = integerOrString(row, "size"); file.revision = str(row, "etag"); file.etag = file.revision; file.checksum = {ChecksumKind::Sha256, str(row, "sha256")}; file.canDownload = json_is_true(json_object_get(row, "canDownload")); file.canHide = json_is_true(json_object_get(row, "canHide"));
        if (!json_is_object(row) || file.id.empty() || file.name.empty() || (file.mimeType != "folder" && file.mimeType != "file") || (!file.folder && (file.etag.empty() || file.checksum.value.size() != 64))) { json_decref(root); error = i18n::tr(i18n::TextId::InvalidServiceJson); return false; }
        parsed.push_back(std::move(file));
    }
    json_decref(root); files.insert(files.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end())); next = nextCursor; return true;
}

void deduplicateHomeStorageDiscoveries(std::vector<DiscoveredHomeStorage>& results) {
    std::set<std::string> seen;
    results.erase(std::remove_if(results.begin(), results.end(), [&](const DiscoveredHomeStorage& item) { return item.health.instanceId.empty() || item.health.protocolVersion != 1 || !seen.insert(item.health.instanceId).second; }), results.end());
}

bool HomeStorageClient::health(const std::string& baseUrl, HomeStorageHealth& health, std::string& error) const {
    HttpClient::Response response; if (!http_.get(baseUrl + "/drive-health", {}, response, error)) return false;
    return parseHomeStorageHealthPayload(response.body, health, error);
}

bool HomeStorageClient::authenticate(const std::string& baseUrl, const std::string& username, const std::string& password, std::string& token, bool& canManage, std::string& error) const {
    HttpClient::Response response; if (!http_.post(baseUrl + "/api/v1/auth/token", "", {"Authorization: Basic " + base64(username + ":" + password)}, response, error)) return false;
    json_t* root = parse(response.body, error); if (!root) return false; token = str(root, "accessToken");canManage=false;json_t* scopes=json_object_get(root,"scopes");size_t index;json_t* scope;json_array_foreach(scopes,index,scope)if(json_is_string(scope)&&std::string(json_string_value(scope))=="catalog:manage")canManage=true;json_decref(root); return !token.empty();
}

bool HomeStorageClient::list(const ProviderConfig& provider, const std::string& folderId, const std::string& cursor, std::vector<RemoteEntry>& files, std::string& next, std::string& error) const {
    std::string url = provider.baseUrl + "/api/v1/catalog?parentId=" + (folderId.empty() ? "root" : folderId); if (!cursor.empty()) url += "&cursor=" + cursor;
    std::vector<std::string> headers; if (!provider.accessToken.empty()) headers.push_back("Authorization: Bearer " + provider.accessToken);
    HttpClient::Response response; if (!http_.get(url, headers, response, error)) return false; return parseHomeStorageCatalogPayload(response.body, provider.id, files, next, error);
}

bool HomeStorageClient::hide(const ProviderConfig& provider, const std::string& id, std::string& error) const { HttpClient::Response response; return http_.del(provider.baseUrl + "/api/v1/catalog/" + id,{"Authorization: Bearer " + provider.accessToken},response,error); }
std::string HomeStorageClient::mediaUrl(const ProviderConfig& provider, const RemoteEntry& file) const { return provider.baseUrl + "/api/v1/files/" + file.id + "/content"; }

bool GoogleStorageProvider::list(const std::string& folder, bool shared, const std::string& cursor, std::vector<RemoteEntry>& files, std::string& next, std::string& error) const { const size_t begin=files.size();if(!drive_.list(token_,folder,shared,cursor,files,next,error))return false;for(size_t i=begin;i<files.size();++i)files[i].providerId="google-drive";return true; }
DownloadRequest GoogleStorageProvider::downloadRequest(const RemoteEntry& file) const { return {drive_.mediaUrl(file),token_.empty()?std::vector<std::string>{}:std::vector<std::string>{"Authorization: Bearer "+token_}}; }
bool HomeStorageProvider::list(const std::string& folder, bool, const std::string& cursor, std::vector<RemoteEntry>& files, std::string& next, std::string& error) const { return home_.list(config_,folder,cursor,files,next,error); }
DownloadRequest HomeStorageProvider::downloadRequest(const RemoteEntry& file) const { return {home_.mediaUrl(config_,file),config_.accessToken.empty()?std::vector<std::string>{}:std::vector<std::string>{"Authorization: Bearer "+config_.accessToken}}; }

bool discoverHomeStorage(std::vector<DiscoveredHomeStorage>& results, std::string& error) {
#ifdef __SWITCH__
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0); if (socketFd < 0) { error = i18n::tr(i18n::TextId::NoStorageFound); return false; }
    int enabled=1;setsockopt(socketFd,SOL_SOCKET,SO_BROADCAST,&enabled,sizeof(enabled));fcntl(socketFd,F_SETFL,O_NONBLOCK);
    sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(8080);target.sin_addr.s_addr=INADDR_BROADCAST;const char probe[]="SWITCHDRIVE_HOME_DISCOVER_V1";sendto(socketFd,probe,sizeof(probe)-1,0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);std::array<char,2048> buffer{};
    while(std::chrono::steady_clock::now()<end){sockaddr_in source{};socklen_t length=sizeof(source);const auto size=recvfrom(socketFd,buffer.data(),buffer.size()-1,0,reinterpret_cast<sockaddr*>(&source),&length);if(size<=0){usleep(50000);continue;}buffer[size]=0;HomeStorageHealth health;std::string parseError;if(parseHomeStorageHealthPayload(std::string(buffer.data(),size),health,parseError)){char address[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&source.sin_addr,address,sizeof(address));results.push_back({std::string("http://")+address+":"+std::to_string(health.httpPort),health});}}close(socketFd);deduplicateHomeStorageDiscoveries(results);return true;
#else
    (void)results; error = i18n::tr(i18n::TextId::NoStorageFound); return false;
#endif
}

} // namespace switchdrive
