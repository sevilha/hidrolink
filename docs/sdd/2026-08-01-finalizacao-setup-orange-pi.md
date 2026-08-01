# Software Design Document

## Contexto

Após a consolidação do código do Transmissor e do Receptor (ESP32) com proteção de dados sensíveis e lógica de mock, e a configuração da arquitetura base (React buildado + Nginx no Orange Pi), precisamos formalizar os próximos passos definidos no `doc.md` para finalização do setup da infraestrutura e automação.

## Problema

O sistema ainda precisa ser implantado fisicamente e testado de ponta a ponta. Além disso, o checklist (`doc.md`) menciona a utilização do Node-RED para automação, porém ele ainda não faz parte do nosso `docker-compose.yml` atual. Também precisamos garantir a previsibilidade da rede definindo um IP fixo no Orange Pi.

## Solução proposta

1. **Infraestrutura no Orange Pi (IP Fixo):** Especificar os comandos de rede para fixar o IP do Orange Pi em `192.168.1.111` (usualmente via `nmcli` ou reserva de IP no roteador).
2. **Atualização do Docker Compose:** Adicionar o serviço do **Node-RED** ao `docker-compose.yml` existente, configurando os volumes necessários para persistência dos fluxos e garantindo que ele comunique na mesma rede do Mosquitto.
3. **Validação e Testes Manuais:** Descrever os passos práticos para gravar os ESP32, testar a recepção LoRa e realizar um teste de injeção manual MQTT (`hidrolink/comando`) direto no servidor.

## Impacto arquitetural

- **Novos Componentes:** Adição do contêiner `nodered/node-red` rodando na porta 1880.
- **Rede:** O Orange Pi passará a atuar ativamente com um IP estático na rede local, facilitando o acesso ao Dashboard (porta 5173), MQTT (porta 1883) e Node-RED (porta 1880).

## Arquivos afetados

- `server/docker-compose.yml` (Inclusão do Node-RED)
- `doc.md` (Atualização para marcar tarefas como concluídas)

## Riscos

- **Conflito de IP:** Fixar o IP no Orange Pi pode causar conflito se o roteador atribuir o `192.168.1.111` a outro dispositivo (recomendável reservar o IP no roteador via DHCP bind).
- **Consumo de Memória:** O Node-RED adicionará um leve *overhead* de memória no Orange Pi.
- **Permissões de Volume (Node-RED):** Contêineres do Node-RED muitas vezes sofrem com problemas de permissão (`chown 1000:1000`) nas pastas montadas se não configurados corretamente.

## Testes necessários

- Teste E2E da comunicação LoRa: Transmissor → Receptor.
- Teste de Integração MQTT: Receptor → Mosquitto (Orange Pi).
- Teste de Integração Node-RED: Node-RED inscrevendo-se em `hidrolink/telemetria` e reagindo aos dados.

## Plano de rollback

- Se a adição do Node-RED falhar ou consumir muitos recursos, reverter a alteração no `docker-compose.yml` via git.
- Se a configuração de IP estático isolar o Orange Pi da rede, conectar teclado/monitor físico para restaurar para DHCP via `nmtui`.

## Critérios de aceite

- [ ] Node-RED está rodando no `docker-compose.yml` e acessível na porta 1880.
- [ ] Orange Pi fixado no IP `192.168.1.111`.
- [ ] Dispositivos ESP32 gravados com firmware de mock e enviando telemetria.
- [ ] Mensagem de telemetria recebida com sucesso no tópico MQTT do Mosquitto no Orange Pi.

## Notas de IA
- Prompt/requisição original: /specify use o doc.md para especificar os proximos passos, crie as specs para rodarmos depois o que for codigo
