import {readFileSync,mkdirSync,writeFileSync,existsSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
class Element {
  constructor(){this.textContent='';this.children=[];this.dataset={};this.classList={add(){},remove(){}};}
  append(...items){this.children.push(...items);}
  prepend(item){this.children.unshift(item);}
  replaceChildren(){this.children=[];}
  get lastElementChild(){return {remove:()=>this.children.pop()};}
  addEventListener(){}
  getBoundingClientRect(){return {width:760,height:260};}
  getContext(){return new Proxy({}, {get:()=>()=>{},set:()=>true});}
}
const elements=new Map();
const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const context=vm.createContext({document:{querySelector:get,querySelectorAll:()=>[],createElement:()=>new Element()},
  window:{addEventListener(){},setInterval(){return 1;},clearInterval(){},devicePixelRatio:1},
  navigator:{},TextEncoder,TextDecoder,Date,Blob,URL,console});
vm.runInContext(readFileSync(new URL('../sounds.js',import.meta.url),'utf8'),context);
vm.runInContext(readFileSync(new URL('../app.js',import.meta.url),'utf8')+'\nglobalThis.api={state,sendCommand,handleSerialLine,resetData,parseMachineTelemetry,recordPacket,renderPreparation,stationSounds};',context);
const api=context.api,bytes=[];
assert.equal(get('#altitudeNow').textContent,'--');
assert.match(get('#telemetryNote').textContent,/Esperando telemetría CPV/);
assert.match(get('#parachutePanel').className,/pending/);
await api.sendCommand('ARMAR');assert.match(get('#commandResult').textContent,/no enviado/);
api.state.connected=true;api.state.writer={write:async data=>bytes.push(new TextDecoder().decode(data))};
await api.sendCommand(' armar ');await api.sendCommand('activar');
assert.deepEqual(bytes,['ARMAR\n','ACTIVAR\n']);
assert.equal(api.state.packetsOk,0); // Commands work with no incoming telemetry.
assert.match(get('#commandResult').textContent,/enviado por USB/);
api.handleSerialLine('Enviado: ACTIVAR');
assert.match(get('#commandResult').textContent,/estación informó transmisión LoRa/);
assert.match(get('#commandResult').textContent,/no confirmados/);
assert.equal(api.state.packetsOk,0);
await api.sendCommand('ARMAR\nACTIVAR');await api.sendCommand('DESARMAR');
assert.equal(bytes.length,2);
api.state.demoTimer=1;await api.sendCommand('ACTIVAR');assert.equal(bytes.length,2);
api.state.demoTimer=null;
api.state.writer={write:async()=>{throw Error('USB cerrado');}};
await api.sendCommand('ARMAR');assert.match(get('#commandResult').textContent,/error al enviar/);
// A station reply can arrive before writer.write resolves.
api.state.writer={write:async()=>api.handleSerialLine('Enviado: ACTIVAR')};
await api.sendCommand('ACTIVAR');assert.match(get('#commandResult').textContent,/estación informó transmisión LoRa/);
// The station confirms command receipt separately from CPV activation telemetry.
api.handleSerialLine('UI_CMD,PENDIENTE,10,1,ACTIVAR,0');
api.handleSerialLine('UI_CMD,TX,10,1,ACTIVAR,0');
assert.match(get('#parachutePanel').className,/pending/);
api.handleSerialLine('UI_CMD,ACK,10,99,ACTIVAR,1');
assert.match(get('#commandResult').textContent,/esperando confirmación/);
api.handleSerialLine('UI_CMD,ACK,10,1,ACTIVAR,1');
assert.match(get('#commandResult').textContent,/aceptado por la CPV/);
assert.match(get('#parachutePanel').className,/pending/);
const tlm=(overrides={})=>{
  const p={boot:77,sample:1,uptime:500,flags:271,source:0,flightState:1,epoch:0,
    altitude:100,speed:23,accel:-9.2,maxAltitude:120,maxSpeed:60,maxAccel:35,...overrides};
  return `UI_TLM2,${p.boot},${p.sample},${p.uptime},${p.flags},${p.source},${p.flightState},${p.epoch},0.1,0.2,1,1,2,3,1000,${p.altitude},${p.speed},${p.accel},${p.maxAltitude},${p.maxSpeed},${p.maxAccel},-80,7.5,${0x32565043}`;
};
api.handleSerialLine(tlm());
assert.match(get('#commandResult').textContent,/aceptado por la CPV/); // First telemetry cannot erase ACK.
assert.equal(get('#maxSpeed').textContent,'60.00 m/s');
assert.equal(get('#maxAcceleration').textContent,'35.00 m/s²');
assert.equal(get('#maxAltitude').textContent,'120.00 m');
assert.equal(api.state.history.at(-1).verticalSpeed,23); // CPV estimate, not USB arrival timing.
api.handleSerialLine(tlm({sample:3,flags:287,source:1}));
assert.equal(api.state.packetsLost,1);
assert.match(get('#parachutePanel').className,/deployed/);
assert.match(get('#parachuteDetail').textContent,/Automático/);
api.handleSerialLine(tlm({sample:3})); // Duplicate or older telemetry must not change the latch.
api.handleSerialLine(tlm({sample:2}));
assert.equal(api.state.packetsOk,2);
for(let i=4;i<190;i++)api.handleSerialLine(tlm({sample:i,flags:287,source:1,altitude:40,speed:-5,maxAltitude:80,maxSpeed:10,maxAccel:3}));
assert.equal(api.state.history.length,180);
assert.equal(get('#maxAltitude').textContent,'120.00 m');
api.resetData(false); // Clear graphs keeps the confirmed status and peaks.
assert.match(get('#parachutePanel').className,/deployed/);
assert.equal(get('#maxSpeed').textContent,'60.00 m/s');
api.handleSerialLine(tlm({boot:88,sample:0,flags:0,source:0,altitude:'nan',speed:'nan',accel:'nan',maxAltitude:'nan',maxSpeed:'nan',maxAccel:'nan'}));
assert.match(get('#parachutePanel').className,/pending/);
assert.equal(get('#altitudeNow').textContent,'--');
assert.equal(get('#maxSpeed').textContent,'-- m/s');
api.handleSerialLine(tlm({boot:88,sample:1,flags:16,source:2})); // Manual activation even without valid sensors.
assert.match(get('#parachutePanel').className,/deployed/);
assert.match(get('#parachuteDetail').textContent,/Manual/);
assert.equal(get('#accelerationNow').textContent,'--');
const before=api.state.packetsOk;
api.handleSerialLine(tlm().split(',').slice(0,-1).join(','));
api.handleSerialLine(tlm().replace('UI_TLM2,77','UI_TLM2,no'));
assert.equal(api.state.packetsOk,before);
api.handleSerialLine('UI_CMD,PENDIENTE,10,2,ARMAR,0');
api.handleSerialLine('UI_CMD,ACK,10,2,ARMAR,2');
assert.match(get('#commandResult').textContent,/rechazado/);
api.handleSerialLine('UI_CMD,PENDIENTE,10,3,ACTIVAR,0');
api.handleSerialLine('UI_CMD,SIN_CONFIRMACION,10,3,ACTIVAR,0');
assert.match(get('#commandResult').textContent,/sin confirmación/);
assert.match(get('#parachutePanel').className,/deployed/);
const fixture='build/cpv-rx-tests/station-telemetry.txt';
if(existsSync(fixture)) {
  api.resetData();
  for(const line of readFileSync(fixture,'utf8').trim().split(/\r?\n/))api.handleSerialLine(line);
  assert.equal(get('#maxAltitude').textContent,'1234.00 m');
  assert.equal(get('#maxSpeed').textContent,'80.00 m/s');
  assert.match(get('#parachutePanel').className,/deployed/);
  console.log('PASS station firmware output -> UI telemetry parser and deployment display.');
}
mkdirSync('build/ui-command-tests',{recursive:true});
writeFileSync('build/ui-command-tests/ui-bytes.txt',bytes.join(''));
// Manual preflight procedure: a sent command or ACK alone is not readiness.
api.resetData();
const preparation=(sample,flags,extra={})=>tlm({boot:99,sample,flags,flightState:0,source:0,altitude:0,speed:0,accel:'nan',...extra});
api.handleSerialLine(preparation(0,2048|1|2));
assert.equal(get('#calibrationState').textContent,'SIN SOLICITAR');
assert.equal(get('#flightWatchState').textContent,'VIGILANDO VUELO');
assert.match(get('#preparationState').textContent,/PREPARACIÓN PENDIENTE/);
assert.equal(get('#altitudeNow').textContent,'0.0'); // Basic reference renders even without full calibration.
const calibrationBytes=[];
api.state.connected=true;api.state.writer={write:async data=>calibrationBytes.push(new TextDecoder().decode(data))};
await api.sendCommand(' calibrar ');
assert.deepEqual(calibrationBytes,['CALIBRAR\n']);
api.handleSerialLine('UI_CMD,PENDIENTE,55,1,CALIBRAR,0');
api.handleSerialLine('UI_CMD,ACK,55,1,CALIBRAR,1');
assert.match(get('#commandResult').textContent,/espera la calibración completa/);
assert.equal(get('#calibrationState').textContent,'SIN SOLICITAR');
api.handleSerialLine(preparation(1,2048|1|2|128));
assert.equal(get('#preparationState').textContent,'ARMANDO PARACAÍDAS');
api.handleSerialLine(preparation(2,2048|1|2|1024));
assert.equal(get('#armingState').textContent,'ARMADO');
assert.equal(get('#calibrationState').textContent,'SIN SOLICITAR');
api.handleSerialLine(preparation(3,2048|1|2|1024|512));
assert.equal(get('#preparationState').textContent,'CALIBRACIÓN MANUAL EN CURSO');
api.handleSerialLine(preparation(4,2048|1|2|1024|4|8,{epoch:1}));
assert.equal(get('#preparationState').textContent,'LISTO PARA VUELO');
assert.equal(get('#preparationState').className,'ok');
api.renderPreparation(null,true);
assert.equal(get('#preparationState').textContent,'SIN TELEMETRÍA RECIENTE');
api.handleSerialLine(preparation(5,2048|2|1024|4,{epoch:1}));
assert.match(get('#preparationState').textContent,/PREPARACIÓN PENDIENTE/); // Missing orientation/IMU cannot claim ready.
api.handleSerialLine(preparation(6,2048|1|1024|4|8,{epoch:1}));
assert.equal(get('#preparationState').textContent,'SIN DATOS PARA DETECTAR VUELO');
api.handleSerialLine(preparation(7,2048|1|2|1024|4096,{epoch:1}));
assert.equal(get('#preparationState').textContent,'CALIBRACIÓN NO COMPLETADA');
api.handleSerialLine(preparation(8,2048|2|32|4096,{epoch:1,flightState:1}));
assert.equal(get('#preparationState').textContent,'EN VUELO');
assert.equal(get('#flightWatchState').textContent,'VUELO DETECTADO');
const heard=[];api.stationSounds.play=event=>heard.push(event);
api.handleSerialLine(tlm({boot:404,sample:0,flags:2048|16|2,source:2}));
assert.equal(heard.filter(event=>event==='parachute').length,1);
api.handleSerialLine(tlm({boot:404,sample:1,flags:2048|16|2,source:2}));
assert.equal(heard.filter(event=>event==='parachute').length,1);
await api.sendCommand(' reiniciar ');
assert.equal(calibrationBytes.at(-1),'REINICIAR\n');
assert.match(get('#parachutePanel').className,/deployed/); // A click never resets CPV state optimistically.
api.handleSerialLine('UI_CMD,PENDIENTE,55,2,REINICIAR,0');
api.handleSerialLine('UI_CMD,ACK,55,2,REINICIAR,1');
assert.match(get('#commandResult').textContent,/esperando telemetría del nuevo arranque/);
api.handleSerialLine(tlm({boot:404,sample:2,flags:8192|16,source:2}));
assert.equal(get('#preparationState').textContent,'REINICIO CPV PENDIENTE');
api.handleSerialLine('UI_CMD,REINICIADO,55,2,REINICIAR,0');
api.handleSerialLine(tlm({boot:405,sample:0,flags:0,source:0,flightState:0,maxAltitude:'nan',maxSpeed:'nan',maxAccel:'nan'}));
assert.match(get('#parachutePanel').className,/pending/);
assert.equal(get('#maxAltitude').textContent,'-- m');
assert.match(get('#commandResult').textContent,/nuevo arranque confirmado/);
assert.equal(heard.at(-1),'restart');
api.handleSerialLine('UI_CMD,PENDIENTE,55,3,REINICIAR,0');
api.handleSerialLine('UI_CMD,SIN_REINICIO,55,3,REINICIAR,0');
assert.match(get('#commandResult').textContent,/no se confirmó/);
api.handleSerialLine('UI_CMD,SIN_DESTINO,55,4,REINICIAR,0');
assert.match(get('#commandResult').textContent,/no enviado/);
console.log('PASS UI reboot and sounds: REINICIAR bytes, actual new-boot confirmation, no optimistic reset, activation sound only from CPV telemetry.');
console.log('PASS UI manual preparation: CALIBRAR USB/ACK, separate arming, baseline telemetry, readiness from actual sensor flags, stale link, failure and unprepared flight.');
console.log('PASS UI: command cascade, ACK identity, automatic/manual status, sensor failures, peaks past chart history, reboot, duplicates, loss, exact USB bytes, demo and USB errors.');
