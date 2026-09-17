import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {FlightModel,defaults,parseEphemeris,makeSamples,K} from './model.mjs';
const source=readFileSync(new URL('./350deg_AR_4_57ms_Aire.e',import.meta.url),'utf8');
const rows=parseEphemeris(source),samples=makeSamples(rows);
const m=new FlightModel();let fired=null,edges=0,previous=false;
for(const s of samples){const r=m.step(s.t,s.p,s.f);assert.ok(Object.values(r).every(x=>typeof x!=='number'||Number.isFinite(x)));if(r.fired&&!previous){fired=r;edges++;}previous=r.fired;}
const peak=samples.reduce((a,b)=>a.h>b.h?a:b);
assert.equal(edges,1);assert.ok(fired.t<peak.t);assert.ok(fired.eta>0&&fired.eta<=2);
assert.ok(fired.t+defaults.actuatorDelay<peak.t);
// Trigger at the first valid prediction <=2 s, never before it.
const gate=new FlightModel();
for(const s of samples){const r=gate.step(s.t,s.p,s.f);if(r.t<fired.t)assert.equal(r.fired,false);}
// Keep the descent confirmation as fallback when anticipation is disabled.
const backup=new FlightModel({...defaults,lead:0});let backupEvent;
for(const s of samples){const r=backup.step(s.t,s.p,s.f);if(r.fired&&!backupEvent)backupEvent=r;}
assert.ok(backupEvent.t>peak.t&&backupEvent.t-peak.t<1);
const stationary=new FlightModel();for(let i=0;i<1000;i++)stationary.step(i*.02,101325,9.81);assert.equal(stationary.fired,false);assert.equal(stationary.vr,0);
assert.throws(()=>stationary.step(0,101325,9.81));assert.throws(()=>stationary.step(20,NaN,9.81));assert.throws(()=>parseEphemeris(source.replace('DistanceUnit Meters','DistanceUnit Kilometers')));
// PDF worked example, including force projection before gravity compensation.
const a1=-Math.sin(10*Math.PI/180)+15*Math.cos(10*Math.PI/180)-9.81;
const a2=-2*Math.sin(20*Math.PI/180)+14*Math.cos(20*Math.PI/180)-9.81;
const example=new FlightModel();example.step(0,100000,a1+9.81);example.vr=8;
assert.ok(Math.abs(example.step(.1,99990.5,a2+9.81).vr-8.328)<.002);
assert.ok(Math.abs(8.328/11.81-.705)<.001);
// A descent without previously recognizing launch must never trigger.
const falling=new FlightModel();for(let i=0;i<200;i++)falling.step(i*.02,101325*Math.exp(i*.02/K),9.81);assert.equal(falling.fired,false);
// A short false descent must reset the confirmation timer.
const brief=new FlightModel({...defaults,hold:.15});brief.step(0,101325,9.81);brief.state=1;brief.peak=10;brief.vr=-1;brief.step(.02,101325*Math.exp(-8/K),9.81);assert.equal(brief.fired,false);
// Isolate the persistence gate with a consistent descending velocity.
brief.h=8;brief.vr=-1;brief.since=.02;
brief.step(.04,101325*Math.exp(-7.98/K),9.81);assert.equal(brief.since,.02);assert.equal(brief.fired,false);
brief.vr=1;brief.step(.06,101325*Math.exp(-8.02/K),9.81);assert.equal(brief.since,-1);assert.equal(brief.fired,false);
console.log(JSON.stringify({points:rows.length,samples:samples.length,duration:samples.at(-1).t,apogee:{t:peak.t,h:peak.h},deployment:fired,delay:fired.t-peak.t,tests:'PASS'},null,2));
