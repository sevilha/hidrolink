# Módulo de Alarme Sonoro no Orange Pi (Buzzer Ativo)

Este módulo roda como um daemon no Orange Pi e dispara um **Buzzer Ativo de 5V** sempre que uma condição crítica de segurança ocorrer no reservatório (Boia atuada, Nível estagnado, Timeout ou Erro).

---

## 🔌 Esquema Elétrico (Conexão do Buzzer ao Orange Pi)

Os pinos de GPIO do Orange Pi fornecem 3.3V com limite de corrente baixa (~10-15mA). Portanto, para acionar um buzzer sonoro potente (5V), utiliza-se um **transistor NPN** como chaveador (driver).

### Componentes:
- **1x Buzzer Ativo 5V** (contínuo)
- **1x Transistor NPN** (BC547, BC337 ou 2N2222)
- **1x Resistor de 1kΩ** (1/4W)
- **1x Diodo 1N4148 ou 1N4007** (opcional, proteção flyback se o buzzer for eletromagnético)

### Diagrama de Ligação:

```text
       +5V (Pino 2 ou 4 do Orange Pi)
         │
         ├───[+] Buzzer Ativo (5V)
         │       [-]
         │        │
         │        ├──[Diodo Catodo / se usado]───┐
         │        │                              │ (Anodo ao Coletor)
         │     Coletor                           │
         │       ┌┴┐                             │
         │  Base │ │ Transistor NPN (BC547)      │
GPIO X ──[1kΩ]───┤ │                             │
(Pino 3 / PA12)  └┬┘                             │
               Emissor                           │
                  │                              │
                 GND (Pino 6 do Orange Pi) ──────┴
```

1. **Orange Pi 5V** -> Pino Positivo (+) do Buzzer.
2. **Pino Negativo (-)** do Buzzer -> Coletor (C) do Transistor NPN.
3. **GPIO Escolhido (ex: GPIO 2)** -> Resistor de 1kΩ -> Base (B) do Transistor.
4. **Emissor (E) do Transistor** -> GND do Orange Pi.

---

## 🚀 Instalação e Execução

### 1. Instalar Dependências
No Orange Pi:
```bash
sudo apt update
sudo apt install -y python3 python3-pip gpiod python3-libgpiod
pip3 install paho-mqtt
```

### 2. Testar Manualmente
```bash
python3 alarme_buzzer.py
```

### 3. Instalar como Serviço do Sistema (Systemd)
```bash
# Copie o arquivo de serviço para o systemd
sudo cp hidrolink-alarme.service /etc/systemd/system/

# Recarregue os daemons e ative para iniciar no boot
sudo systemctl daemon-reload
sudo systemctl enable hidrolink-alarme
sudo systemctl start hidrolink-alarme

# Verifique o status
sudo systemctl status hidrolink-alarme
```

---

## 📡 Tópicos MQTT Utilizados

| Tópico | Direção | Descrição |
|---|---|---|
| `hidrolink/alerta` | Subscrição | Recebe JSON com status do alerta: `{"codigo":"BOIA","ativo":true}` |
| `hidrolink/alarme/comando` | Subscrição | Recebe `SILENCIAR` ou `TESTAR` |
| `hidrolink/alarme/status` | Publicação (Retido) | Publica `{"tocando": true/false, "silenciado": true/false, "codigo": "..."}` |
