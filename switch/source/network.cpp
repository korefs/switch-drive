#include "switchdrive/network.hpp"
#include "switchdrive/i18n.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

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

bool configure(CURL* curl, const std::vector<std::string>& headers, curl_slist*& list, std::string&) {
    list = nullptr;
    for (const auto& header : headers) list = curl_slist_append(list, header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchDrive/0.1");
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

} // namespace

bool HttpClient::get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = i18n::tr(i18n::TextId::CurlUnavailable);
        return false;
    }
    curl_slist* list = nullptr;
    configure(curl, headers, list, error);
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
        error = curl_easy_strerror(result);
        return false;
    }
    return out.status >= 200 && out.status < 300;
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
    configure(curl, all, list, error);
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
        error = curl_easy_strerror(result);
        return false;
    }
    return out.status >= 200 && out.status < 300;
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
    configure(curl, all, list, error);
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
        error = curl_easy_strerror(curlResult);
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

bool AuthClient::begin(const std::string& consoleKey, std::string& id, std::string& url, std::string& code, std::string& pollSecret, std::string& error) const {
    HttpClient::Response response;
    if (!http_.post(serviceUrl_ + "/v1/pairings", "{\"consolePublicKey\":\"" + consoleKey + "\"}", {}, response, error)) return false;
    json_t* root = parse(response.body, error);
    if (!root) return false;
    id = str(root, "id"); url = str(root, "verificationUri"); code = str(root, "code"); pollSecret = str(root, "pollSecret");
    json_decref(root);
    if (id.empty() || url.empty() || code.empty() || pollSecret.empty()) {
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

bool DriveClient::list(const std::string& accessToken, const std::string& folderId, bool sharedWithMe, const std::string& pageToken, std::vector<RemoteFile>& files, std::string& nextPage, std::string& error) const {
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
        RemoteFile file;
        file.id = str(row, "id"); file.name = str(row, "name"); file.mimeType = str(row, "mimeType");
        file.size = integerOrString(row, "size"); file.revision = stringOrInteger(row, "version"); file.md5 = str(row, "md5Checksum");
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

std::string DriveClient::mediaUrl(const RemoteFile& file) const {
    return "https://www.googleapis.com/drive/v3/files/" + file.id + "?alt=media" + (file.resourceKey.empty() ? "" : "&resourceKey=" + file.resourceKey);
}

} // namespace switchdrive
