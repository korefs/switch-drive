// SPDX-License-Identifier: GPL-3.0-or-later
#include "switchdrive/i18n.hpp"

#include <array>

namespace switchdrive::i18n {
namespace {
using Catalog = std::array<const char*, textCount()>;

#define SD_TEXTS(X) \
 X("Switch Drive", "Switch Drive", "Switch Drive") \
 X("Press A to continue.", "Pressione A para continuar.", "Pulsa A para continuar.") \
 X("Install NSP / NSZ", "Instalar NSP / NSZ", "Instalar NSP / NSZ") \
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
 X("Check connection", "Checar conexão", "Comprobar conexión") \
 X("Scan with your phone to continue.", "Escaneie com o celular para continuar.", "Escanea con tu teléfono para continuar.") \
 X("Connected: %s", "Conectado: %s", "Conectado: %s") \
 X("Connection confirmed", "Conexão confirmada", "Conexión confirmada") \
 X("Go to files", "Ir para arquivos", "Ir a archivos") \
 X("Disconnect Google Drive", "Desconectar Google Drive", "Desconectar Google Drive") \
 X("Disconnect this account and delete its stored authorization?", "Desconectar esta conta e apagar a autorização armazenada?", "¿Desconectar esta cuenta y borrar la autorización almacenada?") \
 X("Disconnecting account...", "Desconectando conta...", "Desconectando cuenta...") \
 X("Account disconnected.", "Conta desconectada.", "Cuenta desconectada.") \
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
 X("Checksum does not match", "O checksum não confere", "La suma de comprobación no coincide") \
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
 X("No active transfers.", "Nenhuma transferência ativa.", "No hay transferencias activas.") \
 X("Downloading %s", "Baixando %s", "Descargando %s") \
 X("Installation will start automatically after verification.", "A instalação iniciará automaticamente após a verificação.", "La instalación comenzará automáticamente después de la verificación.") \
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
 X("My Drive", "Meu Drive", "Mi Drive") \
 X("Shared with me", "Compartilhados comigo", "Compartidos conmigo") \
 X("Folder is empty.", "Pasta vazia.", "La carpeta está vacía.") \
 X("A: open  X: download  Y: download and install  B: back", "A: abrir  X: baixar  Y: baixar e instalar  B: voltar", "A: abrir  X: descargar  Y: descargar e instalar  B: volver") \
 X("Library", "Biblioteca", "Biblioteca") \
 X("No indexed downloads.", "Nenhum download indexado.", "No hay descargas indexadas.") \
 X("File missing", "Arquivo ausente", "Archivo ausente") \
 X("Home", "Início", "Inicio") \
 X("Settings", "Configurações", "Configuración") \
 X("No account connected.", "Nenhuma conta conectada.", "No hay ninguna cuenta conectada.") \
 X("Active account: %s", "Conta ativa: %s", "Cuenta activa: %s") \
 X("A: connect account     X: open files", "A: conectar conta     X: abrir arquivos", "A: conectar cuenta     X: abrir archivos") \
 X("Stick / D-pad: move   A: select   B: menu   L/R: section   +: exit", "Analógico / direcional: mover   A: selecionar   B: menu   L/R: seção   +: sair", "Stick / cruceta: mover   A: elegir   B: menú   L/R: sección   +: salir") \
 X("Exit", "Sair", "Salir") \
 X("Language", "Idioma", "Idioma") \
 X("Google OAuth Pairing API", "API de pareamento OAuth do Google", "API de vinculación OAuth de Google") \
 X("Enter an HTTPS origin without a path.", "Informe uma origem HTTPS sem caminho.", "Introduce un origen HTTPS sin ruta.") \
 X("Invalid NRO: NRO0 header missing", "NRO inválido: cabeçalho NRO0 ausente", "NRO inválido: falta la cabecera NRO0") \
 X("Invalid NRO: size outside limit", "NRO inválido: tamanho fora do limite", "NRO inválido: tamaño fuera del límite") \
 X("A homebrew with this name already exists", "Já existe uma homebrew com esse nome", "Ya existe un homebrew con este nombre") \
 X("Ambiguous package: more than one CNMT", "Pacote ambíguo: mais de um CNMT", "Paquete ambiguo: más de un CNMT") \
 X("Invalid NSP/NSZ: NCA, NCZ, or CNMT missing", "NSP/NSZ inválido: faltam NCA, NCZ ou CNMT", "NSP/NSZ inválido: faltan NCA, NCZ o CNMT") \
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
 X("Could not prepare the temporary CNMT directory on microSD (Result 0x%08X)", "não foi possível preparar a pasta temporária do CNMT no microSD (Result 0x%08X)", "no se pudo preparar la carpeta temporal del CNMT en microSD (Result 0x%08X)") \
 X("Could not prepare CNMT (Result 0x%08X)", "não foi possível preparar o CNMT (Result 0x%08X)", "no se pudo preparar el CNMT (Result 0x%08X)") \
 X("Could not mount the CNMT NCA (Result 0x%08X)", "não foi possível montar o NCA do CNMT (Result 0x%08X)", "no se pudo montar el NCA del CNMT (Result 0x%08X)") \
 X("CNMT file missing", "arquivo CNMT ausente", "falta el archivo CNMT") \
 X("Invalid NSP/NSZ: content missing or wrong size", "NSP/NSZ inválido: conteúdo ausente ou com tamanho incorreto", "NSP/NSZ inválido: falta contenido o tiene tamaño incorrecto") \
 X("Could not list CNMT files (Result 0x%08X)", "não foi possível listar os arquivos CNMT (Result 0x%08X)", "no se pudieron listar los archivos CNMT (Result 0x%08X)") \
 X("Could not open the CNMT file (Result 0x%08X)", "não foi possível abrir o arquivo CNMT (Result 0x%08X)", "no se pudo abrir el archivo CNMT (Result 0x%08X)") \
 X("Could not query CNMT size (Result 0x%08X)", "não foi possível consultar o tamanho do CNMT (Result 0x%08X)", "no se pudo consultar el tamaño del CNMT (Result 0x%08X)") \
 X("Could not read the CNMT file (Result 0x%08X)", "não foi possível ler o arquivo CNMT (Result 0x%08X)", "no se pudo leer el archivo CNMT (Result 0x%08X)") \
 X("Invalid CNMT size (expected 32 bytes to 16 MiB)", "tamanho do CNMT inválido (esperado: 32 bytes a 16 MiB)", "tamaño del CNMT inválido (esperado: 32 bytes a 16 MiB)") \
 X("The installer returned a failure without details", "o instalador retornou uma falha sem detalhes", "el instalador devolvió un fallo sin detalles") \
 X("NSP query requires a Nintendo Switch with Atmosphère", "Consulta NSP exige um Nintendo Switch com Atmosphère", "La consulta NSP requiere una Nintendo Switch con Atmosphère") \
 X("NCM unavailable; run under Atmosphère in application mode", "NCM indisponível; execute pelo Atmosphère em modo aplicação", "NCM no disponible; ejecuta Atmosphère en modo aplicación") \
 X("NS unavailable; run under Atmosphère in application mode", "NS indisponível; execute pelo Atmosphère em modo aplicação", "NS no disponible; ejecuta Atmosphère en modo aplicación") \
 X("Could not query installed content", "não foi possível consultar conteúdo instalado", "no se pudo consultar el contenido instalado") \
 X("NSP/NSZ installation requires a Nintendo Switch with Atmosphère", "Instalação NSP/NSZ exige um Nintendo Switch com Atmosphère", "La instalación NSP/NSZ requiere una Nintendo Switch con Atmosphère") \
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
 X("Content installed, but the Home Menu record could not be updated", "Conteúdo instalado, mas não foi possível atualizar o registro do menu HOME", "Contenido instalado, pero no se pudo actualizar el registro del menú HOME") \
 X("Installation complete.", "Instalação concluída.", "Instalación completada.") \
 X("Updates and DLC do not create a new HOME icon.", "Atualizações e DLC não criam um novo ícone no menu HOME.", "Las actualizaciones y los DLC no crean un icono nuevo en el menú HOME.") \
 X("Downloaded item is missing from the library", "Item baixado não foi encontrado na biblioteca", "El elemento descargado no está en la biblioteca") \
 X("This file type cannot be installed", "Este tipo de arquivo não pode ser instalado", "Este tipo de archivo no se puede instalar") \
 X("Content installed; journal could not be updated", "conteúdo instalado; diário não pôde ser atualizado", "contenido instalado; no se pudo actualizar el diario") \
 X("NSP recovery requires the console", "Recuperação NSP exige o console", "La recuperación NSP requiere la consola") \
 X("NCM unavailable during recovery", "NCM indisponível durante recuperação", "NCM no disponible durante la recuperación") \
 X("Could not verify NSP commit", "não foi possível verificar o commit NSP", "no se pudo verificar la confirmación NSP") \
 X("HTTP response did not authorize writing", "resposta HTTP não autorizou escrita", "la respuesta HTTP no autorizó la escritura") \
 X("Server did not confirm download range", "servidor não confirmou a faixa do download", "el servidor no confirmó el rango de descarga") \
 X("Could not record response", "não foi possível registrar a resposta do download", "no se pudo registrar la respuesta de descarga") \
 X("Invalid service JSON", "JSON inválido do serviço", "JSON de servicio inválido") \
 X("HTTP request failed (status %ld)", "Falha na requisição HTTP (status %ld)", "La solicitud HTTP falló (estado %ld)") \
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
 X("Invalid NSP/NSZ: PFS0 header", "NSP/NSZ inválido: cabeçalho PFS0", "NSP/NSZ inválido: cabecera PFS0") \
 X("Invalid NSP/NSZ: PFS0 entries", "NSP/NSZ inválido: entradas PFS0", "NSP/NSZ inválido: entradas PFS0") \
 X("Invalid NSP/NSZ: PFS0 data", "NSP/NSZ inválido: dados PFS0", "NSP/NSZ inválido: datos PFS0") \
 X("Invalid NSP/NSZ: PFS0 name", "NSP/NSZ inválido: nome", "NSP/NSZ inválido: nombre") \
 X("NSP/NSZ read out of bounds", "leitura NSP/NSZ fora dos limites", "lectura NSP/NSZ fuera de límites") \
 X("Invalid NCZ header", "cabeçalho NCZ inválido", "cabecera NCZ inválida") \
 X("Invalid NCZ section table", "tabela de seções NCZ inválida", "tabla de secciones NCZ inválida") \
 X("Invalid NCZ block table", "tabela de blocos NCZ inválida", "tabla de bloques NCZ inválida") \
 X("Could not decompress NCZ", "não foi possível descompactar o NCZ", "no se pudo descomprimir el NCZ") \
 X("X: restart   B: cancel", "X: reiniciar   B: cancelar", "X: reiniciar   B: cancelar") \
 X("A: resume   X: restart   B: cancel", "A: retomar   X: reiniciar   B: cancelar", "A: reanudar   X: reiniciar   B: cancelar") \
 X("%s / %s · %.1f%% · Calculating speed...", "%s / %s · %.1f%% · Calculando velocidade...", "%s / %s · %.1f%% · Calculando velocidad...") \
 X("%s / %s · %.1f%% · %s/s · ETA %s", "%s / %s · %.1f%% · %s/s · ETA %s", "%s / %s · %.1f%% · %s/s · ETA %s") \
 X("%llu B", "%llu B", "%llu B") \
 X("%.1f KiB", "%.1f KiB", "%.1f KiB") \
 X("%.1f MiB", "%.1f MiB", "%.1f MiB") \
 X("%.1f GiB", "%.1f GiB", "%.1f GiB") \
 X("%llus", "%llus", "%llus") \
 X("%llum %02llus", "%llum %02llus", "%llum %02llus") \
 X("%lluh %02llum", "%lluh %02llum", "%lluh %02llum") \
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
 X("…", "…", "…") \
 X("Controller ready", "Controle conectado", "Control conectado") \
 X("Connect a controller", "Conecte um controle", "Conecta un control") \
 X("Waiting for input focus", "Aguardando foco de controle", "Esperando el foco de control") \
 X("Version 0.2.8", "Versão 0.2.8", "Versión 0.2.8") \
 X("Preparing download…", "Preparando download…", "Preparando descarga…") \
 X("Loading files…", "Carregando arquivos…", "Cargando archivos…") \
 X("Verifying downloaded file…", "Verificando arquivo baixado…", "Verificando archivo descargado…") \
 X("B: pause download", "B: pausar download", "B: pausar descarga") \
 X("Delete download", "Excluir download", "Eliminar descarga") \
 X("Delete the downloaded file from microSD? Installed games and saves will not be removed.", "Excluir o arquivo baixado do microSD? Jogos instalados e saves não serão removidos.", "¿Eliminar el archivo descargado de la microSD? No se eliminarán juegos instalados ni partidas.") \
 X("Installation recovery is pending. Reopen the app before deleting this download.", "Há uma recuperação de instalação pendente. Reabra o app antes de excluir este download.", "Hay una recuperación de instalación pendiente. Abre de nuevo la app antes de eliminar esta descarga.") \
 X("The file is outside this download's folder; deletion was blocked.", "O arquivo está fora da pasta deste download; a exclusão foi bloqueada.", "El archivo está fuera de la carpeta de esta descarga; se bloqueó su eliminación.")

#define SD_HOME_TEXTS(X) \
 X("Storage providers", "Provedores de armazenamento", "Proveedores de almacenamiento") \
 X("Home Storage", "Home Storage", "Home Storage") \
 X("Detect on network", "Detectar na rede", "Detectar en la red") \
 X("Manual setup", "Configuração manual", "Configuración manual") \
 X("Server address", "Endereço do servidor", "Dirección del servidor") \
 X("Username", "Usuário", "Usuario") \
 X("Password", "Senha", "Contraseña") \
 X("Searching for Home Storage...", "Procurando Home Storage...", "Buscando Home Storage...") \
 X("No Home Storage service was found.", "Nenhum serviço Home Storage foi encontrado.", "No se encontró ningún servicio Home Storage.") \
 X("Invalid server address.", "Endereço de servidor inválido.", "Dirección de servidor inválida.") \
 X("A: open  X: download  Y: download and install  ZL: hide  B: back", "A: abrir  X: baixar  Y: baixar e instalar  ZL: ocultar  B: voltar", "A: abrir  X: descargar  Y: descargar e instalar  ZL: ocultar  B: volver") \
 X("Hide catalog entry", "Ocultar item do catálogo", "Ocultar elemento del catálogo") \
 X("Hide this item from Home Storage? The PC file will not be deleted.", "Ocultar este item do Home Storage? O arquivo do PC não será excluído.", "¿Ocultar este elemento de Home Storage? El archivo del PC no se eliminará.") \
 X("Home Storage connected: %s", "Home Storage conectado: %s", "Home Storage conectado: %s") \
 X("ZL", "ZL", "ZL") \
 X("L", "L", "L") \
 X("R", "R", "R") \
 X("Download", "Baixar", "Descargar") \
 X("Download and install", "Baixar e instalar", "Descargar e instalar") \
 X("Back", "Voltar", "Volver") \
 X("Language changes will be applied on the next launch.", "A mudança de idioma será aplicada na próxima inicialização.", "El cambio de idioma se aplicará en el próximo inicio.") \
 X("Close Switch Drive?", "Fechar o Switch Drive?", "¿Cerrar Switch Drive?") \
 X("A transfer or installation is active. Cancel it and close Switch Drive?", "Há uma transferência ou instalação ativa. Cancelar e fechar o Switch Drive?", "Hay una transferencia o instalación activa. ¿Cancelarla y cerrar Switch Drive?") \
 X("network: applet=%d rx=%u/%u socket=%08x fallback=%d", "network: applet=%d rx=%u/%u socket=%08x fallback=%d", "network: applet=%d rx=%u/%u socket=%08x fallback=%d") \
 X("download: applet=%d rx=%u/%u fallback=%d received=%llu written=%llu durable=%llu total_us=%llu wait_us=%llu write_us=%llu queue_peak=%llu async=%d", "download: applet=%d rx=%u/%u fallback=%d received=%llu written=%llu durable=%llu total_us=%llu wait_us=%llu write_us=%llu queue_peak=%llu async=%d", "download: applet=%d rx=%u/%u fallback=%d received=%llu written=%llu durable=%llu total_us=%llu wait_us=%llu write_us=%llu queue_peak=%llu async=%d")

#define EN(en, pt, es) en,
constexpr const char* kEnglishKeys[] = { SD_TEXTS(EN) SD_HOME_TEXTS(EN) };
constexpr Catalog kEnglish = { SD_TEXTS(EN) SD_HOME_TEXTS(EN) };
#define PT(en, pt, es) pt,
constexpr const char* kPortugueseKeys[] = { SD_TEXTS(PT) SD_HOME_TEXTS(PT) };
constexpr Catalog kPortuguese = { SD_TEXTS(PT) SD_HOME_TEXTS(PT) };
#define ES(en, pt, es) es,
constexpr const char* kSpanishKeys[] = { SD_TEXTS(ES) SD_HOME_TEXTS(ES) };
constexpr Catalog kSpanish = { SD_TEXTS(ES) SD_HOME_TEXTS(ES) };
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
