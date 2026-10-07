# Primeiro teste de movimento

Este protótipo testa a câmera e o rastreamento do corpo. Agora pode enviar articulações ao receptor de pesquisa do PS5. Ainda não executa jogos Kinect.

## No PC

1. Abra um terminal na pasta do projeto.
2. Execute `python -m http.server 8780 --bind 127.0.0.1 --directory tools/kinect-phone`.
3. Abra http://localhost:8780 no navegador.
4. Clique em **Start camera** e permita o uso da câmera. O primeiro uso baixa o modelo e pode demorar.
5. Fique em um ambiente iluminado, com o corpo inteiro visível. Afaste-se o suficiente para aparecerem pés e mãos.
6. Levante cada braço, dobre os joelhos e dê passos para os lados. Observe se os pontos acompanham o corpo e se as mãos não trocam de lado.
7. Saia do enquadramento: deve aparecer a mensagem pedindo um corpo visível. Volte e observe se o rastreamento retoma.
8. Clique em **Export pose** para salvar uma amostra. **Stop** encerra a câmera; trocar para outra aba também encerra.

Registre aparelho/navegador, tempo de resposta percebido, perda de rastreamento, distância aproximada e iluminação. Compartilhe apenas o JSON exportado se quiser analisar o mapeamento; o protótipo não envia imagens ou movimentos automaticamente.

## No celular

A mesma página precisa ser servida por **HTTPS** com certificado aceito pelo aparelho. O endereço HTTP local do PC ou do PS5 não libera a câmera do celular. Não desative a segurança do navegador para contornar isso.

O aplicativo usa apenas a câmera RGB. Profundidade e distância são estimadas, não medidas por um sensor Kinect. O protótipo exporta 20 articulações aproximadas; cabeça, mãos e coluna precisam de calibração e validação.

## Etapas seguintes

1. Validar o rastreamento no aparelho escolhido e calibrar posição/orientação.
2. Implementar uma conexão autenticada na rede local, com tratamento de desconexão e dados expirados.
3. Implementar os serviços NUI que os jogos consultam no núcleo do emulador.
4. Validar um jogo Kinect por vez em uma build separada. Somente então anunciar o sensor como disponível para o jogo.

Nenhuma dessas funções está ativada no emulador atual. Os jogos que não usam Kinect continuam com o caminho de entrada existente.

## Teste com o PS5 (build .19)

1. Ligue a página de configurações em Sistema e o Movimento por câmera (pesquisa) em Controles.
2. Encerre o servidor estático antigo e rode `python tools/kinect-phone/server.py --console 192.168.0.19`.
3. No PC, abra http://localhost:8780, digite a chave mostrada no PS5 e clique em Connect PS5. Depois clique em Start camera.
4. Abra Dragon Ball Z for Kinect. O painel de pesquisa deve desenhar as articulações sobre o jogo.
5. Saia da câmera ou clique em Stop: o desenho deve deixar de rastrear em até 500 ms.
6. O jogo ainda pode avisar que não há Kinect. Os registros Motion research mostram quais serviços ele pede; a emulação desses serviços será a próxima etapa.

A chave muda ao reiniciar o emulador. Não é a URL do AutoLog. A conexão é local e não envia vídeo. O receptor vem desligado por padrão.
