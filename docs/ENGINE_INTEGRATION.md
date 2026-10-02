# M8: integração do núcleo e entrada nativa de jogo

Estado em 2026-10-02. Projeto separado do Castation. O teste M4 confirmado
no console e o pacote M7 permanecem preservados. M8 não tem confirmação de
execução de jogo no PS5; não é uma promessa de compatibilidade ou desempenho.

## Componentes implementados

- CPU: Processor, frontend PPC/VMX128, passes HIR e backend x64 reais do Xenia,
  com fronteira System V e memória virtual guest existentes desde M5.
- Kernel: módulos xboxkrnl/xam/xbdm, objetos, módulos de usuário, exports,
  gerenciamento de threads e coordenador Emulator originais.
- VFS: host paths, imagens de disco, STFS e dispositivo null upstream.
  Arquivos de jogo são montados somente para leitura; saves ficam separados.
- Threads: implementação POSIX adaptada ao BSD, eventos, semáforos, timers,
  suspensão inicial e fila de APC por thread. APCs são executadas em espera
  alertável fora de locks e handlers de sinal, permitindo enqueue reentrante.
- Controle: driver real InputDriver/XInput, botões, sticks, gatilhos, packets,
  conexão e keystrokes. A entrada nativa adquire scePad e envia amostras ao
  driver. Rumble e repetição de keystrokes ainda não foram implementados.
- GPU: CommandProcessor, tradutor Xenos/SPIR-V, caches de textura/render target,
  VulkanGraphicsSystem, VulkanProvider e Presenter originais; despacho estático
  RADV e superfície KHR_display da mesma instância/dispositivo usados pelo Xenia.
- Áudio: FFmpeg fixado no fork do Xenia, incluindo o decoder XMAFRAMES realmente
  consumido pelo núcleo; XMA1/XMA2 também habilitados. Conversão de 6 canais
  float big-endian para estéreo S16 e AudioOut nativo a 48 kHz, fila limitada,
  consumo sincronizado por semáforo e liberação de clients/ports no shutdown.
- Aplicativo: contexto de UI nativo, window fullscreen, ImGui para diálogos,
  fábricas de GPU/áudio/input, carregamento real via LaunchXexFile, loop de pad,
  pintura, troca de título solicitada pelo guest e encerramento.

## Correções encontradas pela integração

O VFS upstream abria leitura/escrita como O_WRONLY, permitia certos acessos de
escrita em devices readonly e publicava contagem negativa em erros de I/O.
O overlay corrige essas três fronteiras. A inicialização de memória no
Emulator retornava false, equivalente a X_STATUS_SUCCESS: agora retorna erro.

APCs não usam execução de callback em signal handler. A criação de threads
publica o ID e o estado de suspensão antes de acordar quem cria a thread.
A chamada sigaction tinha teste de sucesso invertido na adaptação inicial.

A ligação conjunta encontrou AES, LZX/mspack, bitmaps, bitstreams, ringbuffer,
UI e frontend PPC que não eram exigidos pelos probes separados. Eles agora
participam da integração, sem substituir o kernel ou o GPU por mocks.

Habilitar XMA1/XMA2 sozinho não basta: XmaContext procura AV_CODEC_ID_XMAFRAMES.
O codec foi habilitado, o teste verifica os três decoders e falha de Setup
de contexto é propagada em release. Packets FFmpeg são liberados na destruição.

Thread::current_thread_ é TLS com inicialização constante nullptr. constinit
explicita isso aos demais translation units, evitando importar um thunk fraco
de inicialização que não existe. Os objetos dependem explicitamente desse
overlay, inclusive quando depfiles antigos apontavam ao header upstream.

O aplicativo usa CRT de título nativo e mmap/mprotect/munmap ordinários do
próprio processo. A receita rejeita helpers de alteração de privilégios.
Os stubs AGC são apenas metadados de ligação com os módulos reais do console;
não são implementações de GPU e não entram na pasta distribuída.

## Evidência e limites

`build/kernel-host/integration-tests.xml`: 16 suítes, zero falhas, incluindo
execução PPC/x64, VFS, threads/APCs, input, módulos SPIR-V aceitos pelo Vulkan,
dispositivo Xenia real, codecs/PCM e lifecycle conjunto do Emulator.
`build/integration-vulkan-transfer.txt`: transferência real no Vulkan do PC.
Também passaram os 12 testes Python das ferramentas anteriores.
Os quatro testes novos de ligação fraca elevam a suíte Python a 16 testes:
imports fortes e símbolos externos ao Mesa nunca são convertidos para null.

`build/game-module-load.txt`: o default.xex do Sonic fornecido pelo usuário
foi montado readonly, carregado pelo UserModule real com imports resolvidos,
e entry 82537AE0. Nenhum entry point guest foi chamado nesse diagnóstico.
O dump upstream aponta cinco imports sem implementação completa: XamShowMessageBoxUIEx,
RtlUnwind, RtlCaptureContext, ExThreadObjectType e __C_specific_handler. Portanto,
carregar o módulo não comprova que todos os serviços usados pelo jogo estão prontos.

O RADV usa Mesa 0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8, referência PS5_Vulkan
3f3ee69607013b345d2baa6d6a37c86745649a08 e SDK fork
95c08f27386fc698f6bbe21dde3030140a41d10b. Xenia permanece em
95a5c3ee250f80c3b9d139658649d9ffb6db3eec, FFmpeg em
15ece0882e8d5875051ff5b73c5a8326f7cee9f5. Receitas não modificam o checkout do Xenia.

Ainda precisam de teste no PS5: startup e permissões do título, layout de
signal context/MMIO, memória JIT, apresentação de frames do guest, acesso aos
arquivos em app0, AudioOut, controle no jogo e teardown com jogo aberto.
O backend Vulkan deste Xenia é descrito upstream como incompleto; o port não
torna automaticamente todos os seus recursos/exports compatíveis. A entrada
nativa mapeia direcional/X/O/quadrado para navegação, confirmação, cancelamento
e Tab nos diálogos ImGui; esse caminho ainda precisa de confirmação no hardware.

FTP 192.168.0.19:2121 e API 10101 recusaram conexão neste turno. Nenhuma nova
instalação ou execução no console foi feita. Resultado de hardware não deve
ser inferido das compilações, da aprovação no Lavapipe ou do carregamento do XEX.

## Reprodução

`./build.ps1 IntegrationHost`: compila e testa a integração no PC.
`./build.ps1 IntegrationPS5`: compila archives e probes PS5.
`./build.ps1 RADV`: driver real em imagem Docker própria e cache Linux.
`./build.ps1 NativeGame`: compila os objetos da entrada nativa e gera
`dist/PPSA50011` e `dist/Xbox360PS5-M8-experimental-game-test.zip` se todas as
verificações estruturais passarem. A receita depende do SDK e das referências
fixados e do runtime libc reproduzível já validado em M7. Não inclui jogos.

O jogo extraído deve ficar em `assets/roms`, preservando default.xex e dados.
O caminho padrão é `/app0/assets/roms/default.xex`. Para uma subpasta, escrever
o caminho completo em `assets/game.txt`. Um override persistente pode ficar
em `/download0/xbox360ps5/game.txt`, e argumentos também são aceitos.
Logs ficam em `/download0/xbox360ps5/engine.log`.
Para publicar a nova pasta depois das verificações: `python tools/deploy-native-probe.py
--game --host IP_DO_PS5`. O script confere bytes locais/remotos antes de registrar
o novo título e recusa sobrescrever uma pasta existente. Não envia dados do jogo.

## Fontes e licenças

Xenia é BSD; as ferramentas/startup de título nativo e PS5_Vulkan são GPL-3.0
ou posterior. FFmpeg e mspack mantêm LGPL e avisos originais. O pacote combinado
inclui os avisos em licenses. O código do port, overlays e receitas fornecem
as modificações; os commits exatos dos componentes upstream acima identificam
suas fontes. As bibliotecas não foram editadas dentro dos checkouts, exceto
pelos overlays de build explicitamente gerados pelas ferramentas do port.
Não são distribuídos jogos, BIOS, chaves ou implementações proprietárias.
