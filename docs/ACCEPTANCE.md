# Aceitação em hardware

Registre neste arquivo, por build testado, o modelo do Switch, a versão de
firmware, Atmosphère, hbmenu, tipo do cartão (FAT32 ou exFAT), data e resultado.

| Cenário | Resultado esperado | Resultado |
|---|---|---|
| Pareamento por celular | Conta aparece no Switch após confirmação | Pendente |
| Segunda conta | Contas e arquivos permanecem isolados | Pendente |
| Compartilhados comigo | Pasta e filhos podem ser abertos | Pendente |
| Download interrompido | Retoma só após resposta HTTP 206 válida | Pendente |
| NRO | Copiado atomicamente para `sd:/switch/<id>/` | Pendente |
| NSP jogo/atualização/DLC | Instalação NCM em microSD e memória interna, tipo/versão corretos | Pendente |
| NSP downgrade/igual | Downgrade bloqueado; versão igual não altera conteúdo | Pendente |
| NSP interrupção | Antes do commit restaura a versão anterior; depois conclui a recuperação | Pendente |
| Remoção NSP | Remove somente o componente gerenciado; saves e tickets permanecem | Pendente |
| Limpeza | Pacote é removido somente depois de instalação confirmada | Pendente |
| Biblioteca | Atalho externo ausente é detectado somente ao acioná-lo | Pendente |
