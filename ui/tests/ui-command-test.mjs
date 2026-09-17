import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
class Element {
  constructor(){this.textContent='';this.children=[];this.dataset={};this.classList={add(){},remove(){}};}
  append(...items){this.children.push(...items);}
  prepend(item){this.children.unshift(item);}
  replaceChildren(){this.children=[];}
  addEventListener(){}
  getBoundingClientRect(){return {width:760,height:260};}
  getContext(){return new Proxy({}, {get:()=>()=>{},set:()=>true});}
}
const elements=new Map();
const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const context=vm.createContext({document:{querySelector:get,querySelectorAll:()=>[],createElement:()=>new Element()},
  window:{addEventListener(){},setInterval(){return 1;},clearInterval(){},devicePixelRatio:1},
  navigator:{},TextEncoder,TextDecoder,Date,Blob,URL,console});
vm.runInContext(readFileSync(new URL('../app.js',import.meta.url),'utf8')+'\nglobalThis.api={state,sendCommand,handleSerialLine,resetData};',context);
const api=context.api,bytes=[];
assert.equal(get('#altitudeNow').textContent,'--');
assert.match(get('#telemetryNote').textContent,/no se espera telemetría ni ACK/);
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
mkdirSync('build/ui-command-tests',{recursive:true});
writeFileSync('build/ui-command-tests/ui-bytes.txt',bytes.join(''));
console.log('PASS UI: ARMAR/ACTIVAR exact bytes, no telemetry/ACK dependency, transmission status, invalid commands, demo, USB errors.');
