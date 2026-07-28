# Hidrolink — Central IoT, Monitoramento de Nível e Automação via LoRa/Wi-Fi

O **Hidrolink** é um sistema embarcado avançado de automação para controle inteligente de bombas de água e monitoramento do nível de reservatórios. Originalmente baseado em rádio LoRa ponto-a-ponto, o sistema agora atua como uma **Central IoT** completa, utilizando um Gateway híbrido (LoRa+Wi-Fi) e um Servidor Local (Orange Pi) para telemetria em tempo real, mantendo total independência de nuvens públicas.

---

## 🚀 Destaques do Sistema

- 📡 **Gateway Híbrido (LoRa + Wi-Fi)**: O transmissor na caixa d'água mede o nível, comanda a bomba remotamente via LoRa (operação offline garantida) e, simultaneamente, envia telemetria via Wi-Fi/MQTT para a infraestrutura da rede local.
- 🔒 **Edge Computing e Privacidade**: Infraestrutura 100% local rodando em um Orange Pi (Mosquitto MQTT e PostgreSQL). Sem exposição de portas para a internet, mantendo a rede totalmente segura ("air-gapped").
- 📱 **Web Dashboard Premium**: Interface React moderna e animada, servida localmente para monitorar o sistema em tempo real por qualquer celular ou tablet.
- 🔔 **Notificações Push (Opcionais)**: Capacidade da API local disparar webhooks de saída (ex: para o ntfy.sh ou Telegram) alertando o celular sobre falhas críticas (caixa vazia, vazamento), desde que o roteador permita tráfego de saída. Em modo totalmente offline, o sistema falha o envio do webhook de forma silenciosa e continua operando a bomba normalmente.
- ⚡ **Automação Inteligente por Histerese**: Acionamento automático da bomba quando o nível cai abaixo de 25% e desligamento em 95%.
- 🛡️ **Tripla Proteção da Bomba (Safety Engine)**:
  1. Proteção de Perda de Sinal (Offline): Desliga a bomba se o transmissor ficar > 45s sem enviar pacotes.
  2. Proteção contra Poço Seco / Vazamento: Limite de tempo máximo contínuo da bomba ligada (30 min).
  3. Proteção Anti-Cycling: Pausa mínima de 1 minuto entre acionamentos do motor para preservar a elétrica.
- 📺 **Displays OLED 0.96" Integrados**: Exibição local do volume em litros, porcentagem e diagnósticos de rádio/Wi-Fi nas placas.

---

## 🛠️ Lista de Materiais

| Item | Quantidade | Função / Localização |
|---|---|---|
| **Orange Pi** (Zero, 3B ou superior) | 1 unidade | Servidor Local / Edge Computing (Roda Docker, Broker MQTT, API NestJS e Banco de Dados) |
| Placa LILYGO TTGO LoRa32 **T3 V1.6.1** | 2 unidades | 1 Transmissor Gateway (Caixa d'água) / 1 Receptor (Casa de Máquinas/Bomba) |
| Sensor ultrassônico waterproof **JSN-SR04T** | 1 unidade | Transmissor (Medição ultrassônica da altura da lâmina d'água) |
| Módulo Relé Optoacoplado 5V ou Contatora | 1 unidade | Receptor (Acionamento elétrico de potência da bomba) |
| Fonte 5V / 2A (USB) | 3 unidades | Alimentação individual para as duas placas ESP32 e para o Orange Pi |

---

## 🔌 Esquemas de Ligação

### 1. Transmissor Gateway (Caixa D'Água)
| JSN-SR04T | TTGO LoRa32 |
|---|---|
| **VCC** / **GND** | 5V / GND |
| **TRIG** / **ECHO** | GPIO 25 / GPIO 33 |

### 2. Receptor (Casa / Casa de Máquinas)
| Módulo Relé / Contatora | TTGO LoRa32 |
|---|---|
| **IN / Signal** | **GPIO 17** |

> ⚠️ **IMPORTANTE DE SEGURANÇA ELÉTRICA:** Recomendamos fortemente utilizar o pino GPIO 17 do ESP32 para acionar um módulo relé optoacoplado, que por sua vez energiza a bobina (A1/A2) de uma **Contatora AC**. Isso garante isolamento elétrico total da placa contra surtos indutivos do motor.

---

## 🐳 Infraestrutura do Servidor Local (Orange Pi)

O sistema exige que o servidor local esteja rodando para processar e armazenar as mensagens MQTT publicadas pela caixa d'água. 
O projeto conta com um ambiente Docker pré-configurado na pasta `server/`.

1. Instale o Docker e o Docker Compose no seu Orange Pi.
2. Acesse a pasta da infraestrutura e suba os serviços:
   ```bash
   cd server
   docker compose up -d
   ```
Isso iniciará o broker **Eclipse Mosquitto** (permitindo conexão anônima na porta **1883** para o ESP32 e na porta **9001** via WebSockets para o Web Dashboard).

---

## 💻 Web Dashboard (Painel Supervisório)

O sistema conta com um Painel Web (construído em Vite + React + Tailwind v4) que conecta diretamente ao WebSocket do MQTT em tempo real. O Painel agora roda nativamente dentro do Docker pelo Nginx.

Para iniciar tudo em produção:
1. Basta executar `docker compose up -d --build` na pasta `server/`.
2. Abra o navegador apontando para o IP da máquina na porta 5173 (ex: `http://192.168.1.100:5173`). O painel permitirá monitorar o nível de água com um gráfico animado e **forçar comandos manuais** para ligar ou desligar a bomba via rota `MQTT -> LoRa`.

---

## ⚙️ Configuração e Calibração (`config.h`)

As variáveis críticas de calibração do reservatório e credenciais de rede ficam nos arquivos `config.h`. 

**Para o Transmissor (`firmware/transmissor/config.h`)**, você deve configurar o IP do servidor local e a rede IoT:
```cpp
#define WIFI_SSID           "Sua_Rede_IoT"
#define WIFI_PASSWORD       "Sua_Senha_IoT"
#define MQTT_SERVER         "192.168.1.100"  // IP Estático do Orange Pi
#define MQTT_PORT           1883
#define MQTT_TOPIC_TELEMETRY "hidrolink/telemetria"
```

---

## 📦 Como Compilar e Gravar (Arduino IDE)

1. **Instale as Bibliotecas**:
   - `LoRa` (por Sandeep Mistry)
   - `Adafruit SSD1306` e `Adafruit GFX Library`
   - `PubSubClient` (por Nick O'Leary) - *Necessário agora para a comunicação MQTT*
   - `WiFi` (Nativa no pacote do ESP32)
2. **Configuração da Placa**:
   - Adicione o pacote `esp32` no Gerenciador de Placas.
   - Selecione a placa **`ESP32 Pico Kit`** (ou `ESP32 Dev Module`).
   - Se for usar o `ESP32 Dev Module`, o **Flash Mode** deve obrigatoriamente estar em **`DIO`**, para evitar Crash Loop.
3. **Gravação**:
   - Faça o upload do código `firmware/transmissor/transmissor.ino` na placa que vai na caixa d'água.
   - Faça o upload do código `firmware/receptor/receptor.ino` na placa que controla a bomba.

---

## ⚠️ Troubleshooting (Solução de Problemas)

1. **Tela OLED desligada e Serial Monitor exibindo "sopa de letras":**
   Sinal clássico de *Crash Loop* de hardware no boot. Verifique os pinos. Na placa **LILYGO T3 V1.6.1**, não há pino de reset no OLED. Certifique-se de que `OLED_RST` está configurado como `-1`. Além disso, o reset do LoRa nessa placa específica é no **Pino 23**.
   
2. **Transmissor não conecta no MQTT / Falhas de envio:**
   O Transmissor e o Orange Pi devem pertencer à mesma rede ou as VLANs devem possuir rotas liberadas na porta 1883. Certifique-se de que o sinal Wi-Fi alcança fisicamente o topo do reservatório onde o transmissor está instalado. O rádio LoRa continuará trabalhando independentemente e de forma prioritária caso o Wi-Fi caia.
