# PS5X360 — guia de uso

Emulador experimental de Xbox 360 para PlayStation 5, baseado no Xenia Canary.
Nenhum jogo, BIOS ou chave acompanha o emulador: use cópias dos seus próprios jogos.

## O que é preciso

- PS5 com acesso a homebrew (testado no firmware 13.60 com etaHEN, ShadowMountPlus e kstuff).
- Um cliente FTP no computador (o PS5 com etaHEN abre FTP na porta 2121).

## Instalação

1. Copie a pasta `PPSA50011` para `/data/homebrew/PPSA50011` no PS5.
2. O ShadowMountPlus registra o título sozinho; ele aparece no menu como
   **PS5X360**.

## Jogos

Cada jogo fica na sua própria pasta, dentro de
`/data/homebrew/PPSA50011/assets/roms/`:

```
roms/Sonic the Hedgehog (2006)/default.xex
roms/Outro Jogo/default.xex
roms/Mais Um/jogo.iso
```

Formatos aceitos:

- **Pasta extraída**: `default.xex` com as pastas de dados do jogo.
- **Imagem de disco** `.iso`.
- **Pacote GOD/STFS** (Games on Demand, Xbox Live Arcade): o arquivo de cabeçalho
  com a pasta `.data` ao lado, como vêm do console.

O emulador só enxerga `assets/roms` dentro da pasta do título. Pastas em outros
lugares do PS5 ou em USB não aparecem.

Depois de copiar, abra o emulador, ou use **Quadrado → Atualizar lista de jogos**.

## Controles na prateleira

| Botão | Ação |
| --- | --- |
| Direcional / analógico | Escolher o jogo |
| X | Jogar |
| Triângulo | Detalhes e patches do jogo |
| Quadrado | Configurações |
| L1 / R1 | Filtros (Todos, Recentes, Pastas, ISO, Arcade e GOD) |
| Círculo | Fechar o painel aberto |

Durante o jogo, **clique no touchpad** para abrir o guia do emulador (no
estilo do guia do Xbox 360). Ele tem duas páginas, trocadas com L1/R1:

- **Jogo**: continuar, voltar para o menu de jogos (para trocar de jogo),
  apertar o botão BACK do Xbox no jogo e fechar o emulador.
- **Configurações**: mostrar FPS, som, filtro de imagem (Simples, CAS ou FSR)
  e o que o clique do touchpad faz.

Se preferir o touchpad como botão BACK, troque em "Clique do touchpad"; o guia
passa a abrir com OPTIONS + touchpad. O jogo continua rodando por baixo do
guia, mas não recebe os botões enquanto ele está aberto. O botão PS fecha o
emulador como qualquer jogo.

## Perfis

Os jogos de Xbox 360 gravam saves e conquistas no perfil (gamertag) conectado.
Na primeira vez o emulador cria o perfil "Player". Em **Configurações → Perfil
do jogador** você escolhe qual perfil usar ou cria outro (até 15 letras e
números, começando por uma letra). O perfil escolhido fica valendo para os
próximos jogos, e cada perfil tem os seus próprios saves.

No jogo, o DualSense vira um controle de Xbox 360: X = A, Círculo = B,
Quadrado = X, Triângulo = Y, OPTIONS = Start, clique do touchpad = Back.

## Capas

Em **Quadrado → Baixar capas**, o emulador baixa do XboxUnity (o mesmo serviço
que o Aurora usa) a capa de cada jogo que ainda não tem uma. O PS5 precisa estar
conectado à internet. As capas ficam guardadas e aparecem assim que o download
termina.

Uma capa própria sempre tem prioridade: coloque `cover.png` (ou `.jpg`) na pasta
do jogo, ou `nome-do-arquivo.png` ao lado de um `.iso`.

## Coletâneas

Jogos que abrem outro jogo pelo próprio menu (por exemplo, a Dragon Ball Z
Budokai HD Collection, que abre o Budokai 1 ou o 3) fazem o emulador reiniciar
direto no jogo escolhido. A tela pisca e volta já no jogo; isso é esperado.
Quando o jogo volta ao painel do console, o emulador volta à prateleira.

## Patches

O emulador traz o pacote de patches da comunidade do Xenia Canary (60 FPS,
resolução, desligar efeitos e outros), em `assets/patches`. Todos vêm
desligados. Para ligar: Triângulo no jogo → escolha o patch → X.

- O emulador identifica o jogo pelo próprio executável (ou pela imagem `.iso`),
  então os patches aparecem antes mesmo da primeira vez que ele abre.
- Um patch só é aplicado se foi feito para a mesma versão do executável do seu
  jogo; se não for, o painel avisa.
- Patches de 60 FPS podem mudar a velocidade do jogo ou causar problemas em
  algumas cenas. Se algo der errado, desligue o patch.

## Configurações (Quadrado)

- **Idioma**: Automático usa o idioma do PS5. A interface tem português, inglês e espanhol; sem uma tradução disponível, usa inglês. Também é possível escolher manualmente. A seleção informa o idioma ao próximo jogo iniciado, desde que o jogo tenha essa tradução. Preferências antigas de idioma dos jogos são preservadas até uma nova seleção.
- **Som**: ligado ou mudo.
- **Registros detalhados**: grava cada chamada do jogo ao sistema. Deixa o jogo
  mais lento; use só para investigar um problema.
- **Mostrar FPS no jogo**: contador de quadros por segundo no canto da tela.

## Quando algo dá errado

- O registro do emulador fica em `/download0/xbox360ps5/engine.log`, dentro da
  área de dados do título.
- A cada 30 segundos de jogo o registro anota a média de quadros por segundo.
- Ao relatar um problema, diga o jogo, a versão (região), o que aparece na tela e
  se algum patch estava ligado.

A compatibilidade varia de jogo para jogo. Alguns jogos podem não abrir, travar
ou ter falhas gráficas e de som.
