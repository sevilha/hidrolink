/*
 * ============================================================
 *  HIDROLINK - TRANSMISSOR (Instalado na Caixa D'Água)
 *  Placa: LILYGO TTGO LoRa32 (SX1276 - 915MHz) com OLED
 *  Sensor: JSN-SR04T (ultrassônico waterproof)
 * ============================================================
 */

#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRa.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

// Função Auxiliar de Criptografia
String encryptAES(String plaintext) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char*)LORA_AES_KEY, 128);

    int len = plaintext.length();
    int pad = 16 - (len % 16);
    int padded_len = len + pad;
    unsigned char input[padded_len];
    memcpy(input, plaintext.c_str(), len);
    for (int i = len; i < padded_len; i++) input[i] = (unsigned char)pad;

    unsigned char iv[16];
    for (int i = 0; i < 16; i++) iv[i] = random(0, 256);
    unsigned char iv_copy[16];
    memcpy(iv_copy, iv, 16);

    unsigned char output[padded_len];
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv, input, output);
    mbedtls_aes_free(&aes);

    int final_len = 16 + padded_len;
    unsigned char final_output[final_len];
    memcpy(final_output, iv_copy, 16);
    memcpy(final_output + 16, output, padded_len);

    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, final_output, final_len);
    unsigned char base64_out[olen];
    mbedtls_base64_encode(base64_out, olen, &olen, final_output, final_len);

    return String((char*)base64_out);
}

// Função Auxiliar de Descriptografia (Para receber o ACK)
String decryptAES(String base64_input) {
    size_t len = base64_input.length();
    unsigned char decode_buf[len];
    size_t decode_len = 0;
    
    int err = mbedtls_base64_decode(decode_buf, len, &decode_len, (const unsigned char*)base64_input.c_str(), len);
    if (err != 0 || decode_len < 16) return "";

    unsigned char iv[16];
    memcpy(iv, decode_buf, 16);
    
    size_t cipher_len = decode_len - 16;
    if (cipher_len % 16 != 0 || cipher_len == 0) return "";
    
    unsigned char cipher_buf[cipher_len];
    memcpy(cipher_buf, decode_buf + 16, cipher_len);
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char*)LORA_AES_KEY, 128);
    
    unsigned char output[cipher_len];
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher_len, iv, cipher_buf, output);
    mbedtls_aes_free(&aes);
    
    int pad = output[cipher_len - 1];
    if (pad > 0 && pad <= 16) {
        cipher_len -= pad;
    } else {
        return ""; // Padding inválido
    }
    
    char final_str[cipher_len + 1];
    memcpy(final_str, output, cipher_len);
    final_str[cipher_len] = '\0';
    
    return String(final_str);
}

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
uint32_t contadorPacotes = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastSendTime = 0;

// Estado do Comando Manual recebido via MQTT
// 0 = Auto, 1 = Forçar ON, 2 = Forçar OFF
int modo_manual = 0;

// RESET de alertas travados (boia / nível estagnado / timeout).
// Fica pendente e é reenviado em todo pacote até o RX confirmar com ACK.
bool resetPendente = false;

// Boia eletrônica (estado já filtrado por debounce)
bool boiaAcionada = false;
bool boiaLeituraAnterior = false;
unsigned long boiaMudancaMs = 0;
bool forcarEnvio = false; // Envia imediatamente quando a boia muda de estado

// Estado reportado pelo Receptor no ACK ("ACK,<bomba>,<alerta>")
bool bombaLigadaRX = false;
String alertaAtual = "OK";  // OK | BOIA | ESTAGNADO | TIMEOUT | SENSOR
String alertaPublicado = ""; // Último alerta publicado no MQTT

// Callback para mensagens recebidas do MQTT
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (String(topic) == "hidrolink/comando") {
    if (msg == "AUTO")
      modo_manual = 0;
    else if (msg == "ON")
      modo_manual = 1;
    else if (msg == "OFF")
      modo_manual = 2;
    else if (msg == "RESET") {
      resetPendente = true;
      forcarEnvio = true;
    }
    Serial.println("[MQTT] Comando recebido: " + msg);
  }
}

// ------------------------------------------------------------
//  Boia Eletrônica de Fallback (leitura + debounce não bloqueante)
// ------------------------------------------------------------
bool lerBoiaBruta() {
#if BOIA_HABILITADA
  int v = digitalRead(PINO_BOIA);
  return BOIA_ACIONADA_EM_HIGH ? (v == HIGH) : (v == LOW);
#else
  return false;
#endif
}

void atualizarBoia() {
  bool leitura = lerBoiaBruta();
  if (leitura != boiaLeituraAnterior) {
    boiaLeituraAnterior = leitura;
    boiaMudancaMs = millis();
  } else if (leitura != boiaAcionada &&
             millis() - boiaMudancaMs >= BOIA_DEBOUNCE_MS) {
    boiaAcionada = leitura;
    forcarEnvio = true; // Avisa o RX sem esperar o próximo ciclo de 10s
    Serial.println(boiaAcionada ? "[BOIA] ACIONADA (nivel maximo fisico)!"
                                : "[BOIA] Normalizada.");
  }
}

// ------------------------------------------------------------
//  Publicação do alerta (retido) para o alarme sonoro no Orange Pi
// ------------------------------------------------------------
void publicarAlertaSeMudou(float nivelPct) {
  if (!mqttClient.connected() || alertaAtual == alertaPublicado)
    return;

  bool ativo = (alertaAtual != "OK");
  String payload = "{\"codigo\":\"" + alertaAtual + "\",\"ativo\":" +
                   String(ativo ? "true" : "false") +
                   ",\"nivel\":" + String(nivelPct, 1) + ",\"boia\":" +
                   String(boiaAcionada ? "true" : "false") + "}";

  if (mqttClient.publish(MQTT_TOPIC_ALERTA, payload.c_str(), true)) {
    alertaPublicado = alertaAtual;
    Serial.println("[MQTT] Alerta publicado: " + payload);
  }
}

// ------------------------------------------------------------
//  Leitura filtrada do sensor ultrassônico JSN-SR04T (Mediana de 3)
// ------------------------------------------------------------
float lerDistanciaCM() {
#if MODO_MOCK // Forçando simulação do nível da água
  // Simula o nível da água enchendo e esvaziando
  static float mockDist = TANQUE_ALTURA_CM; // Começa vazia
  static float step = -5.0; // Desce 5cm por leitura (caixa enchendo)

  mockDist += step;
  if (mockDist <= TANQUE_DIST_MIN_CM) {
    mockDist = TANQUE_DIST_MIN_CM;
    step = 2.0; // Começa a esvaziar mais devagar
  } else if (mockDist >= TANQUE_ALTURA_CM) {
    mockDist = TANQUE_ALTURA_CM;
    step = -5.0; // Começa a encher
  }
  return mockDist;
#else
  float leituras[3];
  for (int i = 0; i < 3; i++) {
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);

    long duracao = pulseIn(PINO_ECHO, HIGH, 30000UL); // Timeout de 30ms (~5m)
    leituras[i] = (duracao == 0) ? -1.0 : (duracao * 0.0343) / 2.0;
    
    Serial.print("[Sensor] Leitura "); Serial.print(i);
    Serial.print(": duracao="); Serial.print(duracao);
    Serial.print(" us -> dist="); Serial.print(leituras[i]); Serial.println(" cm");
    
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
#endif
}

// ------------------------------------------------------------
//  Leitura da Tensão da Bateria 18650 (Divisor interno pino 35)
// ------------------------------------------------------------
float lerBateriaV() {
#if 0 // Usando leitura real da bateria
  // Bateria fixa em 4.0V
  return 4.0;
#else
  int leitura = analogRead(PINO_BAT_ADC);
  // Fator de ajuste para divisor de tensão de 100K/100K da placa TTGO
  float tensao = (leitura / 4095.0) * 2.0 * 3.3 * 1.1;
  return tensao;
#endif
}

// ------------------------------------------------------------
//  Atualização de Display OLED Local no Transmissor
// ------------------------------------------------------------
void atualizarOLED(float distancia, float nivelPct, float litros,
                   bool erroSensor) {
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
  if (alertaAtual != "OK") {
    display.print("ALERTA: ");
    display.print(alertaAtual);
  } else {
    display.print("Pkt #");
    display.print(contadorPacotes);
    if (boiaAcionada)
      display.print(" BOIA!");
  }

  display.display();
}

void atualizarOLEDStatusLoRa(bool loraOffline) {
  if (loraOffline) {
    display.fillRect(0, 48, 128, 16, SSD1306_BLACK); // Apaga a parte inferior
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.println("[ERRO] LORA OFFLINE");
    display.setCursor(0, 56);
    display.println("Receptor nao responde");
    display.display();
  }
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
  if (WiFi.status() != WL_CONNECTED)
    return;

  // Tenta reconectar (não bloqueante para não travar o LoRa)
  if (!mqttClient.connected()) {
    Serial.print("[MQTT] Tentando conexao com broker... ");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
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

#if BOIA_HABILITADA
  pinMode(PINO_BOIA, INPUT_PULLUP);
  boiaAcionada = boiaLeituraAnterior = lerBoiaBruta();
  Serial.println(boiaAcionada ? "[BOIA] Iniciou ACIONADA." : "[BOIA] OK.");
#endif

  // Inicialização do Display OLED
  if (OLED_RST > 0) {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
  }
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERRO] Falha ao inicializar o Display OLED SSD1306!");
  } else {
    Serial.println("[OLED] Display OLED iniciado com sucesso.");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Iniciando Gateway...");
    display.display();
  }

  // Inicialização do Rádio LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCIA)) {
    Serial.println("[ERRO] Falha ao inicializar modem LoRa!");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Falha LoRa!");
    display.display();
    while (1) {
      Serial.println("[LORA] Travado: Modem LoRa nao respondeu. Verifique pinos SPI/RST/CS.");
      delay(2000);
    }
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(9); // Equilíbrio alcance vs consumo
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(20); // 20 dBm (Potência Máxima)

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

  atualizarBoia();

  unsigned long currentMillis = millis();

  if (forcarEnvio || currentMillis - lastSendTime >= INTERVALO_ENVIO_TX_MS ||
      lastSendTime == 0) {
    lastSendTime = currentMillis;
    forcarEnvio = false;

    float distancia = lerDistanciaCM();
    bool erroSensor =
        (distancia < 0.0 || distancia > (TANQUE_ALTURA_CM + 50.0));

    float nivelPct = 0.0;
    float litros = 0.0;

    if (!erroSensor) {
      float alturaUtil = TANQUE_ALTURA_CM - TANQUE_DIST_MIN_CM;
      float nivelAgua = (TANQUE_ALTURA_CM - distancia);
      nivelPct = constrain((nivelAgua / alturaUtil) * 100.0, 0.0, 100.0);
      litros = (nivelPct / 100.0) * TANQUE_VOLUME_LITROS;
    }

    if (boiaAcionada && (erroSensor || nivelPct < NIVEL_DESLIGAR_PCT)) {
      Serial.printf("[BOIA] Divergencia: boia acionada com ultrassonico em "
                    "%.1f%% (< %.1f%%)\n",
                    nivelPct, NIVEL_DESLIGAR_PCT);
    }

    float bateria = lerBateriaV();
    contadorPacotes++;
    bool enviouReset = resetPendente;

    // Formato do pacote telemetry CSV:
    // "distancia,nivel,litros,bateria,contador,erro,modo_manual,boia,reset"
    String pacote = String(distancia, 1) + "," + String(nivelPct, 1) + "," +
                    String(litros, 0) + "," + String(bateria, 2) + "," +
                    String(contadorPacotes) + "," + String(erroSensor ? 1 : 0) +
                    "," + String(modo_manual) + "," +
                    String(boiaAcionada ? 1 : 0) + "," +
                    String(enviouReset ? 1 : 0);

    // Envio via LoRa (Para o Receptor na bomba)
    String pacoteCriptografado = encryptAES(pacote);
    LoRa.beginPacket();
    LoRa.print(pacoteCriptografado);
    LoRa.endPacket();
    Serial.println("[TX Envio #" + String(contadorPacotes) + "]: " + pacote);
    Serial.println(" -> Criptografado: " + pacoteCriptografado);

    // Espera pelo ACK ("ACK,<bomba 0/1>,<alerta>")
    unsigned long tempoEspera = millis();
    bool loraOffline = true;
    while (millis() - tempoEspera < 2000) {
      int packetSize = LoRa.parsePacket();
      if (packetSize) {
        String msg = "";
        while (LoRa.available()) {
          msg += (char)LoRa.read();
        }
        String dec = decryptAES(msg);
        if (dec.startsWith("ACK")) {
          loraOffline = false;
          int c1 = dec.indexOf(',');
          int c2 = dec.indexOf(',', c1 + 1);
          if (c1 > 0 && c2 > c1) {
            bombaLigadaRX = dec.substring(c1 + 1, c2).toInt() == 1;
            alertaAtual = dec.substring(c2 + 1);
          }
          if (enviouReset)
            resetPendente = false; // RX processou o RESET antes de responder
          Serial.println("[TX] ACK recebido do RX: " + dec);
          break;
        }
      }
      delay(10);
    }

    if (loraOffline) {
      Serial.println("[TX ERRO] ACK não recebido. Receptor offline ou fora de alcance.");
    }

    // Envio via MQTT (Para o Orange Pi NestJS) se conectado
    if (mqttClient.connected()) {
      // Para o MQTT, enviaremos um JSON estruturado
      String payloadMQTT = "{\"distancia\":" + String(distancia, 1) +
                           ",\"nivel\":" + String(nivelPct, 1) +
                           ",\"litros\":" + String(litros, 0) +
                           ",\"bateria\":" + String(bateria, 2) + ",\"erro\":" +
                           String(erroSensor ? "true" : "false") + ",\"lora_offline\":" +
                           String(loraOffline ? "true" : "false") +
                           ",\"boia\":" + String(boiaAcionada ? "true" : "false") +
                           ",\"bomba\":" + String(bombaLigadaRX ? "true" : "false") +
                           ",\"alerta\":\"" + alertaAtual + "\"}";

      if (mqttClient.publish(MQTT_TOPIC_TELEMETRY, payloadMQTT.c_str())) {
        Serial.println("[MQTT] Telemetria publicada.");
      } else {
        Serial.println("[MQTT] Falha ao publicar telemetria.");
      }
    }
    publicarAlertaSeMudou(nivelPct);

    atualizarOLED(distancia, nivelPct, litros, erroSensor);
    if (loraOffline) {
      atualizarOLEDStatusLoRa(true);
    }
  }
}
