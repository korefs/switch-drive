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

## Correção de renderização da versão 0.2.5

O renderizador usava um único buffer. Se o compositor retém o quadro exibido,
a segunda chamada de `framebufferBegin` espera um buffer livre e impede novas
leituras dos botões. A primeira tela das versões anteriores mostrava
`Connect a controller` antes de consultar o HID; portanto, essa mensagem numa
tela parada não comprovava falha de detecção. Agora são usados dois buffers,
e o menu principal consulta os botões e processa + antes de apresentar o quadro.

Foi retirada a leitura duplicada via SDL e a configuração especulativa de
slots genéricos/foco. O backend SDL do Switch também chama o pad do libnx.
A entrada usa a configuração padrão do libnx para os oito jogadores e portátil.

O teste `switch_drive_ui_runtime_tests` compila o caminho `__SWITCH__` real de
`ui.cpp` com SDL/TTF do host e serviços de vídeo/entrada simulados. O consumidor
simulado retém o buffer exibido até receber outro. Com um buffer, o teste falhou
na segunda apresentação; com dois, passou mais de 300 quadros em cada um de
quatro cenários (aplicação/applet, com/sem controle inicial). Verifica também
conexão tardia, reconexão, direcional, stick direito, A, L, estado de foco e +.
Isso não emula o driver HID, o compositor ou o launcher do console.

```sh
cmake -S tests -B build/preview -DSWITCHDRIVE_UI_RUNTIME_TESTS=ON
cmake --build build/preview
SWITCHDRIVE_PREVIEW_FONT=/caminho/para/fonte.ttf ctest --test-dir build/preview --output-on-failure
```

Pendente em hardware: substituir o NRO, confirmar `Versão 0.2.5`, abrir por
Sphaira → Switch Drive, navegar com Joy-Cons já conectados e sair com +.
Repetir em applet e title override, incluindo desconexão/reconexão.
Se ainda falhar, preservar `sd:/switch-drive/boot.log`: `video: frame=2 before
dequeue` sem `buffer acquired` indica espera pelo vídeo; contadores avançando
com `connected=0` apontam para investigação de entrada. `focus=0` registra
que o processo estava sem foco. A versão e os três primeiros quadros ficam
no log; ele é substituído a cada abertura.
