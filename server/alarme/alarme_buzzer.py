#!/usr/bin/env python3
"""
HIDROLINK - ALARME SONORO DO ORANGE PI
Escuta os tópicos MQTT do sistema Hidrolink e aciona um Buzzer ativo em GPIO
quando houver alertas críticos (Boia atuada, Nível estagnado, Timeout ou Erro).
Permite silenciamento imediato via Dashboard ou comando MQTT.
"""

import os
import sys
import time
import json
import threading
import paho.mqtt.client as mqtt

# ------------------------------------------------------------
#  Configurações do Sistema
# ------------------------------------------------------------
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "admin")
MQTT_PASS = os.getenv("MQTT_PASS", "hidrolink_mqtt_pass")

TOPIC_ALERTA = "hidrolink/alerta"
TOPIC_CMD_ALARME = "hidrolink/alarme/comando"
TOPIC_STATUS_ALARME = "hidrolink/alarme/status"

# Pino do GPIO do Buzzer (Número do pino no Orange Pi)
# Exemplo padrão: GPIO 2 / Pin 3 ou conforme configurado
BUZZER_PIN = int(os.getenv("BUZZER_PIN", "2"))
# Lógica do buzzer: True se nível HIGH aciona o buzzer (comum com NPN BC547)
BUZZER_ACTIVE_HIGH = os.getenv("BUZZER_ACTIVE_HIGH", "true").lower() == "true"

# ------------------------------------------------------------
#  Driver Abstrato de GPIO (gpiod / sysfs / mock)
# ------------------------------------------------------------
class BuzzerController:
    def __init__(self, pin: int, active_high: bool = True):
        self.pin = pin
        self.active_high = active_high
        self.driver = "mock"
        self._gpio_line = None
        self._sysfs_value_file = None

        # 1. Tenta inicializar via libgpiod
        try:
            import gpiod
            chip = gpiod.Chip('gpiochip1') # comum no Orange Pi
            self._gpio_line = chip.get_line(self.pin)
            self._gpio_line.request(consumer="hidrolink_buzzer", type=gpiod.LINE_REQ_DIR_OUT)
            self.driver = "libgpiod"
            print(f"[BUZZER] Driver libgpiod inicializado no pino {self.pin}.")
        except Exception as e:
            # 2. Tenta inicializar via Sysfs (/sys/class/gpio)
            try:
                gpio_path = f"/sys/class/gpio/gpio{self.pin}"
                if not os.path.exists(gpio_path):
                    with open("/sys/class/gpio/export", "w") as f:
                        f.write(str(self.pin))
                    time.sleep(0.1)
                with open(f"{gpio_path}/direction", "w") as f:
                    f.write("out")
                self._sysfs_path = f"{gpio_path}/value"
                self.driver = "sysfs"
                print(f"[BUZZER] Driver sysfs inicializado em {self._sysfs_path}.")
            except Exception as e2:
                self.driver = "mock"
                print(f"[BUZZER] Nenhum hardware GPIO detectado. Operando em MODO SIMULADO (Console).")

        self.set_state(False)

    def set_state(self, on: bool):
        level = (on if self.active_high else not on)
        if self.driver == "libgpiod":
            self._gpio_line.set_value(1 if level else 0)
        elif self.driver == "sysfs":
            try:
                with open(self._sysfs_path, "w") as f:
                    f.write("1" if level else "0")
            except Exception:
                pass
        else:
            # Mock mode
            pass

    def cleanup(self):
        self.set_state(False)
        if self.driver == "libgpiod" and self._gpio_line:
            self._gpio_line.release()

buzzer = BuzzerController(BUZZER_PIN, BUZZER_ACTIVE_HIGH)

# ------------------------------------------------------------
#  Estado Global do Alarme
# ------------------------------------------------------------
lock = threading.Lock()
alerta_ativo = False
codigo_alerta = "OK"
silenciado = False
rodando = True

def thread_sirene():
    """Gera o padrão sonoro intermitente quando o alarme estiver ativo e não silenciado."""
    global alerta_ativo, silenciado, rodando
    while rodando:
        with lock:
            deve_tocar = alerta_ativo and (not silenciado)
        
        if deve_tocar:
            # Padrão: 3 bips rápidos e pausa
            for _ in range(3):
                with lock:
                    if not (alerta_ativo and not silenciado):
                        break
                buzzer.set_state(True)
                time.sleep(0.12)
                buzzer.set_state(False)
                time.sleep(0.10)
            
            # Pausa entre sequências
            for _ in range(15):
                with lock:
                    if not (alerta_ativo and not silenciado):
                        break
                time.sleep(0.1)
        else:
            buzzer.set_state(False)
            time.sleep(0.2)

def publicar_status(client):
    with lock:
        payload = {
            "tocando": bool(alerta_ativo and not silenciado),
            "silenciado": bool(silenciado),
            "codigo": codigo_alerta
        }
    try:
        client.publish(TOPIC_STATUS_ALARME, json.dumps(payload), retain=True)
        print(f"[MQTT] Status do alarme publicado: {payload}")
    except Exception as e:
        print(f"[MQTT] Erro ao publicar status: {e}")

# ------------------------------------------------------------
#  Callbacks MQTT
# ------------------------------------------------------------
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Conectado ao broker com codigo {rc}")
    client.subscribe(TOPIC_ALERTA)
    client.subscribe(TOPIC_CMD_ALARME)
    publicar_status(client)

def on_message(client, userdata, msg):
    global alerta_ativo, codigo_alerta, silenciado
    topico = msg.topic
    payload = msg.payload.decode("utf-8", errors="ignore").strip()

    if topico == TOPIC_ALERTA:
        try:
            data = json.loads(payload)
            codigo = data.get("codigo", "OK")
            ativo = bool(data.get("ativo", False))
        except Exception:
            codigo = payload
            ativo = (codigo != "OK" and codigo != "")

        with lock:
            mudou = (codigo != codigo_alerta or ativo != alerta_ativo)
            codigo_alerta = codigo
            alerta_ativo = ativo
            # Se o alerta foi encerrado (OK), desmarca o silenciamento para o próximo
            if not alerta_ativo:
                silenciado = False
            # Se entrou um NOVO alerta e antes estava OK, garante que comece tocando
            elif mudou and alerta_ativo:
                silenciado = False

        print(f"[ALARME] Alerta atualizado: codigo={codigo_alerta}, ativo={alerta_ativo}, silenciado={silenciado}")
        publicar_status(client)

    elif topico == TOPIC_CMD_ALARME:
        cmd = payload.upper()
        if cmd == "SILENCIAR":
            with lock:
                silenciado = True
            buzzer.set_state(False)
            print("[ALARME] Alarme SILENCIADO via comando MQTT/Dashboard!")
            publicar_status(client)
        elif cmd == "TESTAR":
            # Toca um bip de teste
            buzzer.set_state(True)
            time.sleep(0.3)
            buzzer.set_state(False)
            print("[ALARME] Teste sonoro efetuado com sucesso.")

def main():
    t = threading.Thread(target=thread_sirene, daemon=True)
    t.start()

    client = mqtt.Client(client_id="hidrolink_alarme_opi")
    if MQTT_USER and MQTT_PASS:
        client.username_pw_set(MQTT_USER, MQTT_PASS)

    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[INICIO] Conectando ao broker MQTT em {MQTT_BROKER}:{MQTT_PORT}...")
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
    except Exception as e:
        print(f"[ERRO] Falha ao conectar no broker MQTT: {e}")
        sys.exit(1)

    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[ENCERRAMENTO] Finalizando servico de alarme...")
    finally:
        global rodando
        rodando = False
        buzzer.cleanup()

if __name__ == "__main__":
    main()
