import { useEffect, useState } from 'react';
import mqtt from 'mqtt';
import { 
  Droplets, 
  Battery, 
  Activity, 
  ShieldAlert, 
  Waves, 
  Power, 
  Bell, 
  BellOff, 
  RotateCcw, 
  AlertTriangle,
  CheckCircle2
} from 'lucide-react';

// Interfaces for our state
interface Telemetry {
  distancia: number;
  nivel: number;
  litros: number;
  bateria: number;
  erro: boolean;
  loraOffline: boolean;
  boia: boolean;
  bomba: boolean;
  alerta: string; // "OK" | "BOIA" | "ESTAGNADO" | "TIMEOUT" | "SENSOR" | "OFFLINE"
}

interface AlarmeStatus {
  tocando: boolean;
  silenciado: boolean;
  codigo: string;
}

function App() {
  const [telemetry, setTelemetry] = useState<Telemetry>({
    distancia: 0,
    nivel: 0,
    litros: 0,
    bateria: 0.0,
    erro: false,
    loraOffline: false,
    boia: false,
    bomba: false,
    alerta: 'OK'
  });
  
  const [alarmeStatus, setAlarmeStatus] = useState<AlarmeStatus>({
    tocando: false,
    silenciado: false,
    codigo: 'OK'
  });

  const [client, setClient] = useState<mqtt.MqttClient | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [manualMode, setManualMode] = useState<number>(0); // 0=Auto, 1=ON, 2=OFF
  const [feedbackMsg, setFeedbackMsg] = useState<string | null>(null);

  // Conexão MQTT via WebSocket
  useEffect(() => {
    const brokerUrl = `ws://${window.location.hostname}:9001`;
    const mqttClient = mqtt.connect(brokerUrl, {
      clientId: `dashboard_${Math.random().toString(16).slice(3)}`
    });

    mqttClient.on('connect', () => {
      console.log('Conectado ao MQTT (WebSocket)');
      setIsConnected(true);
      mqttClient.subscribe('hidrolink/telemetria');
      mqttClient.subscribe('hidrolink/alerta');
      mqttClient.subscribe('hidrolink/alarme/status');
    });

    mqttClient.on('message', (topic, message) => {
      try {
        const payloadStr = message.toString();
        if (topic === 'hidrolink/telemetria') {
          const data = JSON.parse(payloadStr);
          setTelemetry({
            distancia: data.distancia ?? 0,
            nivel: data.nivel ?? 0,
            litros: data.litros ?? 0,
            bateria: data.bateria ?? 0,
            erro: data.erro === 'true' || data.erro === true,
            loraOffline: data.lora_offline === 'true' || data.lora_offline === true,
            boia: data.boia === 'true' || data.boia === true,
            bomba: data.bomba === 'true' || data.bomba === true,
            alerta: data.alerta || 'OK'
          });
        } else if (topic === 'hidrolink/alarme/status') {
          const status = JSON.parse(payloadStr);
          setAlarmeStatus({
            tocando: Boolean(status.tocando),
            silenciado: Boolean(status.silenciado),
            codigo: status.codigo || 'OK'
          });
        }
      } catch (e) {
        console.error('Erro ao fazer parse de mensagem MQTT', e);
      }
    });

    mqttClient.on('close', () => setIsConnected(false));
    
    setClient(mqttClient);

    return () => {
      mqttClient.end();
    };
  }, []);

  const sendCommand = (mode: number) => {
    if (client && isConnected) {
      let payload = "AUTO";
      if (mode === 1) payload = "ON";
      if (mode === 2) payload = "OFF";
      
      client.publish('hidrolink/comando', payload);
      setManualMode(mode);
      exibirFeedback(`Comando enviado: ${payload}`);
    }
  };

  const sendReset = () => {
    if (client && isConnected) {
      client.publish('hidrolink/comando', 'RESET');
      exibirFeedback('Comando de RESET enviado! Liberando travas de segurança...');
    }
  };

  const silenciarAlarme = () => {
    if (client && isConnected) {
      client.publish('hidrolink/alarme/comando', 'SILENCIAR');
      exibirFeedback('Alarme sonoro silenciado.');
    }
  };

  const exibirFeedback = (msg: string) => {
    setFeedbackMsg(msg);
    setTimeout(() => setFeedbackMsg(null), 3500);
  };

  // Cores dinâmicas para o nível de água
  const getWaterColor = (nivel: number) => {
    if (nivel <= 25) return 'from-red-500 to-rose-600';
    if (nivel <= 50) return 'from-yellow-400 to-orange-500';
    return 'from-cyan-400 to-blue-600';
  };

  const hasTravaSeguranca = 
    telemetry.alerta === 'BOIA' || 
    telemetry.alerta === 'ESTAGNADO' || 
    telemetry.alerta === 'TIMEOUT';

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col items-center py-8 px-4 font-sans selection:bg-blue-500/30">
      
      {/* Toast Feedback */}
      {feedbackMsg && (
        <div className="fixed top-5 z-50 bg-blue-600 text-white font-medium px-5 py-3 rounded-2xl shadow-2xl flex items-center gap-3 animate-bounce border border-blue-400">
          <Activity className="w-5 h-5" />
          <span>{feedbackMsg}</span>
        </div>
      )}

      {/* Header */}
      <header className="mb-8 text-center">
        <h1 className="text-4xl md:text-5xl font-extrabold tracking-tight bg-gradient-to-r from-blue-400 to-cyan-300 bg-clip-text text-transparent flex items-center justify-center gap-3">
          <Waves className="w-10 h-10 text-blue-400" />
          Hidrolink Central
        </h1>
        <div className="mt-3 flex items-center justify-center gap-2 text-sm text-slate-400 font-medium">
          <span className="relative flex h-3 w-3">
            {isConnected && <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>}
            <span className={`relative inline-flex rounded-full h-3 w-3 ${isConnected ? 'bg-emerald-500' : 'bg-red-500'}`}></span>
          </span>
          {isConnected ? 'Sistema Online (Live)' : 'Reconectando Servidor...'}
        </div>
      </header>

      {/* BANNER DE ALARME SONORO / TRAVA CRÍTICA */}
      {(hasTravaSeguranca || alarmeStatus.tocando) && (
        <div className="w-full max-w-5xl mb-6 bg-red-950/80 border-2 border-red-500/60 rounded-3xl p-6 shadow-2xl backdrop-blur-xl flex flex-col md:flex-row items-center justify-between gap-4 animate-pulse">
          <div className="flex items-center gap-4">
            <div className="p-3 bg-red-600/30 rounded-2xl text-red-400">
              <ShieldAlert className="w-8 h-8" />
            </div>
            <div>
              <h3 className="text-lg font-bold text-red-200">
                {telemetry.alerta === 'BOIA' && 'TRAVA DE SEGURANÇA: Boia de Fallback Acionada!'}
                {telemetry.alerta === 'ESTAGNADO' && 'TRAVA DE SEGURANÇA: Nível Estagnado com Bomba Ligada!'}
                {telemetry.alerta === 'TIMEOUT' && 'TRAVA DE SEGURANÇA: Tempo Máximo de Bomba Excedido (30min)!'}
                {!hasTravaSeguranca && alarmeStatus.tocando && 'ALARME SONORO DISPARADO NO ORANGE PI'}
              </h3>
              <p className="text-sm text-red-300/80">
                {telemetry.alerta === 'BOIA' && 'A água atingiu a boia mecânica no topo antes do sensor ultrassônico indicar cheio. Bomba desligada para evitar transbordo.'}
                {telemetry.alerta === 'ESTAGNADO' && 'A bomba permaneceu ligada por 5 minutos sem elevação do nível d\'água (possível poço seco, desferra ou vazamento).'}
                {telemetry.alerta === 'TIMEOUT' && 'A bomba operou pelo tempo máximo contínuo de 30 minutos e foi desligada por proteção.'}
              </p>
            </div>
          </div>

          <div className="flex items-center gap-3 w-full md:w-auto">
            {alarmeStatus.tocando && (
              <button
                onClick={silenciarAlarme}
                className="flex-1 md:flex-none flex items-center justify-center gap-2 bg-amber-600 hover:bg-amber-500 text-white font-semibold px-4 py-3 rounded-2xl transition shadow-lg cursor-pointer"
              >
                <BellOff className="w-5 h-5" />
                Silenciar Alarme
              </button>
            )}

            {hasTravaSeguranca && (
              <button
                onClick={sendReset}
                className="flex-1 md:flex-none flex items-center justify-center gap-2 bg-red-600 hover:bg-red-500 text-white font-bold px-5 py-3 rounded-2xl transition shadow-lg hover:shadow-red-500/40 cursor-pointer"
              >
                <RotateCcw className="w-5 h-5" />
                Resetar Trava
              </button>
            )}
          </div>
        </div>
      )}

      <main className="w-full max-w-5xl grid grid-cols-1 lg:grid-cols-2 gap-8">
        
        {/* Painel do Reservatório */}
        <div className="bg-slate-900/50 backdrop-blur-xl border border-slate-800 rounded-3xl p-8 shadow-2xl flex flex-col items-center relative overflow-hidden group">
          <div className="absolute top-0 inset-x-0 h-px bg-gradient-to-r from-transparent via-blue-500/50 to-transparent"></div>
          
          <h2 className="text-xl font-bold text-slate-200 mb-6 w-full text-center tracking-wide flex items-center justify-center gap-2">
            Reservatório Principal
          </h2>

          {/* Tanque Visual */}
          <div className="relative w-48 h-64 md:w-56 md:h-72 bg-slate-800/80 rounded-b-3xl rounded-t-sm border-4 border-slate-700 shadow-inner overflow-hidden mb-6 transition-transform hover:scale-105 duration-500">
            <div className="absolute inset-0 bg-gradient-to-b from-white/5 to-transparent z-10 pointer-events-none"></div>
            
            {/* Água Animada */}
            <div 
              className={`absolute bottom-0 w-full bg-gradient-to-t ${getWaterColor(telemetry.nivel)} transition-all duration-1000 ease-out shadow-[0_0_30px_rgba(56,189,248,0.4)]`}
              style={{ height: `${telemetry.nivel}%` }}
            >
              <div className="absolute top-0 w-full h-2 bg-white/30 backdrop-blur-sm"></div>
            </div>

            {/* Texto Centralizado na Água */}
            <div className="absolute inset-0 flex flex-col items-center justify-center z-20 drop-shadow-lg">
              <span className="text-5xl font-black text-white mix-blend-overlay">
                {telemetry.nivel.toFixed(0)}%
              </span>
            </div>
          </div>

          {/* Cards de Métricas (Glassmorphism) */}
          <div className="grid grid-cols-2 gap-4 w-full mb-4">
            <div className="bg-slate-800/60 p-4 rounded-2xl flex items-center gap-4 hover:bg-slate-700/60 transition-colors">
              <div className="bg-blue-500/20 p-3 rounded-xl text-blue-400"><Droplets /></div>
              <div>
                <p className="text-sm text-slate-400 font-medium">Volume</p>
                <p className="text-xl font-bold text-slate-100">{telemetry.litros.toFixed(0)} L</p>
              </div>
            </div>
            
            <div className="bg-slate-800/60 p-4 rounded-2xl flex items-center gap-4 hover:bg-slate-700/60 transition-colors">
              <div className="bg-emerald-500/20 p-3 rounded-xl text-emerald-400"><Battery /></div>
              <div>
                <p className="text-sm text-slate-400 font-medium">Bateria TX</p>
                <p className="text-xl font-bold text-slate-100">{telemetry.bateria.toFixed(1)}V</p>
              </div>
            </div>
          </div>

          {/* Status dos Sensores e Boia */}
          <div className="grid grid-cols-2 gap-4 w-full">
            {/* Status Boia */}
            <div className={`p-4 rounded-2xl border flex items-center gap-3 transition-colors ${
              telemetry.boia 
                ? 'bg-red-500/20 border-red-500 text-red-300 animate-pulse' 
                : 'bg-slate-800/40 border-slate-700/60 text-slate-300'
            }`}>
              <AlertTriangle className={`w-5 h-5 ${telemetry.boia ? 'text-red-400' : 'text-slate-400'}`} />
              <div className="text-left">
                <p className="text-xs text-slate-400 font-medium">Boia Fallback</p>
                <p className="text-sm font-bold">{telemetry.boia ? 'ACIONADA' : 'Normal (Livre)'}</p>
              </div>
            </div>

            {/* Status Estado Bomba */}
            <div className={`p-4 rounded-2xl border flex items-center gap-3 transition-colors ${
              telemetry.bomba 
                ? 'bg-emerald-500/20 border-emerald-500 text-emerald-300' 
                : 'bg-slate-800/40 border-slate-700/60 text-slate-300'
            }`}>
              <Power className={`w-5 h-5 ${telemetry.bomba ? 'text-emerald-400' : 'text-slate-400'}`} />
              <div className="text-left">
                <p className="text-xs text-slate-400 font-medium">Relé da Bomba</p>
                <p className="text-sm font-bold">{telemetry.bomba ? 'LIGADA' : 'DESLIGADA'}</p>
              </div>
            </div>
          </div>

          {/* Alerta de Erro de Sensor */}
          {telemetry.erro && (
            <div className="mt-4 w-full bg-red-500/10 border border-red-500/30 rounded-xl p-4 flex items-center gap-3 text-red-400 animate-pulse">
              <ShieldAlert className="w-6 h-6" />
              <span className="font-semibold text-sm">Erro detectado no sensor ultrassônico!</span>
            </div>
          )}

          {/* Alerta de Lora Offline */}
          {telemetry.loraOffline && (
            <div className="mt-4 w-full bg-amber-500/10 border border-amber-500/30 rounded-xl p-4 flex items-center gap-3 text-amber-400 animate-pulse">
              <ShieldAlert className="w-6 h-6" />
              <span className="font-semibold text-sm">Perda de conexão LoRa com a Bomba!</span>
            </div>
          )}
        </div>

        {/* Painel de Controle e Segurança */}
        <div className="flex flex-col gap-6">
          <div className="bg-slate-900/50 backdrop-blur-xl border border-slate-800 rounded-3xl p-8 shadow-2xl relative overflow-hidden">
            <div className="absolute top-0 inset-x-0 h-px bg-gradient-to-r from-transparent via-purple-500/50 to-transparent"></div>
            
            <h2 className="text-xl font-bold text-slate-200 mb-2 flex items-center gap-2">
              <Activity className="w-5 h-5 text-purple-400" />
              Controle da Bomba d'Água
            </h2>
            <p className="text-sm text-slate-400 mb-6">
              Em modo manual ou automático, as proteções físicas de boia mecânica e nível estagnado desligam e travam a bomba por segurança.
            </p>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
              <button
                onClick={() => sendCommand(0)}
                className={`flex flex-col items-center justify-center p-5 rounded-2xl border-2 transition-all duration-300 cursor-pointer ${
                  manualMode === 0 
                    ? 'bg-blue-600/20 border-blue-500 shadow-[0_0_20px_rgba(59,130,246,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Activity className={`w-7 h-7 mb-2 ${manualMode === 0 ? 'text-blue-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 0 ? 'text-blue-100' : 'text-slate-300'}`}>AUTO</span>
                <span className="text-xs text-slate-500 mt-1 text-center">25% a 95%</span>
              </button>

              <button
                onClick={() => sendCommand(1)}
                className={`flex flex-col items-center justify-center p-5 rounded-2xl border-2 transition-all duration-300 cursor-pointer ${
                  manualMode === 1 
                    ? 'bg-emerald-600/20 border-emerald-500 shadow-[0_0_20px_rgba(16,185,129,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Power className={`w-7 h-7 mb-2 ${manualMode === 1 ? 'text-emerald-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 1 ? 'text-emerald-100' : 'text-slate-300'}`}>FORÇAR ON</span>
                <span className="text-xs text-emerald-400/80 mt-1 text-center font-medium">Liga Manual</span>
              </button>

              <button
                onClick={() => sendCommand(2)}
                className={`flex flex-col items-center justify-center p-5 rounded-2xl border-2 transition-all duration-300 cursor-pointer ${
                  manualMode === 2 
                    ? 'bg-red-600/20 border-red-500 shadow-[0_0_20px_rgba(239,68,68,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Power className={`w-7 h-7 mb-2 ${manualMode === 2 ? 'text-red-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 2 ? 'text-red-100' : 'text-slate-300'}`}>FORÇAR OFF</span>
                <span className="text-xs text-slate-500 mt-1 text-center">Desliga Total</span>
              </button>
            </div>

            {/* Linha de Ações de Proteção e Alarme */}
            <div className="pt-4 border-t border-slate-800/80 flex flex-wrap gap-3">
              <button
                onClick={sendReset}
                className="flex items-center gap-2 bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 text-sm font-semibold px-4 py-2.5 rounded-xl transition cursor-pointer"
                title="Libera qualquer trava de segurança e volta a operar normalmente"
              >
                <RotateCcw className="w-4 h-4 text-amber-400" />
                Resetar Travas
              </button>

              <button
                onClick={silenciarAlarme}
                className="flex items-center gap-2 bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 text-sm font-semibold px-4 py-2.5 rounded-xl transition cursor-pointer"
                title="Silencia o buzzer sonoro do Orange Pi"
              >
                <BellOff className="w-4 h-4 text-cyan-400" />
                Silenciar Buzzer
              </button>
            </div>
          </div>
          
          {/* Status e Diagnóstico do Sistema */}
          <div className="bg-slate-900/30 border border-slate-800 rounded-3xl p-6">
            <h3 className="text-sm font-semibold text-slate-400 uppercase tracking-wider mb-4 flex items-center justify-between">
              <span>Diagnóstico & Segurança</span>
              <span className="text-xs text-slate-500 font-mono">Safety Engine v2</span>
            </h3>
            <div className="space-y-3 text-sm">
              <div className="flex justify-between items-center">
                <span className="text-slate-500">Distância Lida</span>
                <span className="text-slate-300 font-mono">{telemetry.distancia.toFixed(1)} cm</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-slate-500">Modo de Operação</span>
                <span className="text-slate-300 font-mono">
                  {manualMode === 0 ? 'Automático (Histerese)' : manualMode === 1 ? 'Manual LIGADA' : 'Manual DESLIGADA'}
                </span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-slate-500">Código de Alerta RX</span>
                <span className={`font-mono font-bold ${telemetry.alerta === 'OK' ? 'text-emerald-400' : 'text-red-400'}`}>
                  {telemetry.alerta}
                </span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-slate-500">Alarme Sonoro (Orange Pi)</span>
                <span className="text-slate-300 font-mono flex items-center gap-1.5">
                  {alarmeStatus.tocando ? (
                    <span className="text-red-400 font-bold flex items-center gap-1">
                      <Bell className="w-3.5 h-3.5 animate-bounce" /> Tocando
                    </span>
                  ) : alarmeStatus.silenciado ? (
                    <span className="text-amber-400 flex items-center gap-1">
                      <BellOff className="w-3.5 h-3.5" /> Silenciado
                    </span>
                  ) : (
                    <span className="text-emerald-400 flex items-center gap-1">
                      <CheckCircle2 className="w-3.5 h-3.5" /> Normal
                    </span>
                  )}
                </span>
              </div>
            </div>
          </div>
        </div>

      </main>
    </div>
  );
}

export default App;
