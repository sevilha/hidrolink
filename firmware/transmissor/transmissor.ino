/*
 * ============================================================
 *  HIDROLINK - TRANSMISSOR (Instalado na Caixa D'Água)
 *  Placa: LILYGO TTGO LoRa32 (SX1276 - 915MHz) com OLED
 *  Sensor: JSN-SR04T (ultrassônico waterproof)
 * ============================================================
 */

#include "config.h"
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
uint32_t contadorPacotes = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastSendTime = 0;

// Estado do Comando Manual recebido via MQTT
// 0 = Auto, 1 = Forçar ON, 2 = Forçar OFF
int modo_manual = 0;

// Callback para mensagens recebidas do MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  if (String(topic) == "hidrolink/comando") {
    if (msg == "AUTO") modo_manual = 0;
    else if (msg == "ON") modo_manual = 1;
    else if (msg == "OFF") modo_manual = 2;
    Serial.println("[MQTT] Comando recebido: " + msg);
  }
}

// ------------------------------------------------------------
//  Leitura filtrada do sensor ultrassônico JSN-SR04T (Mediana de 3)
// ------------------------------------------------------------
float lerDistanciaCM() {
  float leituras[3];
  for (int i = 0; i < 3; i++) {
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);

    long duracao = pulseIn(PINO_ECHO, HIGH, 30000UL); // Timeout de 30ms (~5m)
    leituras[i] = (duracao == 0) ? -1.0 : (duracao * 0.0343) / 2.0;
    delay(60);
  }

  // Ordenação simples das 3 amostras para seleção da mediana
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      if (leituras[j] < leituras[i]) {
        float t = leituras[i];
        leituras[i] = leituras[j];
        leituras[j] = t;
      }
    }
  }

  return leituras[1];
}

// ------------------------------------------------------------
//  Leitura da Tensão da Bateria 18650 (Divisor interno pino 35)
// ------------------------------------------------------------
float lerBateriaV() {
  int leitura = analogRead(PINO_BAT_ADC);
  // Fator de ajuste para divisor de tensão de 100K/100K da placa TTGO
  float tensao = (leitura / 4095.0) * 2.0 * 3.3 * 1.1;
  return tensao;
}

// ------------------------------------------------------------
//  Atualização de Display OLED Local no Transmissor
// ------------------------------------------------------------
void atualizarOLED(float distancia, float nivelPct, float litros, bool erroSensor) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("HIDROLINK - TX");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  if (erroSensor) {
    display.setTextSize(1.2);
    display.setCursor(0, 20);
    display.println("[ERRO]: SENSOR FALHOU!");
  } else {
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.print(nivelPct, 0);
    display.println(" %");

    display.setTextSize(1);
    display.setCursor(0, 38);
    display.print("Dist: ");
    display.print(distancia, 1);
    display.println(" cm");

    display.setCursor(0, 48);
    display.print("Vol: ");
    display.print(litros, 0);
    display.println(" L");
  }

  display.setCursor(0, 56);
  display.print("Pkt #");
  display.print(contadorPacotes);

  display.display();
}

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("[WiFi] Conectando a ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Falha ao conectar (Timeout).");
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  // Tenta reconectar (não bloqueante para não travar o LoRa)
  if (!mqttClient.connected()) {
    Serial.print("[MQTT] Tentando conexao com broker... ");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("Conectado!");
      mqttClient.subscribe("hidrolink/comando"); // Assina comandos do Dashboard
    } else {
      Serial.print("Falha, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" (Tentará novamente no próximo loop)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);

  // Inicialização do Display OLED
  if (OLED_RST > 0) {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW); delay(20);
    digitalWrite(OLED_RST, HIGH);
  }
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  // Inicialização do Rádio LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCIA)) {
    Serial.println("[ERRO] Falha ao inicializar modem LoRa!");
    display.setCursor(0, 20);
    display.println("Falha LoRa!");
    display.display();
    while (1) delay(1000);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(9);     // Equilíbrio alcance vs consumo
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(20);            // 20 dBm (Potência Máxima)

  setupWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("[HIDROLINK] - TX (Gateway) pronto.");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    mqttClient.loop();
  }

  unsigned long currentMillis = millis();
  
  if (currentMillis - lastSendTime >= INTERVALO_ENVIO_TX_MS || lastSendTime == 0) {
    lastSendTime = currentMillis;

    float distancia = lerDistanciaCM();
    bool erroSensor = (distancia < 0.0 || distancia > (TANQUE_ALTURA_CM + 50.0));

    float nivelPct = 0.0;
    float litros = 0.0;

    if (!erroSensor) {
      float alturaUtil = TANQUE_ALTURA_CM - TANQUE_DIST_MIN_CM;
      float nivelAgua = (TANQUE_ALTURA_CM - distancia);
      nivelPct = constrain((nivelAgua / alturaUtil) * 100.0, 0.0, 100.0);
      litros = (nivelPct / 100.0) * TANQUE_VOLUME_LITROS;
    }

    float bateria = lerBateriaV();
    contadorPacotes++;

    // Formato do pacote telemetry CSV: "distancia,nivel,litros,bateria,contador,erro,modo_manual"
    String pacote = String(distancia, 1) + "," +
                    String(nivelPct, 1) + "," +
                    String(litros, 0) + "," +
                    String(bateria, 2) + "," +
                    String(contadorPacotes) + "," +
                    String(erroSensor ? 1 : 0) + "," +
                    String(modo_manual);

    // Envio via LoRa (Para o Receptor na bomba)
    LoRa.beginPacket();
    LoRa.print(pacote);
    LoRa.endPacket();
    Serial.println("[TX Envio #" + String(contadorPacotes) + "]: " + pacote);

    // Envio via MQTT (Para o Orange Pi NestJS) se conectado
    if (mqttClient.connected()) {
      // Para o MQTT, enviaremos um JSON estruturado
      String payloadMQTT = "{\"distancia\":" + String(distancia, 1) + 
                           ",\"nivel\":" + String(nivelPct, 1) + 
                           ",\"litros\":" + String(litros, 0) + 
                           ",\"bateria\":" + String(bateria, 2) + 
                           ",\"erro\":" + String(erroSensor ? "true" : "false") + "}";
                           
      if (mqttClient.publish(MQTT_TOPIC_TELEMETRY, payloadMQTT.c_str())) {
        Serial.println("[MQTT] Telemetria publicada.");
      } else {
        Serial.println("[MQTT] Falha ao publicar telemetria.");
      }
    }

    atualizarOLED(distancia, nivelPct, litros, erroSensor);
  }
}
