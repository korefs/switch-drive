#include "switchdrive/core.hpp"
#include "switchdrive/network.hpp"

#include <switch.h>
#include <mbedtls/md5.h>
#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace switchdrive;

namespace {

PadState gPad;
#define hidScanInput() padUpdate(&gPad)
#define hidKeysDown(_unused) padGetButtonsDown(&gPad)
#define CONTROLLER_P1_AUTO 0

constexpr const char* kRoot = "sdmc:/switch-drive";
constexpr const char* kDefaultService = "";
constexpr uint64_t kCheckpointBytes = 64ULL * 1024ULL * 1024ULL;
constexpr auto kCheckpointInterval = std::chrono::seconds(10);

void title(const char* page) {
    consoleClear();
    printf("\x1b[36;1mSwitch Drive\x1b[0m  |  %s\n", page);
    printf("────────────────────────────────────────────────────────\n");
}

void hint(const char* text) { printf("\n\x1b[90m%s\x1b[0m\n", text); }

void waitForButton() {
    printf("\nPressione A para continuar.");
    while (appletMainLoop()) {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & HidNpadButton_A) return;
        consoleUpdate(nullptr);
    }
}

std::string configServiceUrl() {
    std::ifstream input(std::string(kRoot) + "/config.json");
    const std::string config((std::istreambuf_iterator<char>(input)), {});
    const std::string marker = "\"service_url\":\"";
    const auto start = config.find(marker);
    if (start == std::string::npos) return kDefaultService;
    const auto begin = start + marker.size();
    const auto end = config.find('"', begin);
    return end == std::string::npos ? "" : config.substr(begin, end - begin);
}

void saveOrShow(StateStore& store, const State& state) {
    std::string error;
    if (!store.save(state, error)) printf("\nErro ao salvar: %s", error.c_str());
}

bool md5File(const fs::path& path, StorageKind kind, std::string& digest, std::string& error) {
    LocalFile file;
    uint64_t size{};
    if (!file.open(path, kind, false, error) || !file.size(size, error)) return false;
    mbedtls_md5_context context;
    mbedtls_md5_init(&context);
    mbedtls_md5_starts(&context);
    std::array<unsigned char, 64 * 1024> buffer{};
    for (uint64_t offset = 0; offset < size;) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - offset));
        if (!file.readAt(offset, buffer.data(), chunk, error)) {
            mbedtls_md5_free(&context);
            return false;
        }
        mbedtls_md5_update(&context, buffer.data(), chunk);
        offset += chunk;
    }
    std::array<unsigned char, 16> raw{};
    mbedtls_md5_finish(&context, raw.data());
    mbedtls_md5_free(&context);
    char output[33]{};
    for (size_t i = 0; i < raw.size(); ++i) std::snprintf(output + i * 2, 3, "%02x", raw[i]);
    digest = output;
    return true;
}

bool connectAccount(StateStore& store, State& state) {
    title("Conectar Google Drive");
    if (state.serviceUrl.empty()) {
        printf("Crie sd:/switch-drive/config.json com a URL HTTPS do serviço.\n");
        waitForButton();
        return false;
    }
    if (state.consolePublicKey.empty()) state.consolePublicKey = makeId() + makeId();
    AuthClient auth(HttpClient{}, state.serviceUrl);
    std::string id, url, code, pollSecret, error;
    if (!auth.begin(state.consolePublicKey, id, url, code, pollSecret, error)) {
        printf("Não foi possível iniciar: %s\n", error.c_str());
        waitForButton();
        return false;
    }
    printf("No celular, abra:\n\x1b[36m%s\x1b[0m\n\nCódigo: \x1b[33;1m%s\x1b[0m\n", url.c_str(), code.c_str());
    hint("A: verificar agora   B: cancelar");
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_B) return false;
        if (pressed & HidNpadButton_A) {
            Account account;
            if (auth.poll(id, pollSecret, account, error) && auth.claim(id, pollSecret, state.sessionToken, account, error)) {
                const auto exists = std::find_if(state.accounts.begin(), state.accounts.end(), [&](const Account& value) { return value.id == account.id; });
                if (exists == state.accounts.end()) state.accounts.push_back(account);
                state.lastAccountId = account.id;
                saveOrShow(store, state);
                printf("\nConectado: %s\n", account.email.c_str());
                waitForButton();
                return true;
            }
            printf("\n%s", error.c_str());
        }
        consoleUpdate(nullptr);
    }
    return false;
}

bool acquireToken(const State& state, std::string& token, std::string& error) {
    if (state.lastAccountId.empty()) {
        error = "Conecte uma conta primeiro";
        return false;
    }
    return AuthClient(HttpClient{}, state.serviceUrl).accessToken(state.sessionToken, state.lastAccountId, token, error);
}

bool sameRemote(const Task& task, const RemoteFile& remote, const std::string& accountId) {
    return task.accountId == accountId && task.remoteId == remote.id && task.revision == remote.revision && task.expectedSize == remote.size && task.md5 == remote.md5;
}

bool hasResumeIdentity(const Task& task) {
    return !task.accountId.empty() && !task.remoteId.empty() && !task.revision.empty() && task.expectedSize > 0 && !task.localPath.empty();
}

Task* findIncompleteTask(State& state, const std::string& accountId, const std::string& remoteId) {
    for (auto& task : state.tasks) {
        if (task.accountId == accountId && task.remoteId == remoteId && task.state != TaskState::Completed && task.state != TaskState::Cancelled) return &task;
    }
    return nullptr;
}

enum class ResumeChoice { Resume, Restart, Cancel };

ResumeChoice askResumeChoice(const Task& task, bool sourceChanged, bool identityMissing) {
    title(sourceChanged ? "Arquivo remoto mudou" : "Download parcial encontrado");
    printf("%s\n", task.displayName.c_str());
    printf("%llu de %llu bytes confirmados\n", static_cast<unsigned long long>(task.committedBytes), static_cast<unsigned long long>(task.expectedSize));
    if (sourceChanged) {
        printf("A versão do Drive mudou. O parcial será preservado até você reiniciar.\n");
        hint("X: reiniciar   B: cancelar");
    } else if (identityMissing) {
        printf("O parcial não tem metadados suficientes para retomar com segurança.\n");
        hint("X: reiniciar   B: cancelar");
    } else {
        hint("A: retomar   X: reiniciar   B: cancelar");
    }
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_X) return ResumeChoice::Restart;
        if (!sourceChanged && !identityMissing && (pressed & HidNpadButton_A)) return ResumeChoice::Resume;
        if (pressed & HidNpadButton_B) return ResumeChoice::Cancel;
        consoleUpdate(nullptr);
    }
    return ResumeChoice::Cancel;
}

bool reconcileTask(Task& task, std::string& error) {
    if (!LocalFile::exists(task.localPath, task.storageKind)) {
        task.committedBytes = 0;
        task.localState = LocalState::NotDownloaded;
        return true;
    }
    LocalFile file;
    uint64_t physicalSize{};
    if (!file.open(task.localPath, task.storageKind, true, error) || !file.size(physicalSize, error)) return false;
    if (physicalSize > task.expectedSize) {
        task.state = TaskState::Failed;
        task.error = "Parcial inválido: maior que o arquivo remoto";
        return false;
    }
    if (physicalSize > task.committedBytes) {
        if (!file.truncate(task.committedBytes, error) || !file.flush(error)) return false;
    } else if (physicalSize < task.committedBytes) {
        task.committedBytes = physicalSize;
    }
    task.localState = task.committedBytes ? LocalState::Present : LocalState::NotDownloaded;
    return true;
}

bool checkpointTask(StateStore& store, State& state, Task& task, LocalFile& file, std::string& error) {
    uint64_t size{};
    if (!file.flush(error) || !file.size(size, error)) return false;
    if (size > task.expectedSize) {
        error = "Parcial inválido: maior que o arquivo remoto";
        return false;
    }
    task.committedBytes = size;
    task.localState = size ? LocalState::Present : LocalState::NotDownloaded;
    return store.save(state, error);
}

bool hasEnoughSpace(uint64_t needed) {
    std::error_code ec;
    const auto space = fs::space("sdmc:/", ec);
    return !ec && space.available >= needed;
}

void applyRemote(Task& task, const RemoteFile& remote, const std::string& accountId, const StateStore& store) {
    task.accountId = accountId;
    task.remoteId = remote.id;
    task.displayName = remote.name;
    task.expectedSize = remote.size;
    task.md5 = remote.md5;
    task.revision = remote.revision;
    task.etag.clear();
    task.storageKind = storageKindForSize(remote.size);
    task.localPath = store.downloadPath(task).string();
    task.committedBytes = 0;
    task.localState = LocalState::NotDownloaded;
    task.error.clear();
}

bool verifyAndRecord(StateStore& store, State& state, Task& task, std::string& error) {
    task.state = TaskState::Verifying;
    saveOrShow(store, state);
    std::string digest;
    if (!md5File(task.localPath, task.storageKind, digest, error)) return false;
    if (!task.md5.empty() && digest != task.md5) {
        error = "checksum MD5 não confere";
        return false;
    }
    task.state = TaskState::Completed;
    task.committedBytes = task.expectedSize;
    task.localState = LocalState::Present;
    const auto existing = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == task.id; });
    if (existing == state.library.end()) {
        LibraryItem item;
        item.id = task.id;
        item.accountId = task.accountId;
        item.remoteId = task.remoteId;
        item.name = task.displayName;
        item.localPath = task.localPath;
        item.md5 = task.md5;
        item.size = task.expectedSize;
        item.localState = LocalState::Present;
        item.storageKind = task.storageKind;
        state.library.push_back(std::move(item));
    }
    return store.save(state, error);
}

void installIfRequested(StateStore& store, State& state, Task& task) {
    if (!task.installAfterDownload || state.library.empty()) return;
    auto library = std::find_if(state.library.begin(), state.library.end(), [&](const LibraryItem& item) { return item.id == task.id; });
    if (library == state.library.end()) return;
    std::string error;
    bool installed = false;
    if (isNro(task.displayName)) {
        NroInstaller installer;
        const std::string baseName = sanitizeFileName(task.displayName.substr(0, task.displayName.size() - 4));
        const fs::path location = std::string("sdmc:/switch/") + baseName + "/" + sanitizeFileName(task.displayName);
        installed = installer.install(task.localPath, location, false, error);
        if (installed) {
            library->installed = InstallKind::Nro;
            library->installedPath = location.string();
        }
    } else if (isNsp(task.displayName)) {
        NspInstaller installer;
        std::string contentId;
        installed = installer.install(task.localPath, task.storageKind, contentId, [](uint64_t, uint64_t) { return true; }, error);
        if (installed) {
            library->installed = InstallKind::Nsp;
            library->installedContentId = contentId;
        }
    } else {
        return;
    }
    if (!installed) {
        printf("Instalação falhou: %s\n", error.c_str());
        return;
    }
    if (task.deleteAfterInstall) {
        if (LocalFile::remove(task.localPath, task.storageKind, error)) {
            task.localState = LocalState::RemovedAfterInstall;
            library->localState = LocalState::RemovedAfterInstall;
        } else {
            printf("Instalado — limpeza pendente: %s\n", error.c_str());
        }
    }
    saveOrShow(store, state);
}

void downloadFile(StateStore& store, State& state, const RemoteFile& remote, bool installAfter) {
    std::string token, error;
    if (!acquireToken(state, token, error)) {
        printf("\n%s", error.c_str());
        waitForButton();
        return;
    }

    Task* task = findIncompleteTask(state, state.lastAccountId, remote.id);
    bool restart = false;
    if (task) {
        const bool changed = !sameRemote(*task, remote, state.lastAccountId);
        const bool missingIdentity = !hasResumeIdentity(*task);
        if (changed || missingIdentity || task->committedBytes) {
            const ResumeChoice choice = askResumeChoice(*task, changed, missingIdentity);
            if (choice == ResumeChoice::Cancel) return;
            restart = choice == ResumeChoice::Restart;
        }
    } else {
        Task newTask;
        newTask.id = makeId();
        newTask.deleteAfterInstall = state.deleteAfterInstall;
        applyRemote(newTask, remote, state.lastAccountId, store);
        state.tasks.push_back(std::move(newTask));
        task = &state.tasks.back();
    }

    if (restart) {
        if (LocalFile::exists(task->localPath, task->storageKind) && !LocalFile::remove(task->localPath, task->storageKind, error)) {
            title("Reiniciar download");
            printf("Não foi possível apagar o parcial: %s\n", error.c_str());
            waitForButton();
            return;
        }
        applyRemote(*task, remote, state.lastAccountId, store);
    }
    task->installAfterDownload = task->installAfterDownload || installAfter;
    task->deleteAfterInstall = state.deleteAfterInstall;
    task->state = TaskState::Queued;
    task->error.clear();

    if (LocalFile::exists(task->localPath, task->storageKind)) {
        if (!reconcileTask(*task, error)) {
            task->state = TaskState::Failed;
            task->error = error;
            saveOrShow(store, state);
            title("Download parcial inválido");
            printf("%s\n", error.c_str());
            waitForButton();
            return;
        }
    }
    if (!hasEnoughSpace(task->expectedSize - task->committedBytes)) {
        task->state = TaskState::Paused;
        task->error = "Espaço insuficiente no microSD";
        saveOrShow(store, state);
        title("Espaço insuficiente");
        printf("Libere espaço e escolha o arquivo novamente para retomar.\n");
        waitForButton();
        return;
    }

    LocalFile output;
    if (LocalFile::exists(task->localPath, task->storageKind)) {
        if (!output.open(task->localPath, task->storageKind, true, error)) {
            task->state = TaskState::Failed;
            task->error = error;
            saveOrShow(store, state);
            return;
        }
    } else if (!output.create(task->localPath, task->storageKind, error)) {
        task->state = TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        title("Erro de armazenamento");
        printf("%s\n", error.c_str());
        waitForButton();
        return;
    }

    task->state = TaskState::Downloading;
    saveOrShow(store, state);
    title(task->committedBytes ? "Retomando download" : "Transferências");
    printf("Baixando %s\n", task->displayName.c_str());
    auto lastCheckpoint = task->committedBytes;
    auto lastCheckpointAt = std::chrono::steady_clock::now();
    DownloadResult result;
    const bool downloaded = task->committedBytes == task->expectedSize || HttpClient{}.download(
        DriveClient(HttpClient{}).mediaUrl(remote),
        {"Authorization: Bearer " + token},
        output,
        task->committedBytes,
        task->expectedSize,
        task->etag,
        [&](const std::string& etag) {
            task->etag = etag;
            return store.save(state, error);
        },
        [&](uint64_t received) {
            printf("\r%llu / %llu bytes   ", static_cast<unsigned long long>(received), static_cast<unsigned long long>(task->expectedSize));
            const auto now = std::chrono::steady_clock::now();
            if (received - lastCheckpoint >= kCheckpointBytes && now - lastCheckpointAt >= kCheckpointInterval) {
                if (!checkpointTask(store, state, *task, output, error)) return false;
                lastCheckpoint = task->committedBytes;
                lastCheckpointAt = now;
            }
            consoleUpdate(nullptr);
            return appletMainLoop();
        },
        result,
        error);

    std::string checkpointError;
    if (!checkpointTask(store, state, *task, output, checkpointError) && error.empty()) error = checkpointError;
    output.close();
    if (!downloaded) {
        task->state = result.status == DownloadStatus::RangeRejected ? TaskState::Failed : TaskState::Paused;
        task->error = error;
        saveOrShow(store, state);
        printf("\n%s: %s\n", result.status == DownloadStatus::RangeRejected ? "Faixa recusada" : "Pausado", error.c_str());
        waitForButton();
        return;
    }

    if (!verifyAndRecord(store, state, *task, error)) {
        task->state = TaskState::Failed;
        task->error = error;
        saveOrShow(store, state);
        printf("\nDownload inválido: %s\n", error.c_str());
        waitForButton();
        return;
    }
    printf("\nDownload concluído.\n");
    installIfRequested(store, state, *task);
    waitForButton();
}

void recoverTasks(StateStore& store, State& state) {
    bool changed = false;
    for (auto& task : state.tasks) {
        if (task.state == TaskState::Completed || task.state == TaskState::Cancelled) continue;
        task.state = TaskState::Paused;
        if (!hasResumeIdentity(task)) {
            task.error = "Parcial sem identidade remota; reinicie o download";
            changed = true;
            continue;
        }
        std::string error;
        if (!reconcileTask(task, error)) {
            task.state = TaskState::Failed;
            task.error = error;
        } else if (task.committedBytes == task.expectedSize) {
            task.state = TaskState::Paused;
            task.error = "Download completo aguardando verificação";
        } else {
            task.error = "Download interrompido; selecione o arquivo para retomar";
        }
        changed = true;
    }
    if (changed) saveOrShow(store, state);
}

void browse(StateStore& store, State& state) {
    std::string token, error;
    if (!acquireToken(state, token, error)) {
        title("Arquivos");
        printf("%s\n", error.c_str());
        waitForButton();
        return;
    }
    DriveClient drive(HttpClient{});
    std::string folder = state.lastFolderId.empty() ? "root" : state.lastFolderId;
    std::vector<std::string> parents;
    bool shared = false;
    size_t selected = 0;
    while (appletMainLoop()) {
        std::vector<RemoteFile> files;
        std::string next;
        if (!drive.list(token, folder, shared, "", files, next, error)) {
            title("Arquivos");
            printf("Erro do Drive: %s\n", error.c_str());
            waitForButton();
            return;
        }
        title(shared ? "Compartilhados comigo" : "Meu Drive");
        printf("Conta: %s\n\n", state.lastAccountId.c_str());
        if (files.empty()) printf("Pasta vazia.\n");
        for (size_t i = 0; i < files.size() && i < 20; ++i) {
            const auto& file = files[i];
            printf("%s %c %-42s %10llu\n", i == selected ? ">" : " ", file.folder ? 'D' : 'F', file.name.c_str(), static_cast<unsigned long long>(file.size));
        }
        hint("A: abrir  X: baixar  Y: baixar e instalar  L: compartilhados  B: voltar");
        bool refresh = false;
        while (appletMainLoop() && !refresh) {
            hidScanInput();
            const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
            if (pressed & HidNpadButton_Down) { if (!files.empty()) selected = (selected + 1) % files.size(); refresh = true; }
            if (pressed & HidNpadButton_Up) { if (!files.empty()) selected = (selected + files.size() - 1) % files.size(); refresh = true; }
            if (pressed & HidNpadButton_L) { shared = !shared; folder = "root"; parents.clear(); selected = 0; refresh = true; }
            if (pressed & HidNpadButton_B) { if (parents.empty()) return; folder = parents.back(); parents.pop_back(); shared = false; selected = 0; refresh = true; }
            if (files.empty()) continue;
            auto& file = files[selected];
            if (pressed & HidNpadButton_A && file.folder) { parents.push_back(folder); folder = file.id; shared = false; selected = 0; refresh = true; }
            if ((pressed & HidNpadButton_X) && file.canDownload && !file.folder) { downloadFile(store, state, file, false); refresh = true; }
            if ((pressed & HidNpadButton_Y) && file.canDownload && !file.folder) { downloadFile(store, state, file, true); refresh = true; }
            consoleUpdate(nullptr);
        }
        state.lastFolderId = folder;
        saveOrShow(store, state);
    }
}

void library(StateStore& store, State& state) {
    title("Biblioteca");
    if (state.library.empty()) printf("Nenhum download indexado.\n");
    for (size_t i = 0; i < state.library.size(); ++i) {
        const auto& item = state.library[i];
        printf("%zu. %s  [%s]\n", i + 1, item.name.c_str(), item.localState == LocalState::Present ? "arquivo local" : "removido após instalar");
    }
    hint("A: verificar primeiro item  B: voltar");
    while (appletMainLoop()) {
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if ((pressed & HidNpadButton_A) && !state.library.empty()) {
            auto& item = state.library[0];
            if (item.localState == LocalState::Present && !LocalFile::exists(item.localPath, item.storageKind)) {
                printf("\nEste arquivo não foi encontrado. Deseja excluir o atalho?  X: excluir\n");
                while (appletMainLoop()) {
                    hidScanInput();
                    const auto confirmation = hidKeysDown(CONTROLLER_P1_AUTO);
                    if (confirmation & HidNpadButton_X) { state.library.erase(state.library.begin()); saveOrShow(store, state); return; }
                    if (confirmation & HidNpadButton_B) break;
                    consoleUpdate(nullptr);
                }
            }
            return;
        }
        if (pressed & HidNpadButton_B) return;
        consoleUpdate(nullptr);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&gPad);
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    StateStore store(kRoot);
    State state = store.load();
    if (state.serviceUrl.empty()) state.serviceUrl = configServiceUrl();
    recoverTasks(store, state);

    int page = 0;
    while (appletMainLoop()) {
        title(page == 0 ? "Início" : page == 1 ? "Arquivos" : page == 2 ? "Biblioteca" : "Configurações");
        if (page == 0) {
            const std::string accountText = state.accounts.empty() ? "Nenhuma conta conectada." : "Conta ativa: " + state.lastAccountId;
            printf("%s\n", accountText.c_str());
            printf("\nA: conectar conta     X: abrir arquivos\n");
        } else if (page == 1) {
            printf("A: abrir navegador do Drive\n");
        } else if (page == 2) {
            printf("A: abrir biblioteca (%zu itens)\n", state.library.size());
        } else {
            printf("Limpar após instalar: %s\nA: alternar     X: adicionar conta\n", state.deleteAfterInstall ? "sim" : "não");
        }
        hint("L/R: trocar tela   +: sair");
        hidScanInput();
        const auto pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if (pressed & HidNpadButton_Plus) break;
        if (pressed & HidNpadButton_R) page = (page + 1) % 4;
        if (pressed & HidNpadButton_L) page = (page + 3) % 4;
        if (page == 0 && (pressed & HidNpadButton_A)) connectAccount(store, state);
        if (page == 0 && (pressed & HidNpadButton_X)) browse(store, state);
        if (page == 1 && (pressed & HidNpadButton_A)) browse(store, state);
        if (page == 2 && (pressed & HidNpadButton_A)) library(store, state);
        if (page == 3 && (pressed & HidNpadButton_A)) { state.deleteAfterInstall = !state.deleteAfterInstall; saveOrShow(store, state); }
        if (page == 3 && (pressed & HidNpadButton_X)) connectAccount(store, state);
        consoleUpdate(nullptr);
    }
    curl_global_cleanup();
    socketExit();
    consoleExit(nullptr);
    return 0;
}
