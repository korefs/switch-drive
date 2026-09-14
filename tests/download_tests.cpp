#include "switchdrive/core.hpp"
#include "switchdrive/i18n.hpp"
#include "switchdrive/network.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace switchdrive;

namespace {

std::vector<unsigned char> payload(size_t size) {
    std::vector<unsigned char> result(size);
    for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<unsigned char>((i * 37 + i / 251) & 0xff);
    return result;
}

std::vector<unsigned char> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

class LoopbackServer {
  public:
    LoopbackServer(std::string headers, std::vector<unsigned char> body, size_t chunk = 4096)
        : headers_(std::move(headers)), body_(std::move(body)), chunk_(chunk) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(listener_ >= 0);
        int enabled = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        assert(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port_ = ntohs(address.sin_port);
        assert(listen(listener_, 1) == 0);
        thread_ = std::thread([this] { serve(); });
    }

    ~LoopbackServer() { wait(); }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/content"; }
    void wait() {
        if (thread_.joinable()) thread_.join();
        if (listener_ >= 0) { close(listener_); listener_ = -1; }
    }
    const std::string& request() const { return request_; }

  private:
    int listener_{-1};
    uint16_t port_{};
    std::string headers_;
    std::vector<unsigned char> body_;
    size_t chunk_{};
    std::string request_;
    std::thread thread_;

    static bool sendAll(int socket, const void* bytes, size_t size) {
        const auto* data = static_cast<const unsigned char*>(bytes);
        while (size) {
            const ssize_t sent = send(socket, data, size, 0);
            if (sent <= 0) return false;
            data += sent;
            size -= static_cast<size_t>(sent);
        }
        return true;
    }

    void serve() {
        const int client = accept(listener_, nullptr, nullptr);
        if (client < 0) return;
        std::array<char, 2048> buffer{};
        while (request_.find("\r\n\r\n") == std::string::npos) {
            const ssize_t received = recv(client, buffer.data(), buffer.size(), 0);
            if (received <= 0) break;
            request_.append(buffer.data(), static_cast<size_t>(received));
        }
        if (sendAll(client, headers_.data(), headers_.size())) {
            for (size_t offset = 0; offset < body_.size();) {
                const size_t amount = std::min(chunk_, body_.size() - offset);
                if (!sendAll(client, body_.data() + offset, amount)) break;
                offset += amount;
            }
        }
        shutdown(client, SHUT_RDWR);
        close(client);
    }
};

std::string okHeaders(size_t size) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(size) + "\r\nETag: \"test\"\r\nConnection: close\r\n\r\n";
}

void testWriter(const fs::path& root) {
    const auto bytes = payload(256 * 1024 + 137);
    const auto path = root / "writer.bin";
    std::string error;
    LocalFile file;
    assert(file.create(path, StorageKind::Regular, error));
    DownloadWriter writer(file, 0, 97, 31);
    assert(writer.start(error));
    assert(writer.write(bytes.data(), 101, nullptr, error) == DownloadWriteStatus::Accepted);
    uint64_t durable{};
    assert(writer.checkpoint(durable, error) && durable == 101);
    assert(writer.write(bytes.data() + 101, bytes.size() - 101, nullptr, error) == DownloadWriteStatus::Accepted);
    assert(writer.checkpoint(durable, error) && durable == bytes.size());
    assert(writer.finish(durable, error) && durable == bytes.size());
    const auto stats = writer.stats();
    assert(stats.asynchronous && stats.writtenBytes == bytes.size() && stats.durableBytes == bytes.size());
    assert(stats.peakQueuedBytes == 97);
    file.close();
    assert(readFile(path) == bytes);

    const auto resumedPath = root / "writer-resume.bin";
    LocalFile resumed;
    assert(resumed.create(resumedPath, StorageKind::Regular, error));
    assert(resumed.writeAt(0, bytes.data(), 113, error) && resumed.flush(error));
    resumed.close();
    assert(resumed.open(resumedPath, StorageKind::Regular, true, error));
    DownloadWriter resumedWriter(resumed, 113, 67, 23);
    assert(resumedWriter.write(bytes.data() + 113, bytes.size() - 113, nullptr, error) == DownloadWriteStatus::Accepted);
    assert(resumedWriter.finish(durable, error) && durable == bytes.size());
    resumed.close();
    assert(readFile(resumedPath) == bytes);

    const auto fallbackPath = root / "writer-fallback.bin";
    LocalFile fallback;
    assert(fallback.create(fallbackPath, StorageKind::Regular, error));
    DownloadWriter fallbackWriter(fallback, 0, 64, 16, false);
    assert(fallbackWriter.write(bytes.data(), bytes.size(), nullptr, error) == DownloadWriteStatus::Accepted);
    assert(fallbackWriter.finish(durable, error) && durable == bytes.size());
    assert(!fallbackWriter.stats().asynchronous);
    fallback.close();
    assert(readFile(fallbackPath) == bytes);

    const auto concatenatedPath = root / "writer-concatenated";
    LocalFile concatenated;
    assert(concatenated.create(concatenatedPath, StorageKind::Concatenated, error, 257));
    {
        DownloadWriter concatenatedWriter(concatenated, 0, 113, 41);
        assert(concatenatedWriter.write(bytes.data(), 701, nullptr, error) == DownloadWriteStatus::Accepted);
        assert(concatenatedWriter.finish(durable, error) && durable == 701);
    }
    concatenated.close();
    assert(concatenated.open(concatenatedPath, StorageKind::Concatenated, true, error, 257));
    {
        DownloadWriter concatenatedWriter(concatenated, 701, 113, 41);
        assert(concatenatedWriter.write(bytes.data() + 701, bytes.size() - 701, nullptr, error) == DownloadWriteStatus::Accepted);
        assert(concatenatedWriter.checkpoint(durable, error) && durable == bytes.size());
        assert(concatenatedWriter.finish(durable, error) && durable == bytes.size());
    }
    concatenated.close();
    LocalFile concatenatedRead;
    assert(concatenatedRead.open(concatenatedPath, StorageKind::Concatenated, false, error, 257));
    std::vector<unsigned char> concatenatedBytes(bytes.size());
    assert(concatenatedRead.readAt(0, concatenatedBytes.data(), concatenatedBytes.size(), error));
    concatenatedRead.close();
    assert(concatenatedBytes == bytes);

    const auto failurePath = root / "writer-failure.bin";
    LocalFile failure;
    assert(failure.create(failurePath, StorageKind::Regular, error));
    failure.close();
    DownloadWriter failedWriter(failure, 0, 64, 16, false);
    error.clear();
    assert(failedWriter.write(bytes.data(), 16, nullptr, error) == DownloadWriteStatus::Failed && !error.empty());
}

void testFreshHttpDownload(const fs::path& root) {
    const auto bytes = payload(2 * 1024 * 1024 + 123);
    LoopbackServer server(okHeaders(bytes.size()), bytes);
    std::string error;
    LocalFile file;
    const auto path = root / "http-fresh.bin";
    assert(file.create(path, StorageKind::Regular, error));
    DownloadWriter writer(file, 0, 32 * 1024, 4096);
    DownloadResult result;
    assert(HttpClient{}.download(server.url(), {}, writer, 0, bytes.size(), {}, [](const std::string& etag) { return etag == "\"test\""; }, {}, result, error));
    assert(result.status == DownloadStatus::Completed);
    assert(result.bytesReceived == bytes.size() && result.bytesWritten == bytes.size() && result.bytesDurable == bytes.size());
    file.close();
    server.wait();
    assert(readFile(path) == bytes);
}

void testResumedHttpDownload(const fs::path& root) {
    const auto bytes = payload(512 * 1024 + 19);
    constexpr size_t resumeAt = 7311;
    std::vector<unsigned char> remainder(bytes.begin() + resumeAt, bytes.end());
    const std::string headers = "HTTP/1.1 206 Partial Content\r\nContent-Length: " + std::to_string(remainder.size()) +
        "\r\nContent-Range: bytes " + std::to_string(resumeAt) + "-" + std::to_string(bytes.size() - 1) + "/" + std::to_string(bytes.size()) +
        "\r\nETag: \"resume\"\r\nConnection: close\r\n\r\n";
    LoopbackServer server(headers, std::move(remainder));
    std::string error;
    LocalFile file;
    const auto path = root / "http-resume.bin";
    assert(file.create(path, StorageKind::Regular, error));
    assert(file.writeAt(0, bytes.data(), resumeAt, error) && file.flush(error));
    DownloadWriter writer(file, resumeAt, 16 * 1024, 2048);
    DownloadResult result;
    assert(HttpClient{}.download(server.url(), {}, writer, resumeAt, bytes.size(), "\"resume\"", [](const std::string&) { return true; }, {}, result, error));
    assert(result.status == DownloadStatus::Completed && result.bytesDurable == bytes.size());
    file.close();
    server.wait();
    assert(server.request().find("Range: bytes=" + std::to_string(resumeAt) + "-") != std::string::npos);
    assert(server.request().find("If-Range: \"resume\"") != std::string::npos);
    assert(readFile(path) == bytes);
}

void testPausedAndTruncatedHttpDownloads(const fs::path& root) {
    const auto bytes = payload(1024 * 1024 + 57);
    {
        LoopbackServer server(okHeaders(bytes.size()), bytes, 1024);
        std::string error;
        LocalFile file;
        const auto path = root / "http-paused.bin";
        assert(file.create(path, StorageKind::Regular, error));
        DownloadWriter writer(file, 0, 8192, 1024);
        DownloadResult result;
        const bool completed = HttpClient{}.download(server.url(), {}, writer, 0, bytes.size(), {}, [](const std::string&) { return true; }, [](uint64_t received) { return received < 32 * 1024; }, result, error);
        assert(!completed && result.status == DownloadStatus::Paused);
        assert(result.bytesReceived > 0 && result.bytesReceived < bytes.size());
        assert(result.bytesReceived == result.bytesWritten && result.bytesWritten == result.bytesDurable);
        file.close();
        server.wait();
        const auto partial = readFile(path);
        assert(partial.size() == result.bytesDurable && std::equal(partial.begin(), partial.end(), bytes.begin()));
    }
    {
        std::vector<unsigned char> truncated(bytes.begin(), bytes.end() - 127);
        LoopbackServer server(okHeaders(bytes.size()), truncated);
        std::string error;
        LocalFile file;
        const auto path = root / "http-truncated.bin";
        assert(file.create(path, StorageKind::Regular, error));
        DownloadWriter writer(file, 0, 8192, 1024);
        DownloadResult result;
        assert(!HttpClient{}.download(server.url(), {}, writer, 0, bytes.size(), {}, [](const std::string&) { return true; }, {}, result, error));
        assert(result.status == DownloadStatus::Failed && !error.empty());
        assert(result.bytesReceived == truncated.size() && result.bytesWritten == truncated.size() && result.bytesDurable == truncated.size());
        file.close();
        server.wait();
        assert(readFile(path) == truncated);
    }
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    i18n::setLanguage(i18n::Language::EnUs);
    const fs::path root = fs::temp_directory_path() / ("switch-drive-download-test-" + makeId());
    fs::create_directories(root);
    testWriter(root);
    testFreshHttpDownload(root);
    testResumedHttpDownload(root);
    testPausedAndTruncatedHttpDownloads(root);
    fs::remove_all(root);
    return 0;
}
