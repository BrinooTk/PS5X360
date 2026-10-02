# M4: teste nativo de tradução PowerPC

Aplicativo separado: **Xbox360PS5 CPU Translation Test**, título **PPSA50009**.
Compilar com `./build.ps1 NativeCPU`. O pacote em pasta sai em
`dist/Xbox360PS5-M4-native-cpu-test.zip`.

Copiar a pasta PPSA50009 para `/data/homebrew/PPSA50009` e registrar/abrir pelo
gerenciador de homebrew já utilizado no console. Não substituir PPSA50008.

Na TV, esperar os nove casos do decoder com PASS, DUALSENSE READY e:

- `PPC HIR CASES 24`
- `PPC FAILURES 0`

Pressionar X repete os testes. O log fica em
`/mnt/sandbox/PPSA50009_000/download0/xbox360ps5-m4.log`, visto pelo FTP após
abrir o aplicativo. O resumo registra casos, falhas e execuções.

## Escopo real

O teste usa emissores originais do Xenia para ADDI, ORI, LWZ e STW. Um pequeno
avaliador HIR de teste confere registradores e bytes de memória antes/depois
dos passes reais de simplificação e remoção de código morto. Ele rejeita
operações fora do subconjunto em vez de ignorá-las. Esse avaliador não é um
interpretador de produção nem prova execução do JIT.

O único método adicionado ao header gerado do builder é um reset da informação
de trace por instrução, igual ao que o Emit upstream faz em seu loop. A ligação
por seções retém apenas o caminho usado pelo teste; Processor, guest memory,
thread scheduling, faults, scanner e Emit completo não são inicializados.
Não há stubs substituindo esses serviços; eles continuam pendentes.

Foram integrados os arquivos upstream de configuração e UTF-8 necessários à
ligação deste caminho. Compilam com `-fno-char8_t`, preservando as strings UTF-8
de C++17 esperadas pelo código fixado. O aplicativo nativo habilita exceções e
RTTI para corresponder às bibliotecas do Xenia e capturar falhas do avaliador.

Esta variante liga `libc++.a`, `libc++abi.a`, `libunwind.a` e os helpers de
`libc.a` do SDK instalado. O runtime mínimo de operadores de alocação do M1
não entra nesta ligação. O hook de observação de alocação do renderer continua
presente; ele não substitui o allocator. O linker gerado define os limites de
`.eh_frame` e `.eh_frame_hdr` exigidos pelo unwinder. Os hashes das bibliotecas,
do script de build e do linker ficam no recibo. Essas mudanças são exclusivas
do stage M4; o SDK, Castation e a pasta instalada do M1 não são alterados.

A compilação não demonstra, por si só, funcionamento de TLS e unwind no PS5.
O teste normal não provoca uma exceção deliberada; caminhos de exceção e
serviços completos do runtime exigem validação adicional antes de usar o JIT.

O teste não abre jogos, não inicializa Vulkan e não aloca memória executável.
Só marcar hardware validado após observar a tela/log desta versão no console.

## Resultado no console em 2026-10-01

O usuário confirmou 24 casos, zero falhas e incremento do contador ao pressionar
X. O log recolhido por FTP registrou 41 execuções, todas com 9 casos do decoder
e 24 da tradução sem falhas. A evidência em `evidence/M4_HARDWARE.json` associa
esse resultado ao SHA256 do executável enviado. Isso valida o subconjunto
sintético e o uso normal do runtime C++ neste teste; não valida jogos, execução
de código x64 gerado, exceções lançadas deliberadamente ou Vulkan.
