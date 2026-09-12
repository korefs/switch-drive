# Aceitação em hardware

Registre neste arquivo, por build testado, o modelo do Switch, a versão de
firmware, Atmosphère, hbmenu, tipo do cartão (FAT32 ou exFAT), data e resultado.

| Cenário | Resultado esperado | Resultado |
|---|---|---|
| Inicialização gráfica | Abre em 1280×720, sem tela de console, `%s` visível ou caracteres corrompidos | Pendente |
| Controle e toque | D-pad/analógico, A/B/X/Y, L/R, + e toque navegam os controles gráficos | Pendente |
| Idiomas | Inglês, português e espanhol exibem acentos, textos longos e placeholders corretos | Pendente |
| Modo applet | Exibe aviso persistente de title override; erros NCM permanecem na interface | Pendente |
| Sphaira direto (Álbum/applet) | Abre a UI gráfica e permite navegação; registrar boot.log e memória informada | Pendente |
| Sphaira em title override | Abre pelo override e permite download/instalação sem encerrar a UI | Pendente |
| Pareamento por celular | Conta aparece no Switch após confirmação | Pendente |
| Segunda conta | Contas e arquivos permanecem isolados | Pendente |
| Compartilhados comigo | Pasta e filhos podem ser abertos | Pendente |
| Download interrompido | Retoma só após resposta HTTP 206 válida | Pendente |
| Paginação do Drive | Mais de 100 itens carregam a próxima página ao alcançar o fim da lista | Pendente |
| Progresso e pausa | Download, MD5 e instalação atualizam a barra; B pausa em checkpoint seguro | Pendente |
| NRO | Copiado atomicamente para `sd:/switch/<id>/` | Pendente |
| NSP jogo/atualização/DLC | Instalação NCM em microSD e memória interna, tipo/versão corretos | Pendente |
| NSP downgrade/igual | Downgrade bloqueado; versão igual não altera conteúdo | Pendente |
| NSP interrupção | Antes do commit restaura a versão anterior; depois conclui a recuperação | Pendente |
| Remoção NSP | Remove somente o componente gerenciado; saves e tickets permanecem | Pendente |
| Limpeza | Pacote é removido somente depois de instalação confirmada | Pendente |
| Biblioteca | Atalho externo ausente é detectado somente ao acioná-lo | Pendente |

## Verificação local da versão 0.2.1

- Testes CMake/CTest: aprovados, incluindo catálogos e seleção por toque nas
  coordenadas do renderizador em modo applet e aplicação.
- Build devkitPro: NRO gerado com ícone, NACP e RomFS na seção ASET.
- Revisão visual: prévias de Início, Arquivos e Configurações nos três idiomas
  e nos dois layouts, usando o renderizador real com uma fonte do host.
- O diagnóstico confirmado é o empacotamento sem recursos e o fallback de
  terminal. A causa específica da falha Sphaira → Switch Drive permanece
  dependente do log e do teste no console; não foi reproduzida no host.

Para repetir a revisão visual (SDL2/SDL2_ttf e pkg-config no host):

```sh
cmake -S tests -B build/preview -DSWITCHDRIVE_UI_PREVIEW=ON
cmake --build build/preview
SWITCHDRIVE_PREVIEW_FONT=/caminho/para/fonte.ttf build/preview/switch_drive_ui_preview
```

As imagens são gravadas em `build/ui-preview/`. O teste no host não emula
serviços, fontes compartilhadas, entrada, memória nem permissões do Switch.

Se o devkitPro fornecer o primeiro `pkg-config` do PATH, selecione o do host
com `-DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config` (Homebrew/macOS).
Após `make`, execute `python3 tests/check_nro.py` para verificar os recursos
embutidos do artefato que será copiado para o cartão.

## Controles da versão 0.2.2

- Testes portáteis: seleção dos três cartões, bordas da grade, entrada/saída
  do menu lateral, A no cartão selecionado e repetição por tempo aprovados.
- Prévias do renderizador: foco de Arquivos, Biblioteca e menu lateral nos
  três idiomas e nos dois layouts.
- Pendente no console: Joy-Cons encaixados, par desacoplado, controles nos
  slots 2–8, reconexão e Pro Controller. Validar direcional, ambos os sticks,
  A/B/X/Y e L/R, incluindo retorno de um diálogo à tela principal.
