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

O ELF é um probe de componentes, não um pacote instalável nem um emulador
jogável. Não há validação de hardware nesta etapa.

## Arquitetura e próximos marcos

Ver [docs/PORTING.md](docs/PORTING.md). Cada componente entra no build com
validação própria antes da integração ao aplicativo. O objetivo gráfico é
usar o renderizador Vulkan do Xenia no driver PS5, com AudioOut e DualSense
como serviços nativos.

Xenia é experimental e sua compatibilidade varia entre jogos. A potência do
PS5 por si só não garante compatibilidade, resolução ou 60 FPS. Essas métricas
precisam ser medidas depois de CPU e GPU funcionarem corretamente.

## Licenças

Código original deste projeto: MIT. Xenia: BSD de três cláusulas; fmt: licença
MIT e exceção indicada em sua licença. As cópias dos avisos estão em
`licenses/`. Nenhum jogo, BIOS ou chave é distribuído.
