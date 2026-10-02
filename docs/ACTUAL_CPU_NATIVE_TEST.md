# M7 — Execucao real PPC/x64 no titulo PS5

Este teste **nao e um teste definitivo de jogo**. Ele substitui a comparacao HIR
do M4 por execucao do backend x64 real do Xenia: traducao PPC, leitura/escrita
guest, chamadas guest/host, resolucao de funcoes e preservacao de vetores.
O nome no console e `Xbox360PS5 Actual CPU Runtime Test`, **PPSA50010**.
PPSA50008, PPSA50009 e os pacotes anteriores permanecem preservados.

O host executa todos os 50 casos duas vezes, destruindo e recriando o runtime.
O aplicativo PS5 executa ao pressionar X; repetir X testa reinicializacao.
So mostra `CPU JIT PASS` com status 0, exatamente 50 casos e zero falhas.
Um erro na reserva ou no acesso a memoria executavel e uma falha de
inicializacao, nao um teste aprovado com zero casos.

O aplicativo grava em `/download0/xbox360ps5-m7.log`, inclusive stdout/stderr
do teste original. Os arquivos temporarios de memoria/cache tambem usam
`/download0`, nao `/app0`, que e somente leitura. O encerramento normal do
runtime libera os arquivos; um crash pode deixar backing files para analise.
O backing e esparso e tem cerca de 4.5 GiB logicos, nao um pacote de jogo.

## Correcao especifica do titulo nativo

Ligar o runtime inicialmente introduziu `kernel_mprotect`, uma dependencia
do SDK de payload que nao tem import valido no titulo nativo. O M7 usa wrappers
de `mmap`, `mprotect` e `munmap` ligados a `sceKernelMmap`,
`sceKernelMprotect` e `sceKernelMunmap`, APIs normais do proprio processo.
Nao altera privilegios. Um erro SCE vira errno/retorno POSIX; uma negativa de
memoria executavel permanece negativa. Sete casos de host verificam o ABI,
offsets de 64 bits e propagacao dos erros. Esses mocks nao provam o hardware.

## Compilar e instalar

No container `castation-buildenv` com o SDK existente:

```bash
PS5_PAYLOAD_SDK=/ws/Castation/native-ps5/.deps/native/ps5-payload-sdk \
  bash tools/build-native-runtime-probe.sh
python3 tools/package-native-runtime.py
```

Saida em `build/native-runtime-stage/dist/PPSA50010` e
`dist/Xbox360PS5-M7-native-runtime-test.zip`, em formato de pasta nativa.
Com o servidor FTP ativo e a API ShadowMount acessivel, publicar uma nova
pasta sem sobrescrever nenhum titulo existente:

```bash
python3 tools/deploy-native-probe.py --runtime --host IP_DO_PS5
```

Abrir pelo menu PS5 e pressionar X. Recolher o log e confirmar que a segunda
execucao tambem termina. Registrar os hashes do eboot/libc juntamente com o
resultado. Nao registrar validacao no hardware somente porque o FSELF foi
gerado ou o upload terminou.

Na etapa M7 ainda faltavam kernel guest, VFS/XEX completo, driver RADV, command processor
Xenos, audio e input guest para um teste de Sonic. O teste M7 nao executa o
XEX fornecido, nao inicia Vulkan e nao afirma compatibilidade ou desempenho.
Para a integracao posterior M8, consultar [ENGINE_INTEGRATION.md](ENGINE_INTEGRATION.md).
