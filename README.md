# Xbox360PS5

Port experimental do Xenia para PS5, iniciado em 2026-10-01. Projeto separado
do Castation. O núcleo vem do Xenia; o trabalho aqui adapta a plataforma PS5.

**Ainda não executa jogos.** O primeiro marco compila o decodificador PowerPC,
as tabelas de instruções, o disassembler e o suporte de strings do Xenia para
o alvo `x86_64-sie-ps5`. Um teste de host verifica instruções de controle,
memória, VMX, VMX128 e uma instrução inválida. O mesmo teste é ligado em um ELF
com o SDK público do PS5, sem executar qualquer teste de JIT ou GPU.

O núcleo está fixado em `95a5c3ee250f80c3b9d139658649d9ffb6db3eec`.
`tools/prepare.py` gera um overlay de `platform.h` que reconhece
`__PROSPERO__` como `XE_PLATFORM_PS5`. A plataforma não se apresenta como Linux.
O checkout do Xenia fica intacto em `.deps/xenia`.

## Compilar

No Windows, com Docker e a imagem local `castation-buildenv`:

```powershell
python tools/prepare.py --fetch
./build.ps1 Host
./build.ps1 PS5
./build.ps1 Native
./build.ps1 NativeCPU
```

O script PS5 usa o SDK já presente no workspace. Pode-se indicar outro caminho
com `-Sdk`. No Linux:

```bash
python3 tools/prepare.py --fetch
bash tools/build.sh host
PS5_PAYLOAD_SDK=/caminho/ps5-payload-sdk bash tools/build.sh ps5
```

Saídas:

- `build/host/xenia-platform-smoke`: teste executável no computador.
- `build/ps5/libxenia_ppc_decoder.a`: primeiros componentes reais do núcleo.
- `build/ps5/xenia-platform-smoke`: ELF de teste do SDK PS5.
- `build/ps5/receipt.json`: hash, revisão, formato e status de validação.
- `build/ps5/libxenia_hir_values.a`: builder, blocos, instruções e valores HIR upstream.
- `build/ps5/xenia-hir-smoke`: teste de desenvolvimento desses componentes.
- `build/ps5/hir-receipt.json`: validação estrutural e hash do novo ELF.

O teste HIR tem 20 casos aprovados no host: aritmética, ordem dos bytes,
vetores, gestão de operandos, construção e limpeza de blocos de tradução.
Os mesmos componentes e o teste foram compilados e ligados para PS5.
Esse teste HIR ainda não foi executado no console. Ver
[docs/CPU_TRANSLATION.md](docs/CPU_TRANSLATION.md).

A continuação M3 compila frontend PPC, passes de otimização e backend x64
como bibliotecas para PS5. Cinco suites de host (74 casos ao todo) passaram.
Incluem testes dos atômicos PS5 e comparação Xbyak/Capstone; ainda não há
execução guest/JIT. O pacote de desenvolvimento pode ser gerado com
`python tools/package-cpu-probe.py`, após os builds Host e PS5.

M4 acrescenta uma sexta suite, com 24 verificações de tradução dos emissores
PPC originais e comparação por avaliador HIR de teste (98 casos de host no
total). `NativeCPU` gera **Xbox360PS5 CPU Translation Test**, PPSA50009,
separado do M1. Instruções e limites em [docs/CPU_NATIVE_TEST.md](docs/CPU_NATIVE_TEST.md).
O M4 também foi confirmado no PS5: o log recolhido registra 41 execuções com
24 casos de tradução e zero falhas. A evidência fixa os hashes do executável
e do módulo libc. A execução x64 gerada e o renderer Vulkan seguem pendentes.

O ELF é um probe de componentes, não um pacote instalável nem um emulador
jogável. O ELF isolado M0 não foi executado no console.

O target `Native` produz o primeiro teste de hardware em
`dist/Xbox360PS5-M1-native-platform-test.zip`, com uma pasta `PPSA50008`
registrável no ShadowMountPlus. Ele apresenta os resultados do decoder do
Xenia e os valores do DualSense na TV. Ver [docs/FIRST_TEST.md](docs/FIRST_TEST.md).
O runtime de título é gerado a partir do toolchain local do Castation na
revisão `94dfef7`; a tela usa o renderer mínimo do PS5 Native App Boilerplate
na revisão `470695e0c557f99ff2df0e36e4df713c5e636526`, com atualização dos buffers.

## Arquitetura e próximos marcos

Ver [docs/PORTING.md](docs/PORTING.md). Cada componente entra no build com
validação própria antes da integração ao aplicativo. O objetivo gráfico é
usar o renderizador Vulkan do Xenia no driver PS5, com AudioOut e DualSense
como serviços nativos.
O teste nativo M1 foi validado no PS5: imagem e controle confirmados pelo
usuário; o log registrou 53 execuções de nove casos com zero falhas. Isso
valida os componentes iniciais, não a emulação de jogos.

Xenia é experimental e sua compatibilidade varia entre jogos. A potência do
PS5 por si só não garante compatibilidade, resolução ou 60 FPS. Essas métricas
precisam ser medidas depois de CPU e GPU funcionarem corretamente.

## Licenças

Código original deste projeto: MIT. Xenia: BSD de três cláusulas; fmt: licença
MIT e exceção indicada em sua licença. O renderer e as ferramentas do runtime
nativo mantêm GPL-3.0-or-later; os aplicativos M1 e M4 combinados são distribuídos sob
essa licença, mantendo os avisos de seus componentes. As cópias dos avisos
estão em `licenses/`, incluindo a licença LLVM do runtime C++ do SDK usado no
M4. Nenhum jogo, BIOS ou chave é distribuído.
