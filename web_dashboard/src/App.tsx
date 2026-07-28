import { useEffect, useState } from 'react';
import mqtt from 'mqtt';
import { Droplets, Battery, Activity, ShieldAlert, Waves, Power } from 'lucide-react';

// Interfaces for our state
interface Telemetry {
  distancia: number;
  nivel: number;
  litros: number;
  bateria: number;
  erro: boolean;
}

function App() {
  const [telemetry, setTelemetry] = useState<Telemetry>({
    distancia: 0,
    nivel: 0,
    litros: 0,
    bateria: 0.0,
    erro: false
  });
  
  const [client, setClient] = useState<mqtt.MqttClient | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [manualMode, setManualMode] = useState<number>(0); // 0=Auto, 1=ON, 2=OFF

  // Conexão MQTT via WebSocket
  useEffect(() => {
    // Pega o IP atual pelo qual a aplicação está sendo acessada no navegador
    const brokerUrl = `ws://${window.location.hostname}:9001`;
    const mqttClient = mqtt.connect(brokerUrl, {
      clientId: `dashboard_${Math.random().toString(16).slice(3)}`
    });

    mqttClient.on('connect', () => {
      console.log('Conectado ao MQTT (WebSocket)');
      setIsConnected(true);
      mqttClient.subscribe('hidrolink/telemetria');
    });

    mqttClient.on('message', (topic, message) => {
      if (topic === 'hidrolink/telemetria') {
        try {
          const data = JSON.parse(message.toString());
          setTelemetry({
            distancia: data.distancia || 0,
            nivel: data.nivel || 0,
            litros: data.litros || 0,
            bateria: data.bateria || 0,
            erro: data.erro === 'true' || data.erro === true
          });
        } catch (e) {
          console.error('Erro ao fazer parse da telemetria', e);
        }
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
    }
  };

  // Cores dinâmicas para o nível de água
  const getWaterColor = (nivel: number) => {
    if (nivel <= 25) return 'from-red-500 to-rose-600';
    if (nivel <= 50) return 'from-yellow-400 to-orange-500';
    return 'from-cyan-400 to-blue-600';
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col items-center py-10 px-4 font-sans selection:bg-blue-500/30">
      
      {/* Header */}
      <header className="mb-12 text-center">
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

      <main className="w-full max-w-5xl grid grid-cols-1 lg:grid-cols-2 gap-8">
        
        {/* Painel do Reservatório */}
        <div className="bg-slate-900/50 backdrop-blur-xl border border-slate-800 rounded-3xl p-8 shadow-2xl flex flex-col items-center relative overflow-hidden group">
          <div className="absolute top-0 inset-x-0 h-px bg-gradient-to-r from-transparent via-blue-500/50 to-transparent"></div>
          
          <h2 className="text-xl font-bold text-slate-200 mb-8 w-full text-center tracking-wide">
            Reservatório Principal
          </h2>

          {/* Tanque Visual */}
          <div className="relative w-48 h-64 md:w-56 md:h-72 bg-slate-800/80 rounded-b-3xl rounded-t-sm border-4 border-slate-700 shadow-inner overflow-hidden mb-8 transition-transform hover:scale-105 duration-500">
            {/* Overlay de gradiente no vidro do tanque */}
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
          <div className="grid grid-cols-2 gap-4 w-full">
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

          {/* Alerta de Erro */}
          {telemetry.erro && (
            <div className="mt-4 w-full bg-red-500/10 border border-red-500/30 rounded-xl p-4 flex items-center gap-3 text-red-400 animate-pulse">
              <ShieldAlert className="w-6 h-6" />
              <span className="font-semibold">Erro detectado no sensor ultrassônico!</span>
            </div>
          )}
        </div>

        {/* Painel de Controle */}
        <div className="flex flex-col gap-6">
          <div className="bg-slate-900/50 backdrop-blur-xl border border-slate-800 rounded-3xl p-8 shadow-2xl relative overflow-hidden">
            <div className="absolute top-0 inset-x-0 h-px bg-gradient-to-r from-transparent via-purple-500/50 to-transparent"></div>
            
            <h2 className="text-xl font-bold text-slate-200 mb-2 flex items-center gap-2">
              <Activity className="w-5 h-5 text-purple-400" />
              Controle da Bomba d'Água
            </h2>
            <p className="text-sm text-slate-400 mb-8">
              O modo de sobrescrita absoluta desabilita os sensores de segurança do poço e nível. Cuidado ao usar.
            </p>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
              <button
                onClick={() => sendCommand(0)}
                className={`flex flex-col items-center justify-center p-6 rounded-2xl border-2 transition-all duration-300 ${
                  manualMode === 0 
                    ? 'bg-blue-600/20 border-blue-500 shadow-[0_0_20px_rgba(59,130,246,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Activity className={`w-8 h-8 mb-3 ${manualMode === 0 ? 'text-blue-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 0 ? 'text-blue-100' : 'text-slate-300'}`}>AUTO</span>
                <span className="text-xs text-slate-500 mt-1 text-center">Histerese (25% - 95%)</span>
              </button>

              <button
                onClick={() => sendCommand(1)}
                className={`flex flex-col items-center justify-center p-6 rounded-2xl border-2 transition-all duration-300 ${
                  manualMode === 1 
                    ? 'bg-emerald-600/20 border-emerald-500 shadow-[0_0_20px_rgba(16,185,129,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Power className={`w-8 h-8 mb-3 ${manualMode === 1 ? 'text-emerald-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 1 ? 'text-emerald-100' : 'text-slate-300'}`}>FORÇAR ON</span>
                <span className="text-xs text-red-400/80 mt-1 text-center font-medium">Ignora Segurança</span>
              </button>

              <button
                onClick={() => sendCommand(2)}
                className={`flex flex-col items-center justify-center p-6 rounded-2xl border-2 transition-all duration-300 ${
                  manualMode === 2 
                    ? 'bg-red-600/20 border-red-500 shadow-[0_0_20px_rgba(239,68,68,0.3)]' 
                    : 'bg-slate-800 border-transparent hover:bg-slate-700'
                }`}
              >
                <Power className={`w-8 h-8 mb-3 ${manualMode === 2 ? 'text-red-400' : 'text-slate-400'}`} />
                <span className={`font-bold ${manualMode === 2 ? 'text-red-100' : 'text-slate-300'}`}>FORÇAR OFF</span>
                <span className="text-xs text-slate-500 mt-1 text-center">Desliga Totalmente</span>
              </button>
            </div>
          </div>
          
          {/* Status System Card */}
          <div className="bg-slate-900/30 border border-slate-800 rounded-3xl p-6">
            <h3 className="text-sm font-semibold text-slate-400 uppercase tracking-wider mb-4">Diagnostic</h3>
            <div className="space-y-3">
              <div className="flex justify-between items-center text-sm">
                <span className="text-slate-500">Distância do Sensor</span>
                <span className="text-slate-300 font-mono">{telemetry.distancia.toFixed(1)} cm</span>
              </div>
              <div className="flex justify-between items-center text-sm">
                <span className="text-slate-500">Modo de Operação Atual</span>
                <span className="text-slate-300 font-mono">
                  {manualMode === 0 ? 'Automático' : manualMode === 1 ? 'Manual LIGADA' : 'Manual DESLIGADA'}
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
