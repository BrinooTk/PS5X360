# CPU: componentes HIR iniciais

## Continuação M3: frontend e backend compilados

O build agora também produz `libxenia_ppc_frontend.a`,
`libxenia_cpu_compiler.a`, `libxenia_x64_backend.a` e `libxenia_capstone.a`,
tanto no host quanto no alvo Prospero. O frontend inclui scanner, tradutor,
builder PPC e as cinco categorias de emissores. Os passes de compilador e
os arquivos comuns/POSIX do backend x64 entram como componentes upstream.
O cache POSIX compilado não é ainda um adapter de memória PS5 validado.

O bloqueio de `base/atomic.h` foi resolvido com um overlay PS5 explícito:
operações Clang `__atomic` com consistência sequencial, exchange/fetch-add
retornando o valor anterior e CAS forte. Nenhuma outra plataforma foi alterada.
O ramo PS5 é exercitado no host por definição explícita no target de teste.

Foram executadas cinco suites no host com zero falhas:

| Suite | Casos | O que verifica |
|---|---:|---|
| Decoder PowerPC | 9 | Instruções escalares/VMX/VMX128 e opcode inválido |
| HIR | 20 | Valores, blocos, operandos e montagem de operações |
| Atômicos PS5 | 13 | Retornos, CAS, wrappers e atualizações concorrentes |
| Otimizador | 24 | Três construções/otimizações, swaps redundantes e código morto, preservando store e seu operando |
| Xbyak/Capstone | 8 | Geração e decodificação x64, imediato e resolução de label |

O teste Xbyak usa buffer de dados fornecido pelo chamador e não executa o
código gerado. Não reserva memória executável. Todos os cinco probes foram
ligados para PS5 e tiveram estrutura ELF verificada; os novos probes não foram
executados no console. `build/host/cpu-tests.xml` guarda o resultado de host.

`python tools/package-cpu-probe.py` valida os objetos ELF64 x86-64 dentro das
seis bibliotecas, gera hashes/revisões em `build/ps5/cpu-receipt.json` e cria
`dist/Xbox360PS5-M3-CPU-development-probe.zip` com os probes, arquivos e avisos.
É um pacote de desenvolvimento, não um aplicativo instalável.

Compilar um arquivo estático não resolve sua ligação completa. O levantamento
de símbolos mostra dependências de Processor, memória guest/host, config/logs,
thread state e exception handling. Elas precisam de implementações reais antes
de iniciar o frontend ou o JIT. Não foram ocultadas por stubs. Próximo passo:
integrar esses serviços e ligar um teste sintético PPC -> HIR, depois validar
a execução x64 dentro do runtime nativo.

## Evidência anterior M2

Atualizado em 2026-10-01. O título M1 validado no console continua separado.

Entraram no build os arquivos upstream `arena.cc`, `hir_builder.cc`,
`block.cc`, `instr.cc`, `opcodes.cc` e `value.cc`. Nenhum comportamento de CPU
foi substituído por stub e o checkout do Xenia não foi modificado.

`src/hir_smoke.cpp` liga esses componentes em um executável e verifica 20
casos. Além da avaliação de constantes, constrói uma operação de soma a
partir de um registrador de contexto, grava o resultado, finaliza o bloco e
verifica o dump de tradução. Também verifica a manutenção das listas de
consumidores ao substituir operandos e remover instruções.

## Evidência

- `./build.ps1 Host`: duas suites aprovadas, decoder e HIR; HIR com 20 casos
  e zero falhas. Executado em Linux dentro do Docker, não no PS5.
- `./build.ps1 PS5`: bibliotecas e executáveis ligados com o SDK Prospero.
- `tools/verify.py xenia-hir-smoke`: ELF64 x86-64, segmentos dentro do arquivo,
  alinhamento dos segmentos de carga e entry point executável verificados.
- SHA256 do ELF HIR: `be5a941fe6174146c26e7c158dd75ab414c36ffebfb1fc680b92f865ec1b2cfe`.

O ELF HIR é um artefato do SDK de payload, não um título nativo instalável.
Ele não foi enviado ao console. A evidência M1 de vídeo/input/decoder não
valida este componente novo. O pacote M2 é somente para desenvolvimento.

## O que falta nesta fase

O teste não traduz bytes PowerPC nem executa código guest. Faltam integrar
`PPCHIRBuilder`, scanner/frontend, passes do compilador e o backend x64.
O builder PPC inclui Processor, flags, logging e memória; não é suficiente
compilar os emitters isoladamente para obter uma CPU funcional. O backend
x64 acrescenta Xbyak, Capstone, cache de código e transições host/guest.

A próxima verificação deve ligar um caminho PPC -> HIR com programa sintético,
depois testar a geração x64 e adaptar memória/TLS/threads. A estratégia de
memória executável deve ser validada especificamente para o runtime nativo.
Construir HIR não exige nem comprova disponibilidade de JIT. Vulkan e jogos
permanecem pendentes.
