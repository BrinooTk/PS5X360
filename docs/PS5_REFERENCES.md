# Referencias PS5 e conexao Vulkan — M6

Analise de codigo realizada em 2026-10-02. As revisoes abaixo sao snapshots,
nao garantias de compatibilidade com o firmware ou com jogos do usuario.

| Projeto e revisao consultada | O que ajuda neste port | Limite |
| --- | --- | --- |
| [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden/tree/46ed1e8991684528aaeb927d1e6a8547f1db0506), `46ed1e8991684528aaeb927d1e6a8547f1db0506` | `headless/vulkan.cmake`, `headless/vulkan_surface.cpp`, `tools/build-radv-dependencies.sh`, `tools/radv-link-eden.sh`: driver estatico, superficie KHR_display, isolamento de simbolos, TLS e runtime C++ | Seu nucleo Switch e sua traducao de shaders nao substituem PowerPC/Xenos. O driver Mesa fixado por esse snapshot difere do PS5_Vulkan atual. |
| [XPSemu](https://github.com/ZiZc3/XPSemu/tree/a1db5328af84aa651709b2004c1645a65ba57182), `a1db5328af84aa651709b2004c1645a65ba57182`; [releases](https://github.com/ZiZc3/XPSemu/releases) | `ps5/configure-ps5.sh`, `ps5/ps5-cross.ini`, `ps5/deps/build-deps.sh`, `ps5/build-title.sh`: cross build FreeBSD, dependencias estaticas e compartilhamento da receita RADV | Emula Xbox original: QEMU/TCG, CPU x86 e NV2A. Nao fornece kernel, PowerPC ou GPU do Xbox 360. A leitura das releases nao equivale a testar seus binarios. |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan/tree/3f3ee69607013b345d2baa6d6a37c86745649a08), `3f3ee69607013b345d2baa6d6a37c86745649a08` | `tools/build-radv.sh`, `tools/radv-link.sh`, `tooling/radv/ps5-cross.ini`: RADV/ACO/NIR, winsys PS5 e apresentacao VideoOut por KHR_display | A receita atual fixa Mesa `0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8` e exige um fork de SDK com `libps5platform.a`. O SDK antigo do nosso teste de CPU nao e equivalente. Os resultados CTS publicados pertencem ao projeto de referencia, nao ao Xbox360PS5. |
| [PS5CEMU](https://github.com/premohq/PS5CEMU/tree/c8f1291a15b1499cb8be881953d3d80dce8567b0), `c8f1291a15b1499cb8be881953d3d80dce8567b0` | `port/ps5/vulkan_display.cpp`, `port/cemu/VulkanPS5.cpp`, `tools/build-radv.sh`: conectar um renderizador existente a um ICD estatico e ao VideoOut | A superficie consultada assume o primeiro dispositivo/tela e nao verifica todos os retornos/capacidades. Nosso adaptador trata esses casos explicitamente. GX2 e APIs Wii U nao servem como Xenos/kernel Xbox. |
| [PS5SX2](https://github.com/Swordpdf/PS5SX2/tree/d96a7031615734cbe4efb1b3953ddfc23e0dd3aa), `d96a7031615734cbe4efb1b3953ddfc23e0dd3aa` | `ps5/frontend/fe_vk.cpp`, `ps5/coreorbis/orbis-shims/orbis_vk.cpp`, estrutura de frontend e runtime nativo: carregar dispatch global/instance/device com verificacao e detectar RADV por sua entrada ligada | Usa PCSX2, com caminhos ps5vk e RADV; nao misturar binarios, CRTs ou SDKs de builds diferentes. Os recompiladores PS2 nao traduzem PowerPC. |

Os repositorios foram consultados como referencias de arquitetura. Nenhum
helper de elevacao, patch de kernel ou instalador deles foi executado ou
incorporado. O novo codigo de conexao Vulkan foi escrito neste projeto sob BSD;
nao copia implementacoes GPL dos emuladores.

## O que foi implementado

`platform/ps5/vulkan_platform.cpp` recebe `PFN_vkGetInstanceProcAddr`.
Isso permite utilizar futuramente a entrada do ICD RADV ligado estaticamente,
sem `dlopen`, XCB ou Win32 no PS5. O teste host usa o loader Linux.

A classe possui instancia, dispositivo logico, fila e superficie. Pede Vulkan
1.1 e, para VideoOut, exige `VK_KHR_surface`, `VK_KHR_display` e
`VK_KHR_swapchain`. Enumera todos os dispositivos, modos e planos; valida
capacidade, associacao do plano, transformacao, alpha, tamanho e posicao.
Seleciona o modo mais proximo do tamanho solicitado e depois da frequencia;
59.94 Hz e aceito para um pedido de 60 Hz. Usa o indice de stack real do plano.
A fila precisa suportar graphics, compute e, quando ha superficie, apresentacao.

Enumeracoes podem retornar `VK_INCOMPLETE`: ha limite de oito tentativas e
16384 entradas, evitando loops e alocacoes descontroladas. Reset/destrutor
esperam o dispositivo e liberam recursos em ordem; reinicializacao e falhas
parciais tambem fazem limpeza. Como em qualquer consumidor Vulkan, o driver
precisa obedecer ao contrato basico: uma instancia criada com sucesso precisa
fornecer seu destrutor. Um ICD invalido sem esse comando nao permite liberar a
instancia. O driver deve permanecer carregado ate depois do destrutor da classe.

Os recursos sao de uma sessao; chamadas concorrentes a Initialize/Reset nao sao
suportadas. A classe nao e um scheduler nem uma implementacao de swapchain.

## Validacao e builds

`./build.ps1 VulkanHost` cria um container separado com loader Vulkan e Mesa
Lavapipe. Roda as oito suites existentes da CPU/memoria, a suite de contrato
Vulkan e um teste real de transferencia: preencher buffer, barreira, copiar,
esperar fence, invalidar memoria mapeada e conferir 1024 palavras.
Lavapipe e um driver de software no computador: isso valida chamadas,
sincronizacao e readback, **nao mede desempenho nem prova GPU do PS5**.

`./build.ps1 VulkanPS5` compila a mesma biblioteca e o teste de contrato para
`x86_64-sie-ps5`; o teste usa dispatch simulado e nao precisa de RADV. O ELF e
validado estruturalmente. `tools/package-vulkan-platform.py` gera o pacote de
desenvolvimento e registra hashes, fontes e resultados. Nao gera titulo
instalavel nem substitui PPSA50009.

## O que falta para um teste de jogo

1. Construir RADV e seu SDK/runtime **na mesma revisao**, em diretorios isolados;
   ligar sua entrada ICD e verificar a conexao no PS5. O driver ainda nao esta
   incluido neste build. Importar somente o archive RADV sem suas dependencias
   de heap/TLS/thread/AGC e sem whole-archive nao equivale a integrar Vulkan.
2. Adaptar os objetos reais `VulkanInstance`, `VulkanDevice` e presenter do
   Xenia ao ICD estatico e KHR_display; preservar as verificacoes de recursos
   que o Xenia exige para emulacao GPU. Esta classe ainda nao e chamada pelo
   Xenos e seu dispositivo minimo de teste nao habilita esses recursos.
3. Integrar command processor Xenos, memoria/EDRAM, SPIR-V, descriptors,
   pipelines, fences e swapchain; testar desenho e readback no hardware.
4. Completar kernel guest, filesystem, threads e carregamento XEX do Xenia.
   A traducao sintetica PPC/x64 aprovada no host nao e execucao de um jogo.

Nao declarar jogo pronto, GPU ativa, upscale ou 60 FPS antes dessas provas.
O jogo Sonic fornecido permanece no diretorio original, sem alteracoes.
