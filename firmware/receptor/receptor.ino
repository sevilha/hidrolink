/*
 * ============================================================
 *  HIDROLINK - RECEPTOR E CONTROLADOR DE BOMBA D'ÁGUA
 *  Placa: LILYGO TTGO LoRa32 (SX1276 - 915MHz) com OLED
 *  Atuador: Módulo Relé / Contatora no PINO_RELE_BOMBA
 * ============================================================
 */

#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

// Função Auxiliar de Descriptografia
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

// ------------------------------------------------------------
//  Estados da Máquina de Controle da Bomba
// ------------------------------------------------------------
enum EstadoBomba {
  BOMBA_DESLIGADA,
  BOMBA_LIGADA,
  ALERTA_OFFLINE,
  ALERTA_ERRO_SENSOR,
  ALERTA_POCO_SECO,
  PAUSA_ANTI_CICLO
};

EstadoBomba g_estadoBomba = BOMBA_DESLIGADA;

// Dados de Telemetria Recebidos do Transmissor
float g_distancia = 0.0;
float g_nivelPct = 0.0;
float g_litros = 0.0;
float g_bateria = 0.0;
uint32_t g_contador = 0;
bool g_erroSensor = false;
int g_modoManual = 0;
int g_rssi = 0;
float g_snr = 0.0;

// Temporizadores de Controle e Segurança
unsigned long g_ultimoPacoteMs = 0;
unsigned long g_inicioBombaLigadaMs = 0;
unsigned long g_ultimoDesligamentoMs = 0;
bool g_primeiroPacote = false;
bool g_bombaRelatadaOn = false;

// ------------------------------------------------------------
//  Acionamento Físico do Relé com suporte a Lógica Invertida
// ------------------------------------------------------------
void acionarRele(bool ligar) {
  g_bombaRelatadaOn = ligar;
  bool sinalPino = RELE_LOGICA_INVERSA ? !ligar : ligar;
  digitalWrite(PINO_RELE_BOMBA, sinalPino ? HIGH : LOW);
}

// ------------------------------------------------------------
//  Desenho da Barra Visual do Nível d'Água no OLED
// ------------------------------------------------------------
void desenharBarraNivel(float pct) {
  int x = 100, y = 14, w = 22, h = 46;
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int alturaPreench = (int)((pct / 100.0) * (h - 4));
  alturaPreench = constrain(alturaPreench, 0, h - 4);

  if (alturaPreench > 0) {
    display.fillRect(x + 2, y + (h - 2) - alturaPreench, w - 4, alturaPreench,
                     SSD1306_WHITE);
  }
}

// ------------------------------------------------------------
//  Atualização da Tela OLED do Receptor
// ------------------------------------------------------------
void atualizarTela(bool online) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Cabeçalho
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HIDROLINK ");
  display.println(online ? "[ON]" : "[OFF]");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  if (!g_primeiroPacote) {
    display.setCursor(0, 25);
    display.println("Aguardando conexao");
    display.println("com transmissor...");
    display.display();
    return;
  }

  // Linha de Status da Bomba
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print("Bomba: ");

  switch (g_estadoBomba) {
  case BOMBA_LIGADA:
    display.println("[ LIGADA ]");
    break;
  case BOMBA_DESLIGADA:
    display.println("[ DESLIGADA ]");
    break;
  case PAUSA_ANTI_CICLO:
    display.println("[ PAUSA ]");
    break;
  case ALERTA_OFFLINE:
    display.println("[! OFFLINE !]");
    break;
  case ALERTA_ERRO_SENSOR:
    display.println("[! ERR SENSOR !]");
    break;
  case ALERTA_POCO_SECO:
    display.println("[! TIMEOUT !]");
    break;
  }

  // Nível e Volume
  display.setTextSize(2);
  display.setCursor(0, 26);
  display.print(g_nivelPct, 0);
  display.print("%");

  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("Vol: ");
  display.print(g_litros, 0);
  display.println(" L");

  // Rodapé: Sinal RSSI e Bateria do Transmissor
  display.setCursor(0, 56);
  display.print("RSSI:");
  display.print(g_rssi);
  display.print(" Bat:");
  display.print(g_bateria, 1);
  display.print("V");

  desenharBarraNivel(g_nivelPct);
  display.display();
}

// ------------------------------------------------------------
//  Processamento do Pacote Telemetria Recebido via LoRa
// ------------------------------------------------------------
void processarPacote(String pacoteBase64) {
  String pacote = decryptAES(pacoteBase64);
  if (pacote == "") {
      Serial.println("[SEGURANÇA] Pacote rejeitado! (Falha na descriptografia / Chave incorreta)");
      return;
  }

  // Formato retornado pelo TX: "distancia,nivel,litros,bateria,contador,erro,modo_manual"
  int idx[6];
  int pos = 0, campo = 0;
  for (int i = 0; i < pacote.length() && campo < 6; i++) {
    if (pacote[i] == ',') {
      idx[campo++] = i;
    }
  }
  if (campo < 6)
    return; // Pacote malformado

  uint32_t novo_contador = pacote.substring(idx[3] + 1, idx[4]).toInt();

  // 1. CHECAGEM DE SEGURANÇA ANTI-REPLAY
  if (g_primeiroPacote && novo_contador <= g_contador) {
      Serial.printf("[SEGURANÇA] Pacote rejeitado! Contador antigo (%d <= %d). Possível ataque Replay!\n", novo_contador, g_contador);
      return;
  }
  g_contador = novo_contador;

  g_distancia = pacote.substring(0, idx[0]).toFloat();
  g_nivelPct = pacote.substring(idx[0] + 1, idx[1]).toFloat();
  g_litros = pacote.substring(idx[1] + 1, idx[2]).toFloat();
  g_bateria = pacote.substring(idx[2] + 1, idx[3]).toFloat();
  g_erroSensor = pacote.substring(idx[4] + 1, idx[5]).toInt() == 1;
  g_modoManual = pacote.substring(idx[5] + 1).toInt();

  g_rssi = LoRa.packetRssi();
  g_snr = LoRa.packetSnr();
  g_ultimoPacoteMs = millis();
  g_primeiroPacote = true;

  Serial.printf(
      "[RX Recebido]: Dist=%.1fcm Nivel=%.1f%% Vol=%.0fL Bat=%.2fV RSSI=%d\n",
      g_distancia, g_nivelPct, g_litros, g_bateria, g_rssi);
}

// ------------------------------------------------------------
//  Lógica de Automação e Máquina de Estados da Bomba
// ------------------------------------------------------------
void executarControleBomba() {
  if (!g_primeiroPacote)
    return;

  unsigned long agora = millis();
  bool semSinalLoRa = (agora - g_ultimoPacoteMs > TIMEOUT_LORA_DESLIGA_MS);

  // 1. CHECAGEM DE SEGURANÇA SUPERIOR (Perda de Sinal LoRa)
  if (semSinalLoRa) {
    if (g_bombaRelatadaOn) {
      acionarRele(false);
      g_ultimoDesligamentoMs = agora;
      Serial.println(
          "[SEGURANÇA] Perda de comunicação LoRa (>45s). Bomba DESLIGADA!");
    }
    g_estadoBomba = ALERTA_OFFLINE;
    return;
  }

  // 2. CHECAGEM DE SEGURANÇA DE ERRO DE SENSOR
  if (g_erroSensor) {
    if (g_bombaRelatadaOn) {
      acionarRele(false);
      g_ultimoDesligamentoMs = agora;
      Serial.println(
          "[SEGURANÇA] Erro no sensor ultrassônico. Bomba DESLIGADA!");
    }
    g_estadoBomba = ALERTA_ERRO_SENSOR;
    return;
  }

  // 3. MODO MANUAL ABSOLUTO (Sobrescreve histerese e tempo máximo)
  if (g_modoManual == 1) { // FORÇAR ON
    if (g_estadoBomba != BOMBA_LIGADA) {
      acionarRele(true);
      g_inicioBombaLigadaMs = agora; // para referência
      g_estadoBomba = BOMBA_LIGADA;
      Serial.println("[MANUAL] Bomba LIGADA forcadamente via Dashboard!");
    }
    return; // Sai da função, ignora o resto
  } 
  else if (g_modoManual == 2) { // FORÇAR OFF
    if (g_estadoBomba != BOMBA_DESLIGADA) {
      acionarRele(false);
      g_ultimoDesligamentoMs = agora;
      g_estadoBomba = BOMBA_DESLIGADA;
      Serial.println("[MANUAL] Bomba DESLIGADA forcadamente via Dashboard!");
    }
    return; // Sai da função
  }

  // 4. CHECAGEM DE SEGURANÇA DE TEMPO MÁXIMO (Proteção contra Poço Seco /
  // Vazamento)
  if (g_estadoBomba == BOMBA_LIGADA) {
    unsigned long tempoExecucaoMin = (agora - g_inicioBombaLigadaMs) / 60000UL;
    if (tempoExecucaoMin >= TEMPO_MAX_BOMBA_LIGADA_MIN) {
      acionarRele(false);
      g_ultimoDesligamentoMs = agora;
      g_estadoBomba = ALERTA_POCO_SECO;
      Serial.println("[ALERTA] Tempo máximo de acionamento excedido (30min). "
                     "Bomba desligada por segurança!");
      return;
    }
  }

  // Se esteve em estado de erro mas o sinal e sensor se restabeleceram, sai do
  // alerta
  if (g_estadoBomba == ALERTA_OFFLINE || g_estadoBomba == ALERTA_ERRO_SENSOR) {
    g_estadoBomba = BOMBA_DESLIGADA;
  }

  // 4. LÓGICA DE HISTERESE (NÍVEL MÍNIMO / MÁXIMO)

  // A. CONDIÇÃO PARA LIGAR (Nível <= NIVEL_LIGAR_PCT)
  if (g_nivelPct <= NIVEL_LIGAR_PCT) {
    if (g_estadoBomba == BOMBA_DESLIGADA) {
      // Verifica tempo de pausa anti-cycling para proteger o motor
      if (agora - g_ultimoDesligamentoMs >= TEMPO_MIN_PAUSA_BOMBA_MS) {
        acionarRele(true);
        g_inicioBombaLigadaMs = agora;
        g_estadoBomba = BOMBA_LIGADA;
        Serial.printf(
            "[AUTOMAÇÃO] Nível baixo (%.1f%% <= %.1f%%). Bomba LIGADA!\n",
            g_nivelPct, NIVEL_LIGAR_PCT);
      } else {
        g_estadoBomba = PAUSA_ANTI_CICLO;
      }
    } else if (g_estadoBomba == PAUSA_ANTI_CICLO) {
      if (agora - g_ultimoDesligamentoMs >= TEMPO_MIN_PAUSA_BOMBA_MS) {
        acionarRele(true);
        g_inicioBombaLigadaMs = agora;
        g_estadoBomba = BOMBA_LIGADA;
        Serial.println("[AUTOMAÇÃO] Fim da pausa anti-ciclo. Bomba LIGADA!");
      }
    }
  }

  // B. CONDIÇÃO PARA DESLIGAR (Nível >= NIVEL_DESLIGAR_PCT)
  if (g_nivelPct >= NIVEL_DESLIGAR_PCT) {
    if (g_bombaRelatadaOn || g_estadoBomba == BOMBA_LIGADA) {
      acionarRele(false);
      g_ultimoDesligamentoMs = agora;
      g_estadoBomba = BOMBA_DESLIGADA;
      Serial.printf(
          "[AUTOMAÇÃO] Nível atingido (%.1f%% >= %.1f%%). Bomba DESLIGADA!\n",
          g_nivelPct, NIVEL_DESLIGAR_PCT);
    } else if (g_estadoBomba != BOMBA_DESLIGADA &&
               g_estadoBomba != ALERTA_POCO_SECO) {
      g_estadoBomba = BOMBA_DESLIGADA;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Inicializa Pino do Relé
  pinMode(PINO_RELE_BOMBA, OUTPUT);
  acionarRele(false); // Garante bomba desligada ao ligar o ESP32

  // Inicialização do Display OLED (Igual ao Transmissor)
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
    display.println("Iniciando Receptor...");
    display.display();
  }

  // Inicialização do Rádio LoRa (Igual ao Transmissor)
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
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);

  Serial.println("[HIDROLINK RX] Receptor e Controlador de Bomba Pronto.");
}

void loop() {
  // Leitura de pacotes LoRa recebidos
  int tamanhoPacote = LoRa.parsePacket();
  bool recebeuPacote = false;
  if (tamanhoPacote) {
    String pacote = "";
    while (LoRa.available()) {
      pacote += (char)LoRa.read();
    }
    processarPacote(pacote);
    recebeuPacote = true;
  }

  // Executa máquina de automação da bomba
  executarControleBomba();

  // Atualiza tela OLED a cada 2 segundos OU se recebeu um pacote novo
  static unsigned long ultimaAtualizacaoTela = 0;
  if (recebeuPacote || millis() - ultimaAtualizacaoTela >= 2000) {
    ultimaAtualizacaoTela = millis();
    bool online = g_primeiroPacote &&
                  (millis() - g_ultimoPacoteMs < TIMEOUT_LORA_DESLIGA_MS);
    atualizarTela(online);
  }

  delay(200);
}
