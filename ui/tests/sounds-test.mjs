import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

class AudioStub {
  constructor() { this.currentTime=0; this.state='suspended'; this.destination={}; this.oscillators=[]; }
  async resume() { this.state='running'; }
  createGain() {
    return { gain:{value:0,setValueAtTime(){},linearRampToValueAtTime(){}},connect(){},disconnect(){} };
  }
  createOscillator() {
    const node={frequency:{value:0},connect(){},disconnect(){},starts:[],stops:[],start(t){this.starts.push(t);},stop(t){this.stops.push(t);}};
    this.oscillators.push(node); return node;
  }
}
const context=vm.createContext({window:{AudioContext:AudioStub},console});
vm.runInContext(readFileSync(new URL('../sounds.js',import.meta.url),'utf8'),context);
const sound=new context.window.StationSounds();
assert.equal(sound.play('parachute'),false); // Browser gesture still needed.
assert.equal(await sound.unlock(),true);
sound.observe({version:2,boot:1,epoch:0,deployed:true,source:1});
const alarmNotes=sound.context.oscillators.length;
assert.ok(alarmNotes>5);
assert.ok(sound.activeUntil>3 && sound.activeUntil<5);
assert.ok(sound.context.oscillators.some(n=>n.type==='square'));
assert.ok(sound.context.oscillators.some(n=>n.type==='triangle'));
const schedule=audio=>audio.context.oscillators.map(n=>({frequency:n.frequency.value,type:n.type,starts:n.starts,stops:n.stops}));
const preview=new context.window.StationSounds();
assert.equal(await preview.unlock(),true);
assert.equal(preview.play('test'),true);
assert.deepEqual(schedule(preview),schedule(sound)); // Preview and actual activation sound identical.
assert.equal(preview.records.size,0); // Preview doesn't consume the real activation event.
assert.equal(sound.play('test'),false); // Preview cannot interrupt a real activation alarm.
sound.context.currentTime+=5;
sound.observe({version:2,boot:1,epoch:0,deployed:true,source:1});
sound.observe({version:2,boot:1,epoch:1,deployed:true,source:1});
assert.equal(sound.context.oscillators.length,alarmNotes); // Repeated telemetry or new calibration epoch is not another activation.
sound.observe({version:2,boot:2,epoch:0,deployed:false});
assert.equal(sound.context.oscillators.length,alarmNotes+3); // Reboot chime.
sound.context.currentTime+=5;
sound.observe({version:2,boot:2,epoch:0,deployed:true,source:2});
assert.equal(sound.context.oscillators.length,2*alarmNotes+3); // Manual activation also sounds.
sound.setEnabled(false);
assert.equal(sound.nodes.length,0);
sound.observe({version:2,boot:3,epoch:0,deployed:true,source:2});
assert.equal(sound.context.oscillators.length,2*alarmNotes+3);
sound.setEnabled(true);
sound.context.currentTime+=5;
sound.observe({version:2,boot:3,epoch:0,deployed:true,source:2});
assert.equal(sound.context.oscillators.length,2*alarmNotes+3); // Unmuting doesn't replay old events.
sound.setVolume(.25);assert.equal(sound.master.gain.value,.25);
sound.setVolume(0);assert.equal(sound.play('test'),false);
sound.setVolume(1);assert.equal(sound.play('warning'),true);
const previous=sound.context.oscillators.at(-1);
assert.equal(sound.play('parachute'),true); // Activation preempts a lower-priority warning.
assert.equal(previous.stops.length,2);
const unsupported=vm.createContext({window:{}});
vm.runInContext(readFileSync(new URL('../sounds.js',import.meta.url),'utf8'),unsupported);
assert.equal(await new unsupported.window.StationSounds().unlock(),false);
console.log('PASS audio: dramatic deployment alarm and identical preview, one event per boot, replay suppression, mute, volume, priority, gesture unlock and unavailable audio.');
