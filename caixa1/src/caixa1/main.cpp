#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_FT6206.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==========================================
int MINHA_CAIXA = 1; 
// ==========================================

#define TFT_CS   7
#define TFT_DC   10
#define TFT_RST  3

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Adafruit_FT6206 ts = Adafruit_FT6206(); 

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* topico_cerebro_envia = "tg/rodrigo/cerebro/tx"; 
const char* topico_caixas_enviam = "tg/rodrigo/caixas/tx";  

WiFiClient espClient;
PubSubClient client(espClient);

struct PacoteDados { int indice; int indice_max; String codigo; String nome; int qtd; };
PacoteDados meusDados = {0, 0, "", "", 0};

enum EstadoTela { 
  TELA_INICIAL, TELA_AGUARDANDO, TELA_ESTOQUE, TELA_STATUS, TELA_ERRO_MOTIVO, 
  TELA_ESPERA_CONFIRMACAO, TELA_ESPERA_PROXIMO, TELA_ESPERA_ANTERIOR, 
  TELA_FINAL, TELA_MENSAGEM_ODOO 
};

EstadoTela estadoAtual = TELA_AGUARDANDO;
String statusProduto = "Pendente";
unsigned long tempoInicioBuffer = 0;
unsigned long ultimoTempoReconnect = 0;
unsigned long ultimoToque = 0; 

const unsigned long TEMPO_ESPERA = 500; 

void atualizarTela(); 

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

void manterConexao() {
  if (!client.connected()) {
    if (millis() - ultimoTempoReconnect > 5000) {
      ultimoTempoReconnect = millis();
      String clientId = "Caixa-" + String(MINHA_CAIXA) + "-" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str())) {
        client.subscribe(topico_cerebro_envia);
      }
    }
  } else client.loop();
}

void enviarParaCerebro(String acaoCerebro) {
  JsonDocument doc;
  doc["caixa_origem"] = MINHA_CAIXA;
  doc["acao"] = acaoCerebro; 
  doc["status_produto"] = statusProduto; 
  doc["qtd_atual"] = meusDados.qtd; 
  
  String payload;
  serializeJson(doc, payload);
  client.publish(topico_caixas_enviam, payload.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (int i = 0; i < length; i++) mensagem += (char)payload[i];
  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, mensagem);
  
  if (!erro && doc["caixa_alvo"].is<int>()) {
    int caixa_alvo = doc["caixa_alvo"];
    String comando = doc["comando"].as<String>();

    if (caixa_alvo == MINHA_CAIXA) {
      if (comando == "TELA_FINAL") {
        if (estadoAtual != TELA_FINAL && estadoAtual != TELA_MENSAGEM_ODOO) {
          estadoAtual = TELA_FINAL;
          atualizarTela();
        }
        return;
      }
      if (comando == "PRODUTO") {
        String novoCodigo = doc["codigo"].as<String>();
        
        // CORREÇÃO: A caixa só recusa o comando do Cérebro se ELA JÁ ESTIVER com a tela ativamente aberta operando o produto!
        bool telaAtiva = (estadoAtual == TELA_INICIAL || estadoAtual == TELA_ESTOQUE || estadoAtual == TELA_STATUS || estadoAtual == TELA_ERRO_MOTIVO || estadoAtual == TELA_ESPERA_CONFIRMACAO || estadoAtual == TELA_ESPERA_PROXIMO || estadoAtual == TELA_ESPERA_ANTERIOR);
        
        if (meusDados.codigo == novoCodigo && telaAtiva) {
          enviarParaCerebro("Recebido"); 
          return; 
        }
        
        // Se ela estava na TELA_FINAL ou AGUARDANDO, ela aceita o produto e limpa a tela!
        meusDados.indice = doc["indice"].as<int>();
        meusDados.indice_max = doc["indice_max"].as<int>(); 
        meusDados.codigo = novoCodigo;
        meusDados.nome = doc["nome"].as<String>();
        meusDados.qtd = doc["qtd"].as<int>();
        statusProduto = doc["status_produto"].as<String>();
        
        estadoAtual = TELA_INICIAL;
        atualizarTela();
        enviarParaCerebro("Recebido"); 
      }
    } else {
      estadoAtual = TELA_AGUARDANDO;
      atualizarTela();
    }
  }
}

void desenharBotao(int x, int y, int w, int h, String texto, uint16_t corBase) {
  tft.fillRect(x, y, w, h, corBase);
  tft.drawRect(x, y, w, h, ILI9341_WHITE);
  tft.setCursor(x + 10, y + (h / 2) - 6);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print(texto);
}

void atualizarTela() {
  if (estadoAtual == TELA_AGUARDANDO) {
    tft.fillScreen(ILI9341_BLACK);
    tft.drawRect(10, 10, 300, 220, ILI9341_DARKGREY);
    tft.setCursor(95, 110); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.println("AGUARDANDO");
    return;
  }
  if (estadoAtual == TELA_MENSAGEM_ODOO) {
    tft.fillScreen(ILI9341_BLACK);
    tft.drawRect(10, 10, 300, 220, ILI9341_GREEN);
    tft.setCursor(50, 90); tft.setTextColor(ILI9341_GREEN); tft.setTextSize(2); tft.println("Pedido Finalizado!");
    tft.setCursor(50, 130); tft.setTextColor(ILI9341_WHITE); tft.println("Enviado para o Odoo.");
    return;
  }
  if (estadoAtual == TELA_FINAL) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(70, 20); tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(3); tft.println("FIM DA LISTA");
    desenharBotao(10, 135, 145, 45, "1- Voltar", ILI9341_ORANGE);
    desenharBotao(165, 135, 145, 45, "2- Enviar Odoo", ILI9341_GREEN);
    return;
  }

  tft.fillRect(0, 0, 320, 75, ILI9341_BLACK);
  tft.setCursor(5, 5); tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2); tft.print(meusDados.codigo);
  tft.setCursor(150, 5); tft.setTextColor(ILI9341_WHITE); tft.print(meusDados.nome);
  tft.setCursor(5, 30); tft.print("Qtd: "); tft.print(meusDados.qtd);
  
  tft.setCursor(150, 30); 
  if (statusProduto == "Concluido") tft.setTextColor(ILI9341_GREEN);
  else if (statusProduto == "Pendente") tft.setTextColor(ILI9341_CYAN);
  else tft.setTextColor(ILI9341_RED); 
  tft.print("St: "); tft.print(statusProduto);

  tft.fillRect(0, 75, 320, 165, ILI9341_BLACK);

  if (estadoAtual == TELA_ESPERA_CONFIRMACAO || estadoAtual == TELA_ESPERA_PROXIMO || estadoAtual == TELA_ESPERA_ANTERIOR) {
    tft.setCursor(40, 120); tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);
    
    if (estadoAtual == TELA_ESPERA_CONFIRMACAO) tft.println("     Salvando...");
    else if (estadoAtual == TELA_ESPERA_PROXIMO) tft.println("Pulando para proximo...");
    else if (estadoAtual == TELA_ESPERA_ANTERIOR) tft.println("Voltando pedido...");
    
    tft.setCursor(80, 160); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1); tft.println("Toque p/ CANCELAR");
  } 
  else if (estadoAtual == TELA_INICIAL) {
    desenharBotao(10, 80, 145, 45, "1- Estoque", ILI9341_BLUE);
    desenharBotao(165, 80, 145, 45, "2- Status", ILI9341_BLUE);
    
    uint16_t corVoltar = (meusDados.indice == 0) ? ILI9341_DARKGREY : ILI9341_MAROON;
    uint16_t corProx = (meusDados.indice >= meusDados.indice_max) ? ILI9341_DARKGREY : ILI9341_MAROON;
    
    desenharBotao(10, 135, 145, 45, "3- Voltar", corVoltar);
    desenharBotao(165, 135, 145, 45, "4- Proximo", corProx);
    desenharBotao(10, 190, 300, 40, "5- Concluido", ILI9341_GREEN);
  } 
  else if (estadoAtual == TELA_ESTOQUE) {
    desenharBotao(10, 80, 145, 45, "1- Aumentar", ILI9341_BLUE);
    desenharBotao(165, 80, 145, 45, "2- Diminuir", ILI9341_BLUE);
    desenharBotao(10, 190, 300, 40, "Voltar Menu", ILI9341_DARKGREY);
  } 
  else if (estadoAtual == TELA_STATUS) {
    desenharBotao(10, 80, 145, 45, "1- Erro", ILI9341_RED);
    desenharBotao(165, 80, 145, 45, "2- Pendente", ILI9341_CYAN);
    desenharBotao(10, 190, 300, 40, "Voltar Menu", ILI9341_DARKGREY);
  } 
  else if (estadoAtual == TELA_ERRO_MOTIVO) {
    desenharBotao(10, 80, 145, 45, "Falta Prod", ILI9341_RED);
    desenharBotao(165, 80, 145, 45, "Prod Errado", ILI9341_RED);
    desenharBotao(10, 190, 300, 40, "Voltar Menu", ILI9341_DARKGREY);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  tft.begin();
  tft.setRotation(1); 
  ts.begin(40);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  atualizarTela();
}

void loop() {
  manterConexao();

  if (millis() - tempoInicioBuffer >= TEMPO_ESPERA) {
    if (estadoAtual == TELA_ESPERA_CONFIRMACAO && statusProduto != "Pendente") {
      String acaoCerebro = (statusProduto == "Concluido") ? "Concluido" : "Erro";
      enviarParaCerebro(acaoCerebro);
      estadoAtual = TELA_AGUARDANDO; atualizarTela();
    }
    else if (estadoAtual == TELA_ESPERA_PROXIMO) {
      enviarParaCerebro("Navegar_Proximo");
      estadoAtual = TELA_AGUARDANDO; atualizarTela();
    }
    else if (estadoAtual == TELA_ESPERA_ANTERIOR) {
      enviarParaCerebro("Navegar_Anterior");
      estadoAtual = TELA_AGUARDANDO; atualizarTela();
    }
    else if (estadoAtual == TELA_MENSAGEM_ODOO) {
      enviarParaCerebro("Odoo");
      estadoAtual = TELA_AGUARDANDO; atualizarTela();
    }
  }

  if (ts.touched() && estadoAtual != TELA_AGUARDANDO && estadoAtual != TELA_MENSAGEM_ODOO) {
    if (millis() - ultimoToque < 300) return; 
    ultimoToque = millis();
    
    TS_Point p = ts.getPoint();
    int tx = p.y;
    int ty = 240 - p.x;

    if (estadoAtual == TELA_ESPERA_CONFIRMACAO || estadoAtual == TELA_ESPERA_PROXIMO || estadoAtual == TELA_ESPERA_ANTERIOR) {
      estadoAtual = TELA_INICIAL; statusProduto = "Pendente"; atualizarTela(); return;
    }

    bool btnTopEsq = (tx > 10 && tx < 155 && ty > 80 && ty < 125);
    bool btnTopDir = (tx > 165 && tx < 310 && ty > 80 && ty < 125);
    bool btnBotEsq = (tx > 10 && tx < 155 && ty > 135 && ty < 180);
    bool btnBotDir = (tx > 165 && tx < 310 && ty > 135 && ty < 180);
    bool btnRodape = (tx > 10 && tx < 310 && ty > 190 && ty < 235);

    if (btnTopEsq || btnTopDir || btnBotEsq || btnBotDir || btnRodape) {
      if (estadoAtual == TELA_FINAL) {
        if (btnBotEsq) { enviarParaCerebro("Voltar"); estadoAtual = TELA_AGUARDANDO; }
        if (btnBotDir) { estadoAtual = TELA_MENSAGEM_ODOO; tempoInicioBuffer = millis(); }
      }
      else if (estadoAtual == TELA_INICIAL) {
        if (btnTopEsq) estadoAtual = TELA_ESTOQUE;
        if (btnTopDir) estadoAtual = TELA_STATUS;
        
        if (btnBotEsq) { 
          if (meusDados.indice > 0) {
             estadoAtual = TELA_ESPERA_ANTERIOR; tempoInicioBuffer = millis();
          }
        }
        if (btnBotDir) { 
          if (meusDados.indice < meusDados.indice_max) {
             estadoAtual = TELA_ESPERA_PROXIMO; tempoInicioBuffer = millis();
          }
        }
        
        if (btnRodape) { statusProduto = "Concluido"; estadoAtual = TELA_ESPERA_CONFIRMACAO; tempoInicioBuffer = millis(); }
      } 
      else if (estadoAtual == TELA_ESTOQUE) {
        if (btnTopEsq) meusDados.qtd++;
        if (btnTopDir && meusDados.qtd > 0) meusDados.qtd--;
        if (btnRodape) estadoAtual = TELA_INICIAL;
      }
      else if (estadoAtual == TELA_STATUS) {
        if (btnTopEsq) estadoAtual = TELA_ERRO_MOTIVO;
        if (btnTopDir) { statusProduto = "Pendente"; estadoAtual = TELA_INICIAL; }
        if (btnRodape) estadoAtual = TELA_INICIAL;
      }
      else if (estadoAtual == TELA_ERRO_MOTIVO) {
        if (btnTopEsq) { statusProduto = "Falta Prod"; estadoAtual = TELA_ESPERA_CONFIRMACAO; tempoInicioBuffer = millis(); }
        if (btnTopDir) { statusProduto = "Prod Errado"; estadoAtual = TELA_ESPERA_CONFIRMACAO; tempoInicioBuffer = millis(); }
        if (btnRodape) estadoAtual = TELA_STATUS;
      }
      atualizarTela(); 
    }
  }
}