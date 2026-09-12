#include "switchdrive/core.hpp"
#include "switchdrive/network.hpp"

#include <switch.h>
#include <mbedtls/md5.h>
#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;
using namespace switchdrive;

namespace {
PadState gPad;
#define hidScanInput() padUpdate(&gPad)
#define hidKeysDown(_unused) padGetButtonsDown(&gPad)
#define CONTROLLER_P1_AUTO 0
constexpr const char* kRoot = "sdmc:/switch-drive";
constexpr const char* kDefaultService = "";

void title(const char* page) {
    consoleClear();
    printf("\x1b[36;1mSwitch Drive\x1b[0m  |  %s\n", page);
    printf("────────────────────────────────────────────────────────\n");
}
void hint(const char* text) { printf("\n\x1b[90m%s\x1b[0m\n", text); }
void waitForButton() { printf("\nPressione A para continuar."); while (appletMainLoop()) { hidScanInput(); if (hidKeysDown(CONTROLLER_P1_AUTO) & HidNpadButton_A) return; consoleUpdate(nullptr); } }
std::string md5File(const fs::path& path) {
    FILE* file=std::fopen(path.string().c_str(),"rb"); if(!file)return {}; mbedtls_md5_context context; mbedtls_md5_init(&context); mbedtls_md5_starts(&context); std::array<unsigned char,64*1024> buffer{}; size_t read{}; while((read=std::fread(buffer.data(),1,buffer.size(),file))>0)mbedtls_md5_update(&context,buffer.data(),read); std::fclose(file); std::array<unsigned char,16> digest{};mbedtls_md5_finish(&context,digest.data());mbedtls_md5_free(&context);char output[33]{};for(size_t i=0;i<digest.size();++i)std::snprintf(output+i*2,3,"%02x",digest[i]);return output;
}
std::string configServiceUrl() {
    std::ifstream input(std::string(kRoot)+"/config.json"); std::string config((std::istreambuf_iterator<char>(input)),{}); const std::string marker="\"service_url\":\""; const auto start=config.find(marker); if(start==std::string::npos)return kDefaultService; const auto begin=start+marker.size(); const auto end=config.find('"',begin); return end==std::string::npos?"":config.substr(begin,end-begin);
}
void saveOrShow(StateStore& store,const State& state) { std::string error; if(!store.save(state,error))printf("\nErro ao salvar: %s",error.c_str()); }

bool connectAccount(StateStore& store, State& state) {
    title("Conectar Google Drive");
    if (state.serviceUrl.empty()) { printf("Crie sd:/switch-drive/config.json com a URL HTTPS do serviço.\n"); waitForButton(); return false; }
    if (state.consolePublicKey.empty()) state.consolePublicKey=makeId()+makeId();
    AuthClient auth(HttpClient{},state.serviceUrl); std::string id,url,code,pollSecret,error;
    if(!auth.begin(state.consolePublicKey,id,url,code,pollSecret,error)){printf("Não foi possível iniciar: %s\n",error.c_str());waitForButton();return false;}
    printf("No celular, abra:\n\x1b[36m%s\x1b[0m\n\nCódigo: \x1b[33;1m%s\x1b[0m\n",url.c_str(),code.c_str());
    hint("A: verificar agora   B: cancelar");
    while(appletMainLoop()){hidScanInput();auto pressed=hidKeysDown(CONTROLLER_P1_AUTO);if(pressed&HidNpadButton_B)return false;if(pressed&HidNpadButton_A){Account account;if(auth.poll(id,pollSecret,account,error)){if(auth.claim(id,pollSecret,state.sessionToken,account,error)){auto exists=std::find_if(state.accounts.begin(),state.accounts.end(),[&](const Account& v){return v.id==account.id;});if(exists==state.accounts.end())state.accounts.push_back(account);state.lastAccountId=account.id;saveOrShow(store,state);printf("\nConectado: %s\n",account.email.c_str());waitForButton();return true;}}printf("\n%s",error.c_str());}consoleUpdate(nullptr);}return false;
}

bool acquireToken(const State& state,std::string& token,std::string& error) { if(state.lastAccountId.empty()){error="Conecte uma conta primeiro";return false;}return AuthClient(HttpClient{},state.serviceUrl).accessToken(state.sessionToken,state.lastAccountId,token,error); }

void downloadFile(StateStore& store, State& state, const RemoteFile& remote, bool installAfter) {
    std::string token,error; if(!acquireToken(state,token,error)){printf("\n%s",error.c_str());waitForButton();return;}
    Task task;task.id=makeId();task.accountId=state.lastAccountId;task.remoteId=remote.id;task.displayName=remote.name;task.expectedSize=remote.size;task.md5=remote.md5;task.localPath=store.downloadPath(task).string();task.installAfterDownload=installAfter;task.deleteAfterInstall=state.deleteAfterInstall;task.state=TaskState::Downloading;state.tasks.push_back(task);saveOrShow(store,state);
    title("Transferências");printf("Baixando %s\n",remote.name.c_str());auto partial=fileSize(task.localPath);bool ok=HttpClient{}.download(DriveClient(HttpClient{}).mediaUrl(remote),{"Authorization: Bearer "+token},task.localPath,partial,task.expectedSize,[&](uint64_t got){printf("\r%llu / %llu bytes   ",static_cast<unsigned long long>(got),static_cast<unsigned long long>(task.expectedSize));consoleUpdate(nullptr);return appletMainLoop();},error);
    auto& persisted=state.tasks.back();if(!ok){persisted.state=TaskState::Paused;persisted.downloaded=fileSize(task.localPath);persisted.error=error;saveOrShow(store,state);printf("\nPausado: %s\n",error.c_str());waitForButton();return;}
    persisted.state=TaskState::Verifying;persisted.downloaded=task.expectedSize;saveOrShow(store,state);if(!remote.md5.empty()&&md5File(task.localPath)!=remote.md5){persisted.state=TaskState::Failed;persisted.error="checksum MD5 não confere";saveOrShow(store,state);printf("\nDownload inválido: checksum não confere.\n");waitForButton();return;}
    persisted.localState=LocalState::Present;persisted.state=TaskState::Completed;LibraryItem item;item.id=task.id;item.accountId=task.accountId;item.remoteId=task.remoteId;item.name=task.displayName;item.localPath=task.localPath;item.md5=task.md5;item.size=task.expectedSize;item.localState=LocalState::Present;state.library.push_back(item);saveOrShow(store,state);
    printf("\nDownload concluído.\n");if(installAfter){if(isNro(remote.name)){NroInstaller installer;std::string location=std::string("sdmc:/switch/")+sanitizeFileName(remote.name.substr(0,remote.name.size()-4))+"/"+sanitizeFileName(remote.name);if(installer.install(task.localPath,location,false,error)){auto& saved=state.library.back();saved.installed=InstallKind::Nro;saved.installedPath=location;if(state.deleteAfterInstall){std::error_code ec;fs::remove(task.localPath,ec);saved.localState=LocalState::RemovedAfterInstall;}saveOrShow(store,state);printf("Homebrew instalada.\n");}else printf("Instalação NRO falhou: %s\n",error.c_str());}else if(isNsp(remote.name)){std::string contentId;NspInstaller installer;if(installer.install(task.localPath,contentId,[](uint64_t,uint64_t){return true;},error)){auto& saved=state.library.back();saved.installed=InstallKind::Nsp;saved.installedContentId=contentId;if(state.deleteAfterInstall){std::error_code ec;fs::remove(task.localPath,ec);saved.localState=LocalState::RemovedAfterInstall;}saveOrShow(store,state);}else printf("Instalação NSP falhou: %s\n",error.c_str());}}waitForButton();
}

void browse(StateStore& store, State& state) {
    std::string token,error;if(!acquireToken(state,token,error)){title("Arquivos");printf("%s\n",error.c_str());waitForButton();return;}DriveClient drive(HttpClient{});std::string folder=state.lastFolderId.empty()?"root":state.lastFolderId;std::vector<std::string> parents;bool shared=false;size_t selected=0;
    while(appletMainLoop()){std::vector<RemoteFile> files;std::string next;if(!drive.list(token,folder,shared,"",files,next,error)){title("Arquivos");printf("Erro do Drive: %s\n",error.c_str());waitForButton();return;}title(shared?"Compartilhados comigo":"Meu Drive");printf("Conta: %s\n\n",state.lastAccountId.c_str());if(files.empty())printf("Pasta vazia.\n");for(size_t i=0;i<files.size()&&i<20;++i){const auto& f=files[i];printf("%s %c %-42s %10llu\n",i==selected?">":" ",f.folder?'D':'F',f.name.c_str(),static_cast<unsigned long long>(f.size));}hint("A: abrir  X: baixar  Y: baixar e instalar  L: compartilhados  B: voltar");bool refresh=false;while(appletMainLoop()&&!refresh){hidScanInput();auto p=hidKeysDown(CONTROLLER_P1_AUTO);if(p&HidNpadButton_Down){if(!files.empty())selected=(selected+1)%files.size();refresh=true;}if(p&HidNpadButton_Up){if(!files.empty())selected=(selected+files.size()-1)%files.size();refresh=true;}if(p&HidNpadButton_L){shared=!shared;folder="root";parents.clear();selected=0;refresh=true;}if(p&HidNpadButton_B){if(parents.empty())return;folder=parents.back();parents.pop_back();shared=false;selected=0;refresh=true;}if(files.empty())continue;auto& f=files[selected];if(p&HidNpadButton_A&&f.folder){parents.push_back(folder);folder=f.id;shared=false;selected=0;refresh=true;}if((p&HidNpadButton_X)&&f.canDownload&&!f.folder){downloadFile(store,state,f,false);refresh=true;}if((p&HidNpadButton_Y)&&f.canDownload&&!f.folder){downloadFile(store,state,f,true);refresh=true;}consoleUpdate(nullptr);}state.lastFolderId=folder;saveOrShow(store,state);}
}

void library(StateStore& store,State& state){title("Biblioteca");if(state.library.empty())printf("Nenhum download indexado.\n");for(size_t i=0;i<state.library.size();++i){auto& item=state.library[i];printf("%zu. %s  [%s]\n",i+1,item.name.c_str(),item.localState==LocalState::Present?"arquivo local":"removido após instalar");}hint("A: verificar primeiro item  B: voltar");while(appletMainLoop()){hidScanInput();if(hidKeysDown(CONTROLLER_P1_AUTO)&HidNpadButton_A){if(!state.library.empty()&&state.library[0].localState==LocalState::Present&&!fileExists(state.library[0].localPath)){printf("\nEste arquivo não foi encontrado. Deseja excluir o atalho?  X: excluir\n");while(appletMainLoop()){hidScanInput();auto p=hidKeysDown(CONTROLLER_P1_AUTO);if(p&HidNpadButton_X){state.library.erase(state.library.begin());saveOrShow(store,state);return;}if(p&HidNpadButton_B)break;consoleUpdate(nullptr);}}return;}if(hidKeysDown(CONTROLLER_P1_AUTO)&HidNpadButton_B)return;consoleUpdate(nullptr);}}
}

int main(int argc, char* argv[]) {
    (void)argc;(void)argv;consoleInit(nullptr);padConfigureInput(1, HidNpadStyleSet_NpadStandard);padInitializeDefault(&gPad);socketInitializeDefault();curl_global_init(CURL_GLOBAL_DEFAULT);StateStore store(kRoot);State state=store.load();if(state.serviceUrl.empty())state.serviceUrl=configServiceUrl();
    int page=0;while(appletMainLoop()){title(page==0?"Início":page==1?"Arquivos":page==2?"Biblioteca":"Configurações");if(page==0){const std::string accountText=state.accounts.empty()?"Nenhuma conta conectada.":"Conta ativa: "+state.lastAccountId;printf("%s\n",accountText.c_str());printf("\nA: conectar conta     X: abrir arquivos\n");}else if(page==1){printf("A: abrir navegador do Drive\n");}else if(page==2){printf("A: abrir biblioteca (%zu itens)\n",state.library.size());}else{printf("Limpar após instalar: %s\nA: alternar     X: adicionar conta\n",state.deleteAfterInstall?"sim":"não");}hint("L/R: trocar tela   +: sair");hidScanInput();auto p=hidKeysDown(CONTROLLER_P1_AUTO);if(p&HidNpadButton_Plus)break;if(p&HidNpadButton_R){page=(page+1)%4;}if(p&HidNpadButton_L){page=(page+3)%4;}if(page==0&&p&HidNpadButton_A)connectAccount(store,state);if(page==0&&p&HidNpadButton_X)browse(store,state);if(page==1&&p&HidNpadButton_A)browse(store,state);if(page==2&&p&HidNpadButton_A)library(store,state);if(page==3&&p&HidNpadButton_A){state.deleteAfterInstall=!state.deleteAfterInstall;saveOrShow(store,state);}if(page==3&&p&HidNpadButton_X)connectAccount(store,state);consoleUpdate(nullptr);}
    curl_global_cleanup();socketExit();consoleExit(nullptr);return 0;
}
