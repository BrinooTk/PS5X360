# Porte Xbox 360 para PS5

## Núcleo selecionado

[Xenia](https://github.com/xenia-project/xenia), revisão
`95a5c3ee250f80c3b9d139658649d9ffb6db3eec`. A versão escolhida inclui backend
x64 e renderizador Vulkan. O porte usa esses componentes; não tenta converter
um emulador de Dreamcast em Xbox 360.

## Mapa de adaptação

| Área | Código do Xenia | Trabalho PS5 |
|---|---|---|
| Identidade da plataforma | `base/platform.h` | Overlay PS5 explícito, já implementado |
| Xenon PowerPC | `cpu/ppc/`, `cpu/hir/` | Decoder/disassembler e builder HIR compilados; frontend PPC e passes ainda pendentes |
| Recompilador x64 | `cpu/backend/x64/` | Portar cache de código, proteção de memória e contexto de exceções |
| Memória | `memory.cc`, `base/memory_posix.cc` | Layout guest, aliases, reservas e faults; não presumir compatibilidade POSIX |
| Xenos | `gpu/vulkan/`, `gpu/spirv_shader_translator*` | Driver Vulkan PS5, recursos exigidos e apresentação nativa |
| Áudio | `apu/`, backend de áudio | XMA/FFmpeg e AudioOut; confirmar formato e ritmo |
| Controle | `hid/` | Backend DualSense |
| Arquivos | `vfs/`, `cpu/xex_module*`, `kernel/util/xex2_info.h` | Leitura de conteúdo, saves e diretórios acessíveis pelo título |
| Threads | `base/threading*`, `kernel/xthread*` | TLS, sincronização e agendamento de threads guest |
| Aplicativo | `app/`, `ui/` | Inicialização PS5, biblioteca, ciclo de vida, fechamento e logs |

## Referências fornecidas

- [PS5SX2](https://github.com/Swordpdf/PS5SX2): estrutura de app nativo,
  integração de um emulador com Vulkan PS5 e cache de shaders.
- [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden): link RADV,
  superfície `VK_KHR_display`, frontend e persistência.
- [PS5CEMU](https://github.com/premohq/PS5CEMU): organização dos adapters de
  plataforma e separação entre build de verificação e app com driver real.
  O README consultado declara que o aplicativo ainda não foi testado em hardware.
- [NATIVE-EMUS-PS5](https://github.com/klortekhq/NATIVE-EMUS-PS5): contratos de
  runtime e organização de marcos por componente. A matriz trata Xenia como
  pesquisa; não oferece um emulador Xbox 360 validado no console.

## Sequência de validação

1. Compilar e testar decoder/disassembler upstream (marco atual).
2. Compilar frontend PowerPC, HIR e backend x64 sem frontend desktop.
3. Implementar os adapters PS5 de memória, threads e arquivos; comparar
   pequenos programas PowerPC sintéticos com os testes do Xenia.
4. Integrar driver Vulkan e verificar os recursos efetivos do PS5 antes de
   iniciar o command processor Xenos. Comparar shaders/quadros sintéticos.
5. Integrar carregamento XEX, kernel guest, áudio e input; iniciar conteúdo de
   teste com logs persistentes e encerramento completo.
6. Gerar título PS5 separado e validar no console; medir jogos e resolver
   divergências antes de experimentar upscale.

Nenhuma etapa deve ser marcada como executada no PS5 apenas por compilar no
computador. A primeira compilação usa o SDK de payload: seu ELF ainda precisa
ser integrado ao runtime e ao formato de título nativo.

## Experiência do Castation que se aplica aqui

Separar formato de pacote, acesso a arquivos e privilégios de memória. Um
payload funcional não comprova que o mesmo código tenha as mesmas condições
num título registrado. Fazer a inicialização reportar capacidades e falhar
com uma mensagem compreensível antes de iniciar um backend indisponível.
Guardar build, símbolos e hash para relacionar cada dump ao executável certo.
Evitar prometer desempenho com base apenas na especificação do hardware.
