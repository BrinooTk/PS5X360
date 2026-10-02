# CPU: componentes HIR iniciais

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
