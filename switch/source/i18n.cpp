// SPDX-License-Identifier: GPL-3.0-or-later
#include "switchdrive/i18n.hpp"

#include <array>

namespace switchdrive::i18n {
namespace {
using Catalog = std::array<const char*, textCount()>;

#define SD_TEXTS(X) \
 X("Switch Drive", "Switch Drive", "Switch Drive") \
 X("Press A to continue.", "Pressione A para continuar.", "Pulsa A para continuar.") \
 X("Install NSP", "Instalar NSP", "Instalar NSP") \
 X("Base game", "Jogo base", "Juego base") \
 X("Update", "Atualização", "Actualización") \
 X("DLC", "DLC", "DLC") \
 X("Title: %s", "Título: %s", "Título: %s") \
 X("Version: %u", "Versão: %u", "Versión: %u") \
 X("Installed: version %u (%s)", "Instalado: versão %u (%s)", "Instalado: versión %u (%s)") \
 X("microSD", "microSD", "microSD") \
 X("Internal storage", "Memória interna", "Memoria interna") \
 X("Confirm", "Confirmar", "Confirmar") \
 X("Cancel", "Cancelar", "Cancelar") \
 X("A: confirm  B: cancel  Up/Down: destination", "A: confirmar  B: cancelar  Cima/Baixo: destino", "A: confirmar  B: cancelar  Arriba/Abajo: destino") \
 X("Connect Google Drive", "Conectar Google Drive", "Conectar Google Drive") \
 X("Create sd:/switch-drive/config.json with the service HTTPS URL.", "Crie sd:/switch-drive/config.json com a URL HTTPS do serviço.", "Crea sd:/switch-drive/config.json con la URL HTTPS del servicio.") \
 X("Could not start: %s", "Não foi possível iniciar: %s", "No se pudo iniciar: %s") \
 X("On your phone, open:", "No celular, abra:", "En tu teléfono, abre:") \
 X("Code", "Código", "Código") \
 X("A: check now   B: cancel", "A: verificar agora   B: cancelar", "A: comprobar ahora   B: cancelar") \
 X("Scan with your phone to continue.", "Escaneie com o celular para continuar.", "Escanea con tu teléfono para continuar.") \
 X("Connected: %s", "Conectado: %s", "Conectado: %s") \
 X("Connect an account first", "Conecte uma conta primeiro", "Conecta una cuenta primero") \
 X("Remote file changed", "Arquivo remoto mudou", "El archivo remoto cambió") \
 X("Partial download found", "Download parcial encontrado", "Descarga parcial encontrada") \
 X("%llu of %llu bytes confirmed", "%llu de %llu bytes confirmados", "%llu de %llu bytes confirmados") \
 X("The Drive version changed. The partial file is preserved until restart.", "A versão do Drive mudou. O parcial será preservado até você reiniciar.", "La versión de Drive cambió. El archivo parcial se conservará hasta reiniciar.") \
 X("The partial file is preserved until restart.", "O parcial será preservado até você reiniciar.", "El archivo parcial se conservará hasta reiniciar.") \
 X("Restart", "Reiniciar", "Reiniciar") \
 X("Resume", "Retomar", "Reanudar") \
 X("The partial file lacks enough metadata to resume safely.", "O parcial não tem metadados suficientes para retomar com segurança.", "El archivo parcial no tiene metadatos suficientes para reanudar con seguridad.") \
 X("Invalid partial: larger than remote file", "Parcial inválido: maior que o arquivo remoto", "Parcial inválido: mayor que el archivo remoto") \
 X("The download destination already exists", "O destino de download já existe", "El destino de descarga ya existe") \
 X("MD5 checksum does not match", "checksum MD5 não confere", "La suma MD5 no coincide") \
 X("Installation failed: %s", "Instalação falhou: %s", "La instalación falló: %s") \
 X("Installed — cleanup pending: %s", "Instalado — limpeza pendente: %s", "Instalado — limpieza pendiente: %s") \
 X("Restart download", "Reiniciar download", "Reiniciar descarga") \
 X("Could not delete partial file: %s", "Não foi possível apagar o parcial: %s", "No se pudo eliminar el archivo parcial: %s") \
 X("Invalid partial download", "Download parcial inválido", "Descarga parcial inválida") \
 X("Insufficient space", "Espaço insuficiente", "Espacio insuficiente") \
 X("Free space and select the file again to resume.", "Libere espaço e escolha o arquivo novamente para retomar.", "Libera espacio y vuelve a seleccionar el archivo para reanudar.") \
 X("Storage error", "Erro de armazenamento", "Error de almacenamiento") \
 X("Resuming download", "Retomando download", "Reanudando descarga") \
 X("Transfers", "Transferências", "Transferencias") \
 X("Downloading %s", "Baixando %s", "Descargando %s") \
 X("Download complete.", "Download concluído.", "Descarga completada.") \
 X("Invalid download: %s", "Download inválido: %s", "Descarga inválida: %s") \
 X("Paused", "Pausado", "Pausado") \
 X("Range rejected", "Faixa recusada", "Rango rechazado") \
 X("Download interrupted; select the file to resume", "Download interrompido; selecione o arquivo para retomar", "Descarga interrumpida; selecciona el archivo para reanudar") \
 X("Select a file to resume", "Selecione um arquivo para retomar", "Selecciona un archivo para reanudar") \
 X("Complete download awaiting verification", "Download completo aguardando verificação", "Descarga completa esperando verificación") \
 X("NSP recovery", "Recuperação NSP", "Recuperación NSP") \
 X("No NSP installation will start.", "Nenhuma instalação NSP será iniciada.", "No se iniciará ninguna instalación NSP.") \
 X("Files", "Arquivos", "Archivos") \
 X("Drive error: %s", "Erro do Drive: %s", "Error de Drive: %s") \
 X("Shared with me", "Compartilhados comigo", "Compartidos conmigo") \
 X("My Drive", "Meu Drive", "Mi Drive") \
 X("Folder is empty.", "Pasta vazia.", "La carpeta está vacía.") \
 X("A: open  X: download  Y: download and install  L: shared  B: back", "A: abrir  X: baixar  Y: baixar e instalar  L: compartilhados  B: voltar", "A: abrir  X: descargar  Y: descargar e instalar  L: compartidos  B: volver") \
 X("Library", "Biblioteca", "Biblioteca") \
 X("No indexed downloads.", "Nenhum download indexado.", "No hay descargas indexadas.") \
 X("local file", "arquivo local", "archivo local") \
 X("removed after install", "removido após instalar", "eliminado tras instalar") \
 X("managed NSP installed", "NSP gerenciado instalado", "NSP administrado instalado") \
 X("A: check  Y: remove managed NSP  B: back", "A: verificar  Y: remover NSP gerenciado  B: voltar", "A: comprobar  Y: eliminar NSP administrado  B: volver") \
 X("File missing", "Arquivo ausente", "Archivo ausente") \
 X("This file was not found. Remove the shortcut?  X: remove", "Este arquivo não foi encontrado. Deseja excluir o atalho?  X: excluir", "No se encontró este archivo. ¿Eliminar el acceso directo?  X: eliminar") \
 X("Remove shortcut", "Excluir atalho", "Eliminar acceso directo") \
 X("Remove NSP", "Remover NSP", "Eliminar NSP") \
 X("There is no managed NSP to remove.", "Não há NSP gerenciado para remover.", "No hay NSP administrado para eliminar.") \
 X("Updates and DLC will not be removed. Saves are preserved.", "Atualizações e DLC não serão removidos. Os saves serão preservados.", "Las actualizaciones y DLC no se eliminarán. Las partidas se conservarán.") \
 X("X: confirm removal   B: cancel", "X: confirmar remoção   B: cancelar", "X: confirmar eliminación   B: cancelar") \
 X("Removal failed: %s", "Remoção falhou: %s", "La eliminación falló: %s") \
 X("Home", "Início", "Inicio") \
 X("Settings", "Configurações", "Configuración") \
 X("No account connected.", "Nenhuma conta conectada.", "No hay ninguna cuenta conectada.") \
 X("Active account: %s", "Conta ativa: %s", "Cuenta activa: %s") \
 X("A: connect account     X: open files", "A: conectar conta     X: abrir arquivos", "A: conectar cuenta     X: abrir archivos") \
 X("Cleanup after install: %s", "Limpar após instalar: %s", "Limpiar después de instalar: %s") \
 X("yes", "sim", "sí") \
 X("no", "não", "no") \
 X("A: toggle cleanup     X: add account", "A: alternar limpeza     X: adicionar conta", "A: cambiar limpieza     X: añadir cuenta") \
 X("X: add account", "X: adicionar conta", "X: añadir cuenta") \
 X("Y: change language", "Y: trocar idioma", "Y: cambiar idioma") \
 X("Stick / D-pad: move   A: select   B: menu   L/R: section   +: exit", "Analógico / direcional: mover   A: selecionar   B: menu   L/R: seção   +: sair", "Stick / cruceta: mover   A: elegir   B: menú   L/R: sección   +: salir") \
 X("Exit", "Sair", "Salir") \
 X("Language", "Idioma", "Idioma") \
 X("Invalid NRO: NRO0 header missing", "NRO inválido: cabeçalho NRO0 ausente", "NRO inválido: falta la cabecera NRO0") \
 X("Invalid NRO: size outside limit", "NRO inválido: tamanho fora do limite", "NRO inválido: tamaño fuera del límite") \
 X("A homebrew with this name already exists", "Já existe uma homebrew com esse nome", "Ya existe un homebrew con este nombre") \
 X("Ambiguous NSP: more than one CNMT", "NSP ambíguo: mais de um CNMT", "NSP ambiguo: más de un CNMT") \
 X("Invalid NSP: NCA or CNMT missing", "NSP inválido: faltam NCA ou CNMT", "NSP inválido: faltan NCA o CNMT") \
 X("Ticketed NSP has no certificate", "NSP com ticket não contém certificado", "El NSP con ticket no contiene certificado") \
 X("Could not import NSP ticket", "não foi possível importar o ticket NSP", "no se pudo importar el ticket NSP") \
 X("CNMT truncated", "CNMT truncado", "CNMT truncado") \
 X("Invalid CNMT: content list", "CNMT inválido: lista de conteúdo", "CNMT inválido: lista de contenido") \
 X("Invalid base-game CNMT", "CNMT de jogo inválido", "CNMT de juego inválido") \
 X("Invalid update CNMT", "CNMT de atualização inválido", "CNMT de actualización inválido") \
 X("Invalid DLC CNMT", "CNMT de DLC inválido", "CNMT de DLC inválido") \
 X("Unsupported CNMT (base game, update, and DLC only)", "CNMT não suportado (somente jogo, atualização ou DLC)", "CNMT no compatible (solo juego base, actualización o DLC)") \
 X("CNMT contains an empty NCA", "CNMT contém NCA vazio", "CNMT contiene un NCA vacío") \
 X("CNMT has no installable content", "CNMT sem conteúdo instalável", "CNMT no tiene contenido instalable") \
 X("CNMT missing", "CNMT ausente", "Falta CNMT") \
 X("Reading encrypted CNMT requires a Nintendo Switch with Atmosphère", "A leitura do CNMT criptografado exige um Nintendo Switch com Atmosphère", "Leer CNMT cifrado requiere una Nintendo Switch con Atmosphère") \
 X("Could not mount SystemContent", "não foi possível montar SystemContent", "no se pudo montar SystemContent") \
 X("Could not prepare CNMT", "não foi possível preparar o CNMT", "no se pudo preparar el CNMT") \
 X("Could not open CNMT; check Atmosphère and patches", "não foi possível abrir o CNMT; verifique Atmosphère e patches", "no se pudo abrir el CNMT; comprueba Atmosphère y parches") \
 X("CNMT file missing", "arquivo CNMT ausente", "falta el archivo CNMT") \
 X("Invalid NSP: CNMT NCA missing or wrong size", "NSP inválido: NCA do CNMT ausente ou com tamanho incorreto", "NSP inválido: falta NCA del CNMT o tiene tamaño incorrecto") \
 X("NSP query requires a Nintendo Switch with Atmosphère", "Consulta NSP exige um Nintendo Switch com Atmosphère", "La consulta NSP requiere una Nintendo Switch con Atmosphère") \
 X("NCM unavailable; run under Atmosphère in application mode", "NCM indisponível; execute pelo Atmosphère em modo aplicação", "NCM no disponible; ejecuta Atmosphère en modo aplicación") \
 X("NS unavailable; run under Atmosphère in application mode", "NS indisponível; execute pelo Atmosphère em modo aplicação", "NS no disponible; ejecuta Atmosphère en modo aplicación") \
 X("Could not query installed content", "não foi possível consultar conteúdo instalado", "no se pudo consultar el contenido instalado") \
 X("NSP installation requires a Nintendo Switch with Atmosphère", "Instalação NSP exige um Nintendo Switch com Atmosphère", "La instalación NSP requiere una Nintendo Switch con Atmosphère") \
 X("Update rejected: an installed version is newer", "Atualização recusada: a versão instalada é mais nova", "Actualización rechazada: la versión instalada es más nueva") \
 X("Metadata NCA missing", "NCA de metadados ausente", "Falta el NCA de metadatos") \
 X("Could not open installation destination", "não foi possível abrir o destino de instalação", "no se pudo abrir el destino de instalación") \
 X("Not enough space at destination", "espaço insuficiente no destino", "espacio insuficiente en el destino") \
 X("Invalid NCA identifier", "identificador NCA inválido", "identificador NCA inválido") \
 X("Could not query NCA", "não foi possível consultar NCA", "no se pudo consultar NCA") \
 X("Could not create NCM temporary space", "não foi possível criar espaço temporário NCM", "no se pudo crear espacio temporal NCM") \
 X("Could not reserve NCA", "não foi possível reservar NCA", "no se pudo reservar NCA") \
 X("NCA missing during installation", "NCA ausente durante instalação", "falta NCA durante la instalación") \
 X("Failed to write NCA", "falha ao gravar NCA", "fallo al escribir NCA") \
 X("Installation cancelled", "instalação cancelada", "instalación cancelada") \
 X("Failed to register NCA", "falha ao registrar NCA", "fallo al registrar NCA") \
 X("Failed to commit NCM metadata", "falha ao confirmar metadados NCM", "fallo al confirmar metadatos NCM") \
 X("Content installed; journal could not be updated", "conteúdo instalado; diário não pôde ser atualizado", "contenido instalado; no se pudo actualizar el diario") \
 X("NSP recovery requires the console", "Recuperação NSP exige o console", "La recuperación NSP requiere la consola") \
 X("NCM unavailable during recovery", "NCM indisponível durante recuperação", "NCM no disponible durante la recuperación") \
 X("Could not verify NSP commit", "não foi possível verificar o commit NSP", "no se pudo verificar la confirmación NSP") \
 X("NSP removal requires the console", "Desinstalação NSP exige o console", "La eliminación NSP requiere la consola") \
 X("Managed content missing", "conteúdo gerenciado ausente", "falta contenido administrado") \
 X("Could not open metadata", "não foi possível abrir metadados", "no se pudieron abrir los metadatos") \
 X("Managed version is no longer installed", "a versão gerenciada não está mais instalada", "la versión administrada ya no está instalada") \
 X("Could not enumerate content to remove", "não foi possível enumerar o conteúdo a remover", "no se pudo enumerar el contenido a eliminar") \
 X("Metadata removal failed; content was preserved", "falha ao remover metadados; o conteúdo foi preservado", "fallo al eliminar metadatos; el contenido se conservó") \
 X("HTTP response did not authorize writing", "resposta HTTP não autorizou escrita", "la respuesta HTTP no autorizó la escritura") \
 X("Server did not confirm download range", "servidor não confirmou a faixa do download", "el servidor no confirmó el rango de descarga") \
 X("Could not record response", "não foi possível registrar a resposta do download", "no se pudo registrar la respuesta de descarga") \
 X("Invalid service JSON", "JSON inválido do serviço", "JSON de servicio inválido") \
 X("curl unavailable", "curl indisponível", "curl no disponible") \
 X("download paused", "download pausado", "descarga pausada") \
 X("download range rejected", "faixa de download recusada", "rango de descarga rechazado") \
 X("downloaded size differs from expected", "tamanho baixado diferente do esperado", "el tamaño descargado difiere del esperado") \
 X("incomplete pairing response", "resposta de pareamento incompleta", "respuesta de vinculación incompleta") \
 X("awaiting authorization", "aguardando autorização", "esperando autorización") \
 X("access token missing", "token de acesso ausente", "falta el token de acceso") \
 X("Could not save state: %s", "Não foi possível salvar o estado: %s", "No se pudo guardar el estado: %s") \
 X("Account: %s", "Conta: %s", "Cuenta: %s") \
 X("A: open Drive browser", "A: abrir navegador do Drive", "A: abrir explorador de Drive") \
 X("A: open library (%zu items)", "A: abrir biblioteca (%zu itens)", "A: abrir biblioteca (%zu elementos)") \
 X("%zu items", "%zu itens", "%zu elementos") \
 X("Installing %llu / %llu bytes", "Instalando %llu / %llu bytes", "Instalando %llu / %llu bytes") \
 X(", managed NSP installed", ", NSP gerenciado instalado", ", NSP administrado instalado") \
 X("Partial download lacks remote identity; restart the download", "Parcial sem identidade remota; reinicie o download", "La descarga parcial no tiene identidad remota; reinicia la descarga") \
 X("Insufficient space on microSD", "Espaço insuficiente no microSD", "Espacio insuficiente en microSD") \
 X("Could not seek file", "não foi possível posicionar o arquivo", "no se pudo posicionar el archivo") \
 X("Could not truncate file", "não foi possível truncar o arquivo", "no se pudo truncar el archivo") \
 X("Could not open logical file", "não foi possível abrir o arquivo lógico", "no se pudo abrir el archivo lógico") \
 X("Invalid segment size", "tamanho de segmento inválido", "tamaño de segmento inválido") \
 X("Could not create concatenated file", "não foi possível criar arquivo concatenado", "no se pudo crear archivo concatenado") \
 X("Logical file not found", "arquivo lógico não encontrado", "archivo lógico no encontrado") \
 X("Logical file is closed", "arquivo lógico fechado", "el archivo lógico está cerrado") \
 X("Incomplete logical-file read", "leitura incompleta do arquivo lógico", "lectura incompleta del archivo lógico") \
 X("Logical file is not writable", "arquivo lógico não está aberto para escrita", "el archivo lógico no está abierto para escritura") \
 X("Could not write segment", "não foi possível gravar segmento", "no se pudo escribir el segmento") \
 X("Could not write logical file", "não foi possível gravar arquivo lógico", "no se pudo escribir el archivo lógico") \
 X("Invalid concatenated segment", "segmento concatenado inválido", "segmento concatenado inválido") \
 X("Could not query file size", "não foi possível consultar tamanho", "no se pudo consultar el tamaño") \
 X("Could not flush logical file", "não foi possível confirmar arquivo lógico", "no se pudo confirmar el archivo lógico") \
 X("Could not open temporary state", "não foi possível abrir o estado temporário", "no se pudo abrir el estado temporal") \
 X("Could not write state", "falha ao gravar estado", "no se pudo escribir el estado") \
 X("Invalid installation journal", "diário de instalação inválido", "diario de instalación inválido") \
 X("Incomplete installation journal", "diário de instalação incompleto", "diario de instalación incompleto") \
 X("Could not write installation journal", "não foi possível gravar o diário de instalação", "no se pudo escribir el diario de instalación") \
 X("Could not sync installation journal", "falha ao sincronizar o diário de instalação", "no se pudo sincronizar el diario de instalación") \
 X("Could not commit installation journal to microSD", "não foi possível confirmar o diário no microSD", "no se pudo confirmar el diario en microSD") \
 X("Could not commit journal cleanup", "não foi possível confirmar a limpeza do diário", "no se pudo confirmar la limpieza del diario") \
 X("Invalid NSP: PFS0 header", "NSP inválido: cabeçalho PFS0", "NSP inválido: cabecera PFS0") \
 X("Invalid NSP: PFS0 entries", "NSP inválido: entradas PFS0", "NSP inválido: entradas PFS0") \
 X("Invalid NSP: PFS0 data", "NSP inválido: dados PFS0", "NSP inválido: datos PFS0") \
 X("Invalid NSP: PFS0 name", "NSP inválido: nome", "NSP inválido: nombre") \
 X("NSP read out of bounds", "leitura NSP fora dos limites", "lectura NSP fuera de límites") \
 X("X: restart   B: cancel", "X: reiniciar   B: cancelar", "X: reiniciar   B: cancelar") \
 X("A: resume   X: restart   B: cancel", "A: retomar   X: reiniciar   B: cancelar", "A: reanudar   X: reiniciar   B: cancelar") \
 X("%llu / %llu bytes", "%llu / %llu bytes", "%llu / %llu bytes") \
 X("Graphics unavailable: %s", "Gráficos indisponíveis: %s", "Gráficos no disponibles: %s") \
 X("Application mode (R + game) recommended for NSP.", "Para NSP, use o modo aplicação (R + jogo).", "Para NSP, usa el modo aplicación (R + juego).") \
 X("Operation cancelled", "Operação cancelada", "Operación cancelada") \
 X("A", "A", "A") \
 X("X", "X", "X") \
 X("Y", "Y", "Y") \
 X("Folder", "Pasta", "Carpeta") \
 X("%.1f MiB", "%.1f MiB", "%.1f MiB") \
 X("Your Drive, ready for your Switch.", "Seu Drive, pronto para o seu Switch.", "Tu Drive, listo para tu Switch.") \
 X("Explore your folders and download to microSD.", "Explore suas pastas e baixe para o microSD.", "Explora tus carpetas y descarga a la microSD.") \
 X("Your downloads, all in one place.", "Seus downloads, todos em um só lugar.", "Tus descargas, todas en un solo lugar.") \
 X("Make Switch Drive yours.", "Deixe o Switch Drive do seu jeito.", "Configura Switch Drive a tu gusto.") \
 X("Network initialization failed (%08x). Reopen the app to try again.", "Falha ao iniciar a rede (%08x). Reabra o app para tentar novamente.", "Error al iniciar la red (%08x). Abre de nuevo la app para reintentar.") \
 X("Clean up packages", "Limpar pacotes", "Limpiar paquetes") \
 X("…", "…", "…") \
 X("Controller ready", "Controle conectado", "Control conectado") \
 X("Connect a controller", "Conecte um controle", "Conecta un control") \
 X("Waiting for input focus", "Aguardando foco de controle", "Esperando el foco de control") \
 X("Version 0.2.5", "Versão 0.2.5", "Versión 0.2.5")

#define EN(en, pt, es) en,
constexpr const char* kEnglishKeys[] = { SD_TEXTS(EN) };
constexpr Catalog kEnglish = { SD_TEXTS(EN) };
#define PT(en, pt, es) pt,
constexpr const char* kPortugueseKeys[] = { SD_TEXTS(PT) };
constexpr Catalog kPortuguese = { SD_TEXTS(PT) };
#define ES(en, pt, es) es,
constexpr const char* kSpanishKeys[] = { SD_TEXTS(ES) };
constexpr Catalog kSpanish = { SD_TEXTS(ES) };
#undef EN
#undef PT
#undef ES
static_assert(std::size(kEnglishKeys) == textCount() && std::size(kPortugueseKeys) == textCount() && std::size(kSpanishKeys) == textCount());

Language gLanguage = Language::EnUs;
const Catalog& catalog(Language language) {
    switch (language) { case Language::PtBr: return kPortuguese; case Language::EsEs: return kSpanish; default: return kEnglish; }
}
}

Language parseLanguage(std::string_view code) { if (code == "pt-BR") return Language::PtBr; if (code == "es-ES") return Language::EsEs; return Language::EnUs; }
std::string_view languageCode(Language language) { switch (language) { case Language::PtBr: return "pt-BR"; case Language::EsEs: return "es-ES"; default: return "en-US"; } }
std::string_view languageName(Language language) { switch (language) { case Language::PtBr: return "Português (Brasil)"; case Language::EsEs: return "Español"; default: return "English (US)"; } }
Language nextLanguage(Language language) { return language == Language::EnUs ? Language::PtBr : language == Language::PtBr ? Language::EsEs : Language::EnUs; }
void setLanguage(Language language) { gLanguage = language; }
Language currentLanguage() { return gLanguage; }
const char* tr(TextId id) { const size_t index = static_cast<size_t>(id); return index < textCount() ? catalog(gLanguage)[index] : kEnglish[0]; }

} // namespace switchdrive::i18n
