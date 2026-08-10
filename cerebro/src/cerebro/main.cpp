#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#define PINO_LED 8
#define NUM_LEDS 8
CRGB leds[NUM_LEDS];

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// Tópicos com as Caixas
const char* topico_cerebro_envia = "tg/rodrigo/cerebro/tx";
const char* topico_caixas_enviam = "tg/rodrigo/caixas/tx";

// Tópicos com o Python (Odoo)
const char* topico_odoo_tx = "tg/rodrigo/odoo/tx";
const char* topico_odoo_rx = "tg/rodrigo/odoo/rx";

WiFiClient espClient;
PubSubClient client(espClient);

struct Produto { int id_produto; int caixa_alvo; String codigo; String nome; int qtd_original; int qtd_final; String status_final; };
Produto listaPedidos[10]; 
int totalProdutos = 0;

int indiceAtual = 0;
unsigned long ultimoTempoEnvio = 0;
unsigned long ultimoTempoReconnect = 0;
unsigned long tempoCorFinal = 0;

const unsigned long INTERVALO_REENVIO = 500; 

enum EstadoCerebro { ESPERANDO_ODOO, ENVIANDO, AGUARDANDO_RECEBIMENTO, AGUARDANDO_CONCLUSAO, MOSTRANDO_COR, ESPERANDO_ACAO_FINAL, FINALIZADO };
EstadoCerebro estadoAtual = ESPERANDO_ODOO;

void setup_wifi() {
  Serial.println("\nCerebro conectando ao Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWi-Fi Conectado!");
}

void manterConexao() {
  if (!client.connected()) {
    if (millis() - ultimoTempoReconnect > 5000) {
      ultimoTempoReconnect = millis();
      String clientId = "Cerebro-" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str())) {
        client.subscribe(topico_caixas_enviam);
        client.subscribe(topico_odoo_rx); 
        
        if (estadoAtual == ESPERANDO_ODOO) {
          Serial.println("[CEREBRO] Solicitando lista oficial ao Odoo...");
          client.publish(topico_odoo_tx, "{\"comando\": \"PEDIR_LISTA\"}");
        }
      }
    }
  } else client.loop();
}

void atualizarLeds(int qtd, CRGB cor) {
  FastLED.clear();
  for(int i = 0; i < qtd && i < NUM_LEDS; i++) leds[i] = cor;
  FastLED.show();
}

void enviarProximoProduto() {
  JsonDocument doc;
  doc["comando"] = "PRODUTO";
  doc["indice"] = indiceAtual;
  doc["indice_max"] = totalProdutos - 1; 
  doc["caixa_alvo"] = listaPedidos[indiceAtual].caixa_alvo;
  doc["codigo"] = listaPedidos[indiceAtual].codigo;
  doc["nome"] = listaPedidos[indiceAtual].nome;
  doc["qtd"] = listaPedidos[indiceAtual].qtd_final;
  doc["status_produto"] = listaPedidos[indiceAtual].status_final;

  String payload;
  serializeJson(doc, payload);
  client.publish(topico_cerebro_envia, payload.c_str());
  
  Serial.println("\n[CEREBRO TX] -> Caixa " + String(listaPedidos[indiceAtual].caixa_alvo) + " (" + doc["codigo"].as<String>() + ") - Tentativa enviada!");
  atualizarLeds(listaPedidos[indiceAtual].caixa_alvo, CRGB::Blue);
  
  ultimoTempoEnvio = millis();
  estadoAtual = AGUARDANDO_RECEBIMENTO; 
}

void enviarComandoTelaFinal() {
  JsonDocument doc;
  doc["comando"] = "TELA_FINAL";
  doc["caixa_alvo"] = listaPedidos[totalProdutos - 1].caixa_alvo; 
  String payload;
  serializeJson(doc, payload);
  client.publish(topico_cerebro_envia, payload.c_str());
  ultimoTempoEnvio = millis();
}

void enviarPlanilhaOdoo() {
  Serial.println("\n[CEREBRO] Devolvendo resultados para o Python atualizar o Odoo...");
  JsonDocument doc;
  doc["comando"] = "SALVAR_ODOO";
  JsonArray lista = doc["produtos"].to<JsonArray>();
  
  for (int i = 0; i < totalProdutos; i++) {
    JsonObject prd = lista.add<JsonObject>();
    prd["codigo"] = listaPedidos[i].codigo;
    prd["qtd_separada"] = listaPedidos[i].qtd_final;
    prd["status"] = listaPedidos[i].status_final;
  }
  String payload;
  serializeJson(doc, payload);
  client.publish(topico_odoo_tx, payload.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (int i = 0; i < length; i++) mensagem += (char)payload[i];
  
  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, mensagem);
  
  if (String(topic) == topico_odoo_rx && doc["comando"] == "NOVA_LISTA") {
    JsonArray arrayProdutos = doc["produtos"].as<JsonArray>();
    totalProdutos = arrayProdutos.size();
    
    if (totalProdutos > 0) {
      Serial.println("\n[CEREBRO RX] <- Recebeu " + String(totalProdutos) + " produtos do Odoo!");
      for (int i = 0; i < totalProdutos; i++) {
        listaPedidos[i].id_produto = arrayProdutos[i]["id_produto"];
        listaPedidos[i].caixa_alvo = arrayProdutos[i]["caixa_alvo"];
        listaPedidos[i].codigo = arrayProdutos[i]["codigo"].as<String>();
        listaPedidos[i].nome = arrayProdutos[i]["nome"].as<String>();
        listaPedidos[i].qtd_original = arrayProdutos[i]["qtd_original"];
        
        listaPedidos[i].qtd_final = arrayProdutos[i]["qtd_original"];
        listaPedidos[i].status_final = "Pendente";
      }
      indiceAtual = 0;
      estadoAtual = ENVIANDO; 
    }
    return;
  }

  if (String(topic) == topico_caixas_enviam && doc["acao"].is<String>()) {
    int caixaQueRespondeu = doc["caixa_origem"];
    String acao = doc["acao"].as<String>();
    String statusProduto = doc["status_produto"].as<String>();
    int qtdAtual = doc["qtd_atual"].as<int>();
    
    if (caixaQueRespondeu == listaPedidos[indiceAtual].caixa_alvo && acao != "Recebido" && acao != "Odoo" && acao != "Voltar") {
        listaPedidos[indiceAtual].qtd_final = qtdAtual;
        listaPedidos[indiceAtual].status_final = statusProduto;
    }

    if (acao == "Navegar_Anterior" && indiceAtual > 0) {
      indiceAtual--; estadoAtual = ENVIANDO; return;
    }
    if (acao == "Navegar_Proximo" && indiceAtual < totalProdutos - 1) {
      indiceAtual++; estadoAtual = ENVIANDO; return;
    }

    if (caixaQueRespondeu == listaPedidos[indiceAtual].caixa_alvo) {
      if (acao == "Recebido" && estadoAtual == AGUARDANDO_RECEBIMENTO) {
        Serial.println("[CEREBRO RX] <- Caixa confirmou o recebimento!");
        estadoAtual = AGUARDANDO_CONCLUSAO; 
      }
      else if ((acao == "Concluido" || acao == "Erro") && estadoAtual == AGUARDANDO_CONCLUSAO) {
        CRGB corStatus = (acao == "Concluido") ? CRGB::Green : CRGB::Red;
        atualizarLeds(listaPedidos[indiceAtual].caixa_alvo, corStatus);
        
      
        if (indiceAtual < totalProdutos - 1) {
            tempoCorFinal = millis();
            estadoAtual = MOSTRANDO_COR; 
        } else {
            Serial.println("[CEREBRO] Ultimo produto finalizado! Chamando tela final...");
            enviarComandoTelaFinal();
            estadoAtual = ESPERANDO_ACAO_FINAL;
        }
      }
    }
    
    if (estadoAtual == ESPERANDO_ACAO_FINAL && caixaQueRespondeu == listaPedidos[totalProdutos - 1].caixa_alvo) {
      if (acao == "Voltar") {
        indiceAtual = totalProdutos - 1; 
        estadoAtual = ENVIANDO;
      }
      else if (acao == "Odoo") {
        enviarPlanilhaOdoo();
        estadoAtual = FINALIZADO;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<NEOPIXEL, PINO_LED>(leds, NUM_LEDS);
  FastLED.clear(); FastLED.show();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(1024); 
}

void loop() {
  manterConexao();

  if (estadoAtual == ESPERANDO_ODOO) {
    fill_solid(leds, NUM_LEDS, (millis() % 1000 < 500) ? CRGB::Purple : CRGB::Black);
    FastLED.show();
  }
  else if (estadoAtual == ENVIANDO) {
    enviarProximoProduto();
  } 
  else if (estadoAtual == AGUARDANDO_RECEBIMENTO) {
    if (millis() - ultimoTempoEnvio > INTERVALO_REENVIO) enviarProximoProduto();
  }
  else if (estadoAtual == MOSTRANDO_COR) {
    if (millis() - tempoCorFinal > 1000) {
      if(indiceAtual < totalProdutos - 1) {
        indiceAtual++; 
        estadoAtual = ENVIANDO;
      }
    }
  }
  else if (estadoAtual == ESPERANDO_ACAO_FINAL) {
    if (millis() - ultimoTempoEnvio > INTERVALO_REENVIO) enviarComandoTelaFinal();
  }
  else if (estadoAtual == FINALIZADO) {
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
  }
}