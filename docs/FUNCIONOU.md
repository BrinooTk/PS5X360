# Funcionou: primeiro jogo rodando no PS5 (2026-10-02)

O Xbox360PS5 (Xenia portado para PS5, título nativo PPSA50011 em
`/data/homebrew/PPSA50011`) executou **Sonic the Hedgehog (2006)** num PS5
com firmware 13.60 (etaHEN + ShadowMountPlus + kstuff).

Resultado relatado pelo usuário: **"funcionou por hora, mas em partes"**. A
imagem do jogo apareceu na TV. Quais partes ainda falham não foi detalhado;
este arquivo registra só o que o log comprova.

## O que o log desta execução comprova

Log completo: `logs/ultimo-log-ps5.log` (4,9 MB, transmitido pelo próprio
título pela porta TCP 9100).

- Sessão de cerca de 2 min 37 s (16:35:27 a 16:38:04 no log do ShadowMount),
  encerrada pelo usuário ao reabrir o título, sem registro de crash.
- 9265 quadros entregues pelo jogo (`VdSwap`), em torno de 59 por segundo
  nesse intervalo. É a contagem de trocas do jogo, não uma medição de
  desempenho na tela.
- JIT PowerPC para x64 executando o código do jogo; 26 threads do jogo criadas.
- Vulkan pelo RADV (`PlayStation 5 GPU (RADV NAVI21)`), swapchain 3840x2160,
  10 pipelines gráficos, texturas DXT1/DXT2_3/DXT4_5/8_8_8_8 e render targets
  com MSAA 4x.
- Porta de áudio do PS5 aberta (`AudioOut port ... open`). Se o som saiu
  corretamente não foi confirmado.
- Arquivos do jogo lidos de `/app0/assets/roms` (`win32\archives`,
  `xenon\archives`).

## Problemas vistos no mesmo log

- `CreateThread failed` / `Thread creation failed: C0000017` quatro vezes: o
  jogo pediu threads que o emulador não conseguiu criar (falta de memória para
  a pilha, pelo código de erro). Pode explicar parte do "em partes".
- Dois formatos de textura sem suporte no dispositivo (`k_2_10_10_10` com
  sinal e `k_2_10_10_10_AS_16_16_16_16`).
- Escritas em registradores de GPU desconhecidos (0081, 0082, 1E4E).
- Controle, som e estabilidade em sessões longas não foram verificados.

## O que foi preciso para chegar aqui

Cada item foi encontrado por um crash no console e corrigido em seguida.

1. **Exceção C++ na inicialização do ImGui e na configuração do jogo**:
   `std::filesystem` lança erro para caminhos relativos, porque o diretório de
   trabalho do título não é pesquisável. `stat`/`lstat` passaram a responder
   "arquivo não existe" nesses casos (`platform/ps5/libc_bits.c`).
2. **Memória** (`platform/ps5/memory_ps5.cpp`): `mmap` anônimo é limitado a
   448 MiB, então tudo usa memória direta em faixas reservadas. A memória
   guest de 4,5 GiB fica em `0x1000000000` com seus espelhos. Páginas do PS5
   têm 16 KiB e as do Xbox 4 KiB; cada página do host recebe a união das
   proteções das quatro subpáginas.
3. **JIT**: o `libc.prx` do título é carregado em `0x80000000`, onde o Xenia
   queria a tabela de indireção. O código gerado foi para `0x40000000` e a
   tabela para `0x50000000` (overlays em `tools/prepare.py`).
4. **Sinais**: o contexto de sinal do PS5 tem 48 bytes a mais antes dos
   registradores e tamanho 0x480 (`platform/ps5/exception_handler.cpp`).
5. **Saída de thread**: destrutores de `thread_local` rodavam depois de o
   armazenamento TLS emulado ser liberado; a chave dos destrutores agora é
   criada primeiro (`platform/ps5/crash_report.cpp`).
6. **Áudio**: a porta principal precisa do usuário de sistema (255). Com o
   registro de áudio falhando, o Sonic desmontava o próprio motor de som e
   depois usava um ponteiro nulo. Há também um driver silencioso de reserva
   (`platform/ps5/native_audio.cpp`).

## Ferramentas de depuração criadas

- `tools/console.py update | logs | watch | netlog`: troca o `eboot.bin` no
  console, lê o log de kernel e grava o log transmitido pelo título.
- Relator de crash no título: sinal, registradores, pilha simbolizável
  (`eboot+0x...` é o endereço em `build/native-game/llvm-pie.elf`) e, para
  crash do jogo, registradores PowerPC e o código em volta.
- `xenia-engine-integration --run-xex <default.xex> <segundos>`: roda o jogo
  no PC sem janela, para comparar o rastro com o do console.
- Rastro de chamadas de kernel no PS5: criar `assets/debug.txt` na pasta do
  título.

## Limites

Uma execução de um jogo não é compatibilidade geral. O backend Vulkan deste
Xenia é descrito pelo próprio projeto como incompleto. Os testes automáticos
de host não foram refeitos depois destas mudanças.

## 3 de outubro de 2026

- **Tela preta nas lutas (Burst Limit, provavelmente Ultimate Tenkaichi e outros):**
  corrigida. Era um defeito do Xenia no tradutor de shaders para Vulkan: o ajuste
  de expoente das texturas era lido da palavra errada da constante de textura, e
  qualquer textura com bias de LOD saía multiplicada por uma potência de dois
  errada (a profundidade do Burst Limit vinha dividida por 65536 e o pós-processamento
  apagava a cena). A correção vale para a biblioteca toda. Verificado no PC: a luta
  Goku x Raditz aparece completa.
- **Budokai HD Collection:** escolher o Budokai 1 ou 3 reinicia o emulador direto no
  jogo escolhido (antes travava). Verificado no PC: o Budokai 3 chega ao menu com música.
- **Capas em 3D:** cada jogo aparece como uma caixa com a capa e a lombada originais.
  As capas são baixadas sozinhas do XboxUnity quando a prateleira abre (também em
  Quadrado → Baixar capas).
- O ID do jogo é lido do próprio executável/ISO: capas e patches aparecem antes do
  primeiro boot.
