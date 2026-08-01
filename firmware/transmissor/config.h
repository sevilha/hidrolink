/*
 * ============================================================
 *  HIDROLINK - CONFIGURAÇÕES GLOBAIS DO SISTEMA
 * ============================================================
 */
#ifndef CONFIG_H
#define CONFIG_H

// ------------------------------------------------------------
//  1. Configurações da Caixa D'Água / Reservatório
// ------------------------------------------------------------
#define TANQUE_ALTURA_CM 150 // Altura total útil (fundo da caixa até o sensor)
#define TANQUE_DIST_MIN_CM                                                     \
  10 // Distância quando a caixa está CHEIA (folga cega do sensor)
#define TANQUE_VOLUME_LITROS 1000 // Capacidade total em litros

// ------------------------------------------------------------
//  2. Parâmetros de Automação da Bomba (Histerese)
// ------------------------------------------------------------
#define NIVEL_LIGAR_PCT 25.0    // Liga a bomba quando o nível for <= 25%
#define NIVEL_DESLIGAR_PCT 95.0 // Desliga a bomba quando o nível for >= 95%

// ------------------------------------------------------------
//  3. Mecanismos de Proteção e Segurança (Safety Engine)
// ------------------------------------------------------------
#define TIMEOUT_LORA_DESLIGA_MS                                                \
  45000UL // Se não receber pacote por 45s -> Desliga bomba (proteção estanque)
#define TEMPO_MAX_BOMBA_LIGADA_MIN                                             \
  30 // Tempo máximo (minutos) da bomba ligada continuamente (proteção contra
     // poço seco / transbordo)
#define TEMPO_MIN_PAUSA_BOMBA_MS                                               \
  60000UL // Pausa mínima de 1 minuto entre religamentos (anti-cycling do motor)

// ------------------------------------------------------------
//  4. Frequência e Comunicação LoRa (915MHz - Brasil)
// ------------------------------------------------------------
#define LORA_FREQUENCIA 915E6
#define LORA_SYNC_WORD 0xA5 // Identificador da rede (deve ser igual no TX e RX)
#define INTERVALO_ENVIO_TX_MS                                                  \
  10000 // Intervalo de medição e envio no Transmissor (10s)

// ------------------------------------------------------------
//  5. Pinagem Hardware - LILYGO TTGO LoRa32 (V1/V2)
// ------------------------------------------------------------
// Pinos do LoRa (SPI)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23 // Na T3 v1.6.1 o LoRa Reset é no 23
#define LORA_DIO0 26

// Pinos do OLED (I2C)
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST -1 // A T3 v1.6.1 NÃO possui pino de reset no OLED.

// Pinos do Sensor Ultrassônico e Bateria (No Transmissor)
#define PINO_TRIG 25
#define PINO_ECHO 33
#define PINO_BAT_ADC 35

// Pinos de Acionamento da Bomba (No Receptor)
#define PINO_RELE_BOMBA 17 // Saída para controle da bomba (Relé / Contatora)
#define RELE_LOGICA_INVERSA                                                    \
  false // Setar 'true' se o módulo relé acionar em LOW (lógica invertida comum
        // em módulos optoacoplados)

// ------------------------------------------------------------
//  6. Conectividade Wi-Fi e MQTT (Transmissor/Gateway)
// ------------------------------------------------------------

#define MQTT_TOPIC_TELEMETRY "hidrolink/telemetria"
#define MQTT_CLIENT_ID "Hidrolink_Transmissor"
#include "secrets.h"

#endif // CONFIG_H
