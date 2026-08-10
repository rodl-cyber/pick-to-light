# 📦 Sistema Pick-to-Light (ESP32 + Odoo + MQTT)

Este projeto implementa um sistema automatizado de **Pick-to-Light** voltado para gestão de inventário e separação de pedidos. O ecossistema integra microcontroladores open-source, displays touch, sinalização em LED e um script em Python que atua como ponte de comunicação em tempo real com o ERP Odoo.

## 🚀 Funcionalidades

* **Integração Direta com Odoo:** Lê as ordens de separação ativas e atualiza as quantidades e status automaticamente no banco de dados.
* **Sincronização via MQTT:** Comunicação de altíssima velocidade e baixa latência entre o hardware físico e o servidor.
* **Interface Visual (Touch):** Caixas equipadas com telas TFT ILI9341 informam o produto, a quantidade necessária e permitem a interação tátil do operador para navegar ou reportar problemas (Falta Prod, Prod Errado).
* **Sinalização Luminosa:** Um ESP32 central atuando como "Cérebro" controla uma fita de LED endereçável, indicando visualmente qual caixa o operador deve acessar.
* **Relatórios Automáticos:** Injeta um relatório visual no "Chatter" (Log Notes) do pedido no Odoo ao final da separação da lista.

## 🏗️ Arquitetura do Sistema

O fluxo de dados é dividido em 3 partes principais:

1. **Script Python (A Ponte):** Fica ouvindo o servidor MQTT e atua como tradutor, pegando as requisições do ESP32 e enviando comandos via API XML-RPC para ler e escrever no Odoo.
2. **ESP32 "Cérebro":** Pede a lista de separação ao Python, gerencia a fila de produtos, comanda a fita de LED para iluminar a posição correta e dita o ritmo do processo para o restante do hardware.
3. **ESP32 "Caixas":** Recebem a instrução do Cérebro, exibem a interface gráfica na tela touch e devolvem o feedback da operação humana, esmagando latências locais com relógios internos rápidos (millis).

## 🛠️ Hardware Utilizado

* Placas de desenvolvimento ESP32 (ex: ESP32-C3-DevKitM-1)
* Display TFT 2.8" ou 2.4" com controlador **ILI9341**
* Touch Screen Capacitivo com controlador **FT6206**
* Fita de LED Endereçável WS2812B / NeoPixel

## 💻 Bibliotecas C++ (PlatformIO / Arduino IDE)

* `PubSubClient` (Gestão do protocolo MQTT)
* `ArduinoJson` (Empacotamento de dados estruturados)
* `FastLED` (Controle complexo de cores e posições dos LEDs)
* `Adafruit_GFX`, `Adafruit_ILI9341`, `Adafruit_FT6206` (Interface e toque da tela)

## ⚙️ Como Configurar e Rodar

### 1. Configurando o Python (Odoo Bridge)
No arquivo `integracao_odoo.py`, altere as credenciais na seção superior do código antes de executar:
* Substitua `URL`, `DB`, `USER` e `PASSWORD` com os dados de acesso reais do seu banco Odoo.
* Instale a biblioteca do MQTT no seu terminal: `pip install paho-mqtt`
* Execute o script: `python integracao_odoo.py`. Mantenha este terminal rodando em segundo plano.

### 2. Configurando o Hardware (ESP32)
Tanto no código do Cérebro quanto no das Caixas, certifique-se de configurar:
* As credenciais da sua rede Wi-Fi local (`ssid` e `password`).
* O IP / Endereço do seu broker MQTT (Atualmente configurado para testes com o broker público do HiveMQ).
* No código das Caixas, defina o número de identificação física da prateleira alterando a variável global `MINHA_CAIXA`.

Faça o upload (`Push`) dos códigos para as respectivas placas.

