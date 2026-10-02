# Primeiro teste Xbox360PS5 (M1)

Este aplicativo testa no PS5 o decoder de instruções do Xenia, a apresentação
VideoOut e o controle DualSense. Ainda não carrega jogos e não inicializa JIT,
Vulkan ou o kernel guest do Xbox 360.

Extrair `PPSA50008` para `/data/homebrew/PPSA50008` e registrar a pasta com o
ShadowMountPlus. Abrir **Xbox360PS5 Platform Test** pelo menu do PS5.

Conferir:

1. Aparece a tela `XBOX360PS5 M1`.
2. Os nove casos mostram `PASS`; `FAILURES` é zero.
3. O status mostra `DUALSENSE READY`.
4. Pressionar X aumenta `CROSS PRESSES` e `RUNS`.
5. Mover o analógico esquerdo altera os dois valores em `LEFT STICK`.
6. Fechar o aplicativo pelo menu do PS5.

O diagnóstico fica em `/download0/xbox360ps5-m1.log`, dentro do sandbox do
título. Isso permite recuperar os resultados mesmo se a apresentação falhar.
O teste usa um title ID próprio e não substitui o Castation.

Passar neste teste comprova apenas os componentes acima. A emulação do
processador, a GPU Xenos, o carregamento XEX e a execução de jogos continuam
pendentes.

## Implantação inicial

Em 2026-10-01, o teste foi enviado para `192.168.0.19` e registrado como
`source_type=folder`, `installed=true`, `source_available=true`, sem erro de
registro. O usuário confirmou abertura, imagem e resposta do controle. O título usa
`/data/homebrew/PPSA50008`.

O FTP do console expõe SELF como ELF e zera alguns metadados não mapeados.
A verificação compara o cabeçalho, a tabela de segmentos e todos os segmentos
mapeados/dinâmicos; tolera somente notas de versão fora dos segmentos de
memória que o FTP tenha zerado integralmente. Código modificado ou cabeçalho
divergente são rejeitados. Os testes dessa verificação estão em
`tools/test_deploy_verify.py`. Ícone e param.json são conferidos byte a byte.

O log foi lido pelo FTP com o aplicativo aberto: 53 execuções dos nove casos,
sempre com zero falhas. O handle do pad foi aberto com sucesso. O usuário
confirmou que a tela, o contador de X e os valores do analógico respondem.
Esse resultado valida o marco M1, sem atestar execução de jogos, JIT ou Vulkan.
O registro vinculado ao hash do executável está em `docs/evidence/M1_HARDWARE.json`.
