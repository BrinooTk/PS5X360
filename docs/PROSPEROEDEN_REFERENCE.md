# Referência de plataforma: ProsperoEden

Revisão da documentação e dos scripts públicos em 2026-10-01:
https://github.com/blackbearreloaded/ProsperoEden

A cópia local consultada está em `336ff39891ee924df94bc421062e2ec11144bc7f`
e não contém o diretório `headless` anunciado no repositório online. Não usá-la
como fonte completa e atual do port. Nenhum arquivo dessa cópia foi alterado.

## Aplicação ao Xbox360PS5

| Evidência consultada | Uso previsto no port Xenia |
|---|---|
| `docs/BUILDING.md`: dependências fixadas, fontes derivadas no configure, toolchain nativo | Manter revisão fixa do Xenia e overlay gerado; ampliar adapters sem editar o checkout upstream |
| `UPSTREAM.json`: Boost.Context compilado por alvo e compiler-rt emulated TLS | Auditar transições de contexto e destrutores TLS do Xenia; não ligar bibliotecas de host no PS5 |
| `tools/radv-link-eden.sh`: receita estática RADV, adaptação do loader Vulkan e wrappers de runtime | Comparar com `ui/vulkan/vulkan_provider.cc`; somente depois validar extensões e recursos requeridos por Xenos |
| `tools/check-native-gpu-thread.py`: falhas de criação, wake/stop/join, encerramento antes de destruir objetos usados pelo worker | Exigir testes equivalentes ao integrar threads de CPU/GPU; evitar travas ao fechar ou trocar jogos |
| `tools/isolate-radv.py`: separação de símbolos internos Mesa/RADV | Necessário avaliar se launcher OpenGL e emulação Vulkan coexistirem; não adicionar isolamento indiscriminadamente |
| README: pasta nativa, configuração separada dos jogos, logs e cache de shaders | Reaproveitar a organização de app/dados; frontend e cache só depois dos backends funcionais |

O README declara vídeo, áudio, controles e saves funcionando no ProsperoEden.
Isso é uma declaração do projeto de referência, não evidência de que o Xenia
já tenha essas capacidades no PS5. Eden emula Switch; o Xenia precisa de
tradução PowerPC/Xenon, kernel Xbox 360 e GPU Xenos próprios.

Não copiar JIT ARM ou offsets/layout de memória do Eden para a CPU Xenon.
Não ativar Vulkan apenas porque uma biblioteca foi ligada: consultar os
recursos exigidos pelo provider/command processor do Xenia e validar no alvo.
Próxima etapa do Xbox360PS5 permanece PPC -> HIR e backend x64; HIR já foi
compilado para PS5, mas ainda não executado no console.

## Fontes verificadas

- https://github.com/blackbearreloaded/ProsperoEden
- https://github.com/blackbearreloaded/ProsperoEden/blob/main/docs/BUILDING.md
- https://github.com/blackbearreloaded/ProsperoEden/blob/main/UPSTREAM.json
- https://github.com/blackbearreloaded/ProsperoEden/blob/main/tools/radv-link-eden.sh
- https://github.com/blackbearreloaded/ProsperoEden/blob/main/tools/check-native-gpu-thread.py
- https://github.com/blackbearreloaded/ProsperoEden/blob/main/tools/isolate-radv.py
