# M5: execução real do núcleo CPU

Esta etapa acrescenta execução de programas PowerPC sintéticos pelo recompilador
x64 original do Xenia. O teste anterior M4 avaliava HIR; não executava o código
x64 produzido. **M5 ainda não inicia jogos e não foi executado no PS5.**

## Resultado verificado

- Oito suites no computador, sem falhas: as seis anteriores (98 verificações),
  contrato de memória e arquivos (39) e execução real PPC/x64 (50 verificações, incluindo
  chamadas repetidas para testar estabilidade).
- A execução compara registradores e memória big endian, realiza 32 chamadas
  a um handler nativo por um import thunk sintético, resolve uma função guest
  ainda não compilada, exercita oito shifts vetoriais e mantém quinze vetores
  calculados vivos através de chamadas nativas.
- O backend vetorial é limitado às extensões até MOVBE, incluindo AVX2/FMA,
  sem atalhos GFNI/AVX512 que não correspondem ao Zen 2 do PS5.
- O mesmo runtime e seu teste de memória foram compilados e ligados com o SDK
  PS5. A validação confirma ELF64 x86-64, limites dos segmentos, alinhamento,
  entrada executável e formato de cada objeto das dez bibliotecas.
- Os testes Python validam metadados XEX e a verificação de arquivos recebidos
  por FTP. Os relatórios de host e os hashes entram no pacote de desenvolvimento.

Não confundir o ELF de desenvolvimento com um aplicativo nativo registrado.
O aplicativo M4 PPSA50009 e seus pacotes funcionais permanecem preservados.

## Correção da fronteira de chamadas

Nesta revisão do Xenia, os thunks gerados recebiam target/context/return address
em RCX/RDX/R8 e escreviam na home area do chamador. Entretanto o compilador
Linux/PS5 passa esses argumentos em RDI/RSI/RDX e não reserva essa home area.
O primeiro teste real saltou para uma vtable em vez do código recompilado.

Os overlays corrigem a entrada do host sem alterar a convenção interna guest.
Na saída, o thunk converte contexto e três argumentos inteiros/ponteiros para
os registradores System V, preserva RSI/RDI e XMM1..15 e mantém RAX/XMM0 como
retornos. A resolução lenta de funções usa a mesma fronteira apropriada.

Os helpers vetoriais recebem endereços de valores guardados na pilha. Seus
argumentos SIMD são referências explícitas no System V; seus resultados
continuam valores SIMD. Isso também se aplica às declarações e implementações
de tracing. Os slots dos thunks foram ampliados para salvar todos os vetores
necessários, mantendo o alinhamento da pilha.

`tools/prepare.py` gera os overlays em `build/generated` e
`build/generated-sources`. O checkout Xenia fixado em
`95a5c3ee250f80c3b9d139658649d9ffb6db3eec` permanece sem alterações.
O CMake registra os headers novos como dependências para não reutilizar objetos
antigos que ainda apontavam para os headers upstream.

## Memória e serviços de plataforma

`platform/ps5/memory.cpp` usa mmap/mprotect e arquivos compartilhados reais:

- reservas não substituem mapeamentos alheios; um hint deslocado é recusado;
- recommit preserva os bytes e os aliases em vez de remapear memória anônima;
- proteção, consulta e liberação verificam a posse e os limites da região;
- arquivos de backing são exclusivos, dimensionados e removidos ao fechar;
- decommit anônimo usa as APIs normais; decommit de backing compartilhado ainda
  é recusado explicitamente, sem simular uma operação bem-sucedida;
- uma negativa de memória executável é propagada, sem alterar privilégios.

O backing guest foi ampliado em uma página de 4 KiB: o alias E0000000 inclui
esse deslocamento e o último intervalo não cabia no tamanho original. O
upstream já ajusta esse alias quando a granularidade de alocação é maior que
4 KiB; esse caminho e as proteções de páginas precisam de validação no PS5.

`thread_primitives.cpp` implementa TLS, identificação de thread, yield, sleep
e barreiras. Não implementa Thread/Wait/APC nem o scheduler guest completo.
O logging é limitado e síncrono. Os overlays POSIX usam off_t de 64 bits,
tratam MAP_FAILED e offsets de arquivos, e mantêm a frequência do relógio
monotônico em nanosegundos coerente com a unidade dos contadores.

`exception_handler.cpp` transporta SIGSEGV/SIGILL pela estrutura BSD do SDK,
com validação de tamanho, registradores e formato do estado SIMD. Um contexto
incompatível é recusado; um fault não tratado segue para o handler anterior
ou para o encerramento normal. O layout **ainda não foi validado no hardware**.
Handlers só podem ser removidos depois de parar as threads que podem chamá-los.
Callbacks de faults e logging também precisam de auditoria de reentrância
antes de servir ao cache de texturas e MMIO de um jogo.

## Conteúdo escolhido

O `default.xex` fornecido para Sonic the Hedgehog foi inspecionado somente para
metadados. Nenhum arquivo do jogo foi modificado, enviado ou executado.

| Campo | Valor |
|---|---|
| Title ID | 534507D6 |
| Tamanho do XEX | 14.102.528 bytes |
| Image base | 0x82000000 |
| Entry point | 0x82537AE0 |
| Tamanho da imagem | 14.286.848 bytes |
| Bibliotecas importadas | xam.xex; xboxkrnl.exe |
| Registros de importação | 66; 256 |

Esses 322 registros não significam 322 APIs distintas ou implementadas.
O SHA-256 do XEX é
`373689b7c8bc839fbc00f55f756da7a1ff55d786d043423606db8bf7f3905c94`.
O relatório local `build/sonic-preflight.json` inclui a contagem dos arquivos
da pasta. O pacote de desenvolvimento não contém o jogo ou chaves.

## Builds reproduzíveis

```powershell
./build.ps1 RuntimeHost
./build.ps1 RuntimePS5
```

No container Linux já usado pelo projeto:

```bash
bash tools/build-runtime.sh host
PS5_PAYLOAD_SDK=/ws/Castation/native-ps5/.deps/native/ps5-payload-sdk \
  bash tools/build-runtime.sh ps5
python3 tools/package-runtime.py
```

Saídas: `build/runtime-host/runtime-tests.xml`,
`build/runtime-ps5/{memory,runtime,development}-receipt.json` e
`dist/Xbox360PS5-M5-runtime-development.zip`. Esse ZIP é para desenvolvimento,
não é um instalador nem um teste de jogo. Os ELFs ainda exigem uma integração
de aplicação nativa e uma validação de execução no console.

## O que falta para iniciar Sonic

1. Validar no título PS5 a execução do cache x64, as reservas, a proteção de
   memória, o TLS e a recuperação de exceções, mantendo logs e falhas claras.
2. Integrar o kernel guest real, import resolver de xam/xboxkrnl, objetos,
   threads, waits/APCs, VFS e ciclo de vida. O source XexModule já compila, mas
   seu carregamento completo e resolução de imports ainda não estão ligados
   e exercitados no teste M5.
3. Compilar o driver RADV/PS5 compatível, validar recursos e apresentação e
   integrar o provider Vulkan e o command processor Xenos do Xenia.
4. Integrar áudio, input guest, encerramento e carregamento do jogo pelo VFS.
5. Produzir e instalar uma variante de aplicativo que realmente inicia o XEX,
   acompanhar seu primeiro frame e diagnosticar a execução antes de anunciar
   um teste jogável. Compatibilidade e desempenho ainda precisam ser medidos.

Os projetos PS5SX2, ProsperoEden e PS5_Vulkan continuam referências para
runtime, driver e apresentação. Eles não fornecem o kernel guest Xbox 360 nem
substituem o backend Xenos. Consulte `PS5_VULKAN_REFERENCE.md` e
`PROSPEROEDEN_REFERENCE.md` para as diferenças observadas.
