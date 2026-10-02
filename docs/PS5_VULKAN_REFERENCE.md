# Driver de referência: Mihawk PS5_Vulkan

Fonte consultada em 2026-10-01:
https://github.com/mihawk-99/PS5_Vulkan

O projeto contém duas rotas diferentes: RADV/Mesa como driver principal e
ps5vk como implementação anterior. A documentação anuncia Vulkan 1.4 para
RADV, mas mantém `conformanceVersion` em zero enquanto a qualificação completa
não estiver concluída. Não tratar o número de versão como comprovação de que
todas as funcionalidades necessárias ao Xenia estão disponíveis.

## Pontos úteis ao Xbox360PS5

- RADV é construído a partir de PS5_Mesa com winsys PS5. Não basta ligar uma
  biblioteca Vulkan desktop: alocação, submissão e apresentação são nativas.
- Apresentação por `VK_KHR_display` e VideoOut. O Xenia desta revisão tem
  superfícies desktop/Android em `ui/vulkan/vulkan_instance.cc`; precisará de
  um caminho de instância, superfície e presenter específico para PS5.
- `tools/radv-link.sh` reúne driver, runtime C++, unwind, emulated TLS e
  camada de plataforma do SDK. A receita também trata heap e threads. O SDK
  usado hoje pelo nosso probe não foi verificado contra essa receita.
- A ligação whole-archive do RADV preserva entradas referenciadas fracamente
  pelas tabelas de dispatch. Não reproduzir isso indiscriminadamente para
  todas as bibliotecas.
- Testes CTS e os registros de gaps ajudam a escolher casos de regressão.
  Resultados reportados pelo autor são evidência do driver de referência,
  não testes executados por este projeto no console do usuário.

## Verificação necessária para Xenos

No Xenia fixado em `95a5c3ee`, `VulkanDevice::CreateIfSupported` exige
`independentBlend` para emulação de GPU. Essa é apenas uma verificação inicial;
o command processor e o texture cache também consultam recursos, limites e
formatos. Consultar capacidades efetivas no console antes de iniciar Xenos.

Sequência de integração: fixar revisões do driver/Mesa/SDK compatíveis,
compilar e ligar uma aplicação Vulkan mínima, enumerar recursos e formatos,
validar clear/readback e apresentação, depois integrar provider e renderer
do Xenia. CPU PowerPC, kernel guest e carregador de jogos continuam trabalhos
separados. Nenhum driver foi compilado, instalado ou habilitado nesta revisão
de referência.

## Fontes

- https://github.com/mihawk-99/PS5_Vulkan
- https://github.com/mihawk-99/PS5_Vulkan/blob/main/tools/radv-link.sh
- https://github.com/mihawk-99/PS5_Vulkan/blob/main/docs/CTS_GAPS.md
- Xenia local: `src/xenia/ui/vulkan/vulkan_device.cc` e `vulkan_instance.cc`.
