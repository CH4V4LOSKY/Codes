import {parseEphemeris,makeSamples,FlightModel,defaults} from './model.mjs';
const $=id=>document.getElementById(id);
let samples=[],records=[],model,idx=0,running=false,paused=false,session=0,port,reader,writer,pending,buffer='',deployment=null,lastState=-1,startWall=0,startSim=0,wallElapsed=0,initializing=false,confirmed=false;
function event(text){const li=document.createElement('li');li.textContent=text;$('events').prepend(li);}
function fail(e){running=false;paused=false;event('Error: '+e.message);$('decision').textContent=e.message;locks();}
function locks(){const busy=running||paused||initializing;for(const id of ['mode','file','tau','speed','connect'])$(id).disabled=busy;$('start').disabled=busy||!samples.length;$('pause').disabled=initializing||!busy;$('reset').disabled=initializing;$('pause').textContent=paused?'Continuar':'Pausar';$('export').disabled=!records.length;for(const id of ['manualArm','manualDisarm'])$(id).disabled=busy||!confirmed||!writer;}
function load(text,name){const rows=parseEphemeris(text);samples=makeSamples(rows);resetView();$('fileInfo').textContent=`${name} · ${rows.length} puntos · ${samples.at(-1).t.toFixed(2)} s`;event('Trayectoria lista. Altura relativa al primer punto.');locks();draw();}
function resetView(){records=[];idx=0;deployment=null;lastState=-1;model=new FlightModel({...defaults,tau:Number($('tau').value)});$('events').replaceChildren();$('state').textContent='En espera';$('decision').textContent='Sin decisión de despliegue.';$('chute').classList.remove('fired');$('progress').style.width='0';$('time').textContent='0.00 s';$('height').textContent='0.00 m';$('velocity').textContent='0.00 m/s';$('eta').textContent='—';$('timing').textContent='Esperando inicio.';}
function acceptLine(line){
  if(line.startsWith('ERR,')){if(pending){const p=pending;pending=null;clearTimeout(p.timer);p.reject(Error(line));}else fail(Error(line));return;}
  if(pending&&pending.match(line)){const p=pending;pending=null;clearTimeout(p.timer);p.resolve(line);}
}
async function readLoop(){try{while(true){const {value,done}=await reader.read();if(done)break;buffer+=new TextDecoder().decode(value);let n;while((n=buffer.indexOf('\n'))>=0){acceptLine(buffer.slice(0,n).trim());buffer=buffer.slice(n+1);}if(buffer.length>8192)throw Error('Respuesta serial demasiado larga');}}catch(e){event('USB: '+e.message);}finally{if(pending){clearTimeout(pending.timer);pending.reject(Error('Conexión USB cerrada'));pending=null;}running=false;paused=false;confirmed=false;try{reader.releaseLock();writer?.releaseLock();await port.close();}catch{}writer=null;$('connection').textContent='ESP32 desconectado';locks();}}
async function command(text,match){
  if(!writer)throw Error('Conecta el ESP32 primero.');
  if(pending)throw Error('Existe una solicitud serial pendiente.');
  return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{pending=null;reject(Error('ESP32 no respondió en 2 segundos.'));},2000);pending={resolve,reject,match,timer};writer.write(new TextEncoder().encode(text+'\n')).catch(e=>{clearTimeout(timer);pending=null;reject(e);});});
}
$('connect').onclick=async()=>{try{
  if(!navigator.serial)throw Error('Abre localhost en Chrome o Edge para usar USB.');
  if(writer){if(!confirmed)await command('HELLO',x=>x==='READY,CPV_SIM,4,MOTOR');confirmed=true;event('ESP32 conectado.');locks();return;}
  port=await navigator.serial.requestPort();await port.open({baudRate:115200});writer=port.writable.getWriter();reader=port.readable.getReader();buffer='';readLoop();
  await new Promise(r=>setTimeout(r,1800));await command('HELLO',x=>x==='READY,CPV_SIM,4,MOTOR');confirmed=true;$('mode').value='serial';$('connection').textContent='ESP32 conectado · salida motor';event('Firmware CPV_SIM v4 confirmado.');locks();
}catch(e){fail(e);}};
$('mode').onchange=()=>{$('connection').textContent=$('mode').value==='local'?'Modo local · sin placa':writer?'ESP32 conectado · salida motor':'USB · falta conectar';};
$('file').onchange=async e=>{try{const f=e.target.files[0];if(f)load(await f.text(),f.name);}catch(e){samples=[];fail(e);}};
$('start').onclick=async()=>{try{
  const tau=Number($('tau').value);if(!Number.isFinite(tau)||tau<0.02||tau>5)throw Error('τ debe estar entre 0.02 y 5 s.');
  if($('mode').value==='serial'&&(!writer||!confirmed))throw Error('Conecta primero el firmware CPV_SIM.');
  if($('mode').value==='serial')$('speed').value='1';
  resetView();running=true;initializing=true;locks();
  if($('mode').value==='serial'){await command('RESET',x=>x==='RESET_OK');await command(`CFG,${tau}`,x=>x==='CONFIG_OK');await command('ARM',x=>x==='ARMED');}
  $('source').textContent=$('mode').value==='serial'?'Decisión y estimaciones recibidas del ESP32.':'Resultados calculados en este navegador.';
  initializing=false;locks();event('Prueba iniciada · '+($('mode').value==='serial'?'ESP32':'modelo local'));startWall=performance.now();startSim=0;wallElapsed=0;const token=++session;run(token);
}catch(e){initializing=false;fail(e);}};
$('pause').onclick=()=>{if(running){paused=true;running=false;wallElapsed+=(performance.now()-startWall)/1000;event('Reproducción pausada.');}else if(paused){paused=false;running=true;startWall=performance.now();startSim=samples[idx]?.t??0;event('Reproducción reanudada.');}locks();};
$('reset').onclick=async()=>{try{running=false;paused=false;++session;if(pending){await new Promise(r=>setTimeout(r,2100));}if(writer)await command('RESET',x=>x==='RESET_OK');resetView();locks();draw();}catch(e){fail(e);}};
async function run(token){try{while((running||paused)&&token===session&&idx<samples.length){
  if(paused){await new Promise(r=>setTimeout(r,20));continue;}
  const s=samples[idx],speed=Number($('speed').value),target=startWall+(s.t-startSim)*1000/speed;
  if(performance.now()<target){await new Promise(r=>setTimeout(r,Math.min(20,target-performance.now())));continue;}
  let result,latency=0;
  if($('mode').value==='serial'){
    const now=performance.now(),seq=idx;
    const line=await command(`S,${seq},${s.t.toFixed(6)},${s.p.toFixed(9)},${s.f.toFixed(9)}`,x=>x.startsWith(`R,${seq},`));
    latency=performance.now()-now;const a=line.split(',').slice(2).map(Number);
    if(a.length!==9||!a.every(Number.isFinite)||Math.abs(a[0]-s.t)>1e-5)throw Error('Respuesta de telemetría inválida.');
    result={t:a[0],h:a[1],vr:a[2],vb:a[3],va:a[4],af:a[5],eta:a[6],state:a[7],fired:a[8]===1};
  }else result=model.step(s.t,s.p,s.f);
  if(token!==session)return;
  records.push({...result,ref:s.v,p:s.p,f:s.f,latency});idx++;
  if(result.state!==lastState){event(`${s.t.toFixed(2)} s · ${['Esperando ascenso','Ascenso reconocido','Orden de activación'][result.state]}`);lastState=result.state;}
  if(result.fired&&!deployment){deployment={...result,releaseAt:s.t+defaults.actuatorDelay,releaseShown:false};$('chute').classList.add('fired');$('decision').textContent=`Orden a ${s.t.toFixed(2)} s (${result.eta>0?'apogeo estimado en '+result.eta.toFixed(2)+' s':'respaldo por descenso'}). Liberación estimada a ${deployment.releaseAt.toFixed(2)} s; no medida.`;}
  if(deployment&&!deployment.releaseShown&&s.t+1e-8>=deployment.releaseAt){deployment.releaseShown=true;event(`${deployment.releaseAt.toFixed(2)} s · Liberación estimada tras 1 s de actuador (sin confirmación física).`);}
  $('time').textContent=s.t.toFixed(2)+' s';$('height').textContent=result.h.toFixed(2)+' m';$('velocity').textContent=result.vr.toFixed(2)+' m/s';$('eta').textContent=result.eta>=0?result.eta.toFixed(3)+' s':'—';$('state').textContent=deployment?(deployment.releaseShown?'Liberación estimada':'Actuador en liberación'):['Esperando ascenso','Ascenso reconocido','Orden enviada'][result.state];$('progress').style.width=(idx/samples.length*100)+'%';
  const elapsed=wallElapsed+(performance.now()-startWall)/1000;
  $('timing').textContent=`Reloj real: ${elapsed.toFixed(1)} s · USB: ${latency.toFixed(1)} ms · retraso: ${Math.max(0,(performance.now()-target)/1000).toFixed(2)} s`;
  if(idx%3===0||idx===samples.length)draw();
  if($('mode').value==='local')await new Promise(r=>setTimeout(r,0));
}if(idx===samples.length&&token===session){running=false;event('Trayectoria terminada.');locks();}}catch(e){fail(e);}}
function chart(id,series){const c=$(id),dpr=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*dpr;c.height=h*dpr;const ctx=c.getContext('2d');ctx.scale(dpr,dpr);const pad={l:53,r:15,t:15,b:28},W=w-pad.l-pad.r,H=h-pad.t-pad.b,end=samples.at(-1)?.t||1;
  const vals=series.flatMap(s=>s.data.map(p=>p.y)).filter(Number.isFinite);let lo=Math.min(0,...vals),hi=Math.max(1,...vals);const gap=(hi-lo)*.08;lo-=gap;hi+=gap;
  const X=t=>pad.l+t/end*W,Y=v=>pad.t+(hi-v)/(hi-lo)*H;
  ctx.font='11px Segoe UI';ctx.lineWidth=1;for(let i=0;i<=4;i++){const v=lo+(hi-lo)*i/4,y=Y(v);ctx.strokeStyle='#28374c';ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);ctx.stroke();ctx.fillStyle='#8fa2ba';ctx.fillText(v.toFixed(1),3,y+4);const t=end*i/4;ctx.fillText(t.toFixed(1)+' s',Math.min(X(t)-10,w-44),h-5);}
  for(const s of series){ctx.strokeStyle=s.color;ctx.lineWidth=s.width||1.5;ctx.beginPath();s.data.forEach((p,i)=>i?ctx.lineTo(X(p.t),Y(p.y)):ctx.moveTo(X(p.t),Y(p.y)));ctx.stroke();}
  if(deployment){ctx.strokeStyle='#ffb066';ctx.setLineDash([5,4]);ctx.beginPath();ctx.moveTo(X(deployment.t),pad.t);ctx.lineTo(X(deployment.t),h-pad.b);ctx.stroke();ctx.setLineDash([]);}
  if(deployment&&deployment.releaseShown){ctx.strokeStyle='#f4e68c';ctx.setLineDash([2,4]);ctx.beginPath();ctx.moveTo(X(deployment.releaseAt),pad.t);ctx.lineTo(X(deployment.releaseAt),h-pad.b);ctx.stroke();ctx.setLineDash([]);}
}
function draw(){const data=(rows,key)=>rows.map(r=>({t:r.t,y:r[key]}));chart('altChart',[{data:data(samples,'h'),color:'#57667c'},{data:data(records,'h'),color:'#52e0c4',width:2.5}]);chart('velChart',[{data:data(samples,'v'),color:'#57667c'},{data:data(records,'vb'),color:'#b895ef'},{data:data(records,'va'),color:'#67a7f2'},{data:data(records,'vr'),color:'#52e0c4',width:2}]);chart('accChart',[{data:data(records,'af'),color:'#52e0c4'}]);}
window.addEventListener('resize',draw);
$('export').onclick=()=>{const keys=['t','h','ref','vr','vb','va','af','eta','state','fired','p','f','latency'];const csv=keys.join(',')+'\n'+records.map(r=>keys.map(k=>r[k]).join(',')).join('\n');const url=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));const a=document.createElement('a');a.href=url;a.download='CPV_resultados.csv';a.click();URL.revokeObjectURL(url);};
try{const response=await fetch('./350deg_AR_4_57ms_Aire.e');if(!response.ok)throw Error('No se pudo cargar el archivo predeterminado');load(await response.text(),'350deg_AR_4_57ms_Aire.e');}catch(e){fail(e);}

async function manualMotor(action){
  if(running||paused||initializing||!confirmed||!writer)return;
  initializing=true;locks();
  try{
    await command('STOP',x=>x==='STOPPED');
    event(action+' · motor encendido durante 1 segundo.');
    await command(action,x=>x==='DONE,'+action);
    event(action+' · pulso terminado, motor apagado (posición no medida).');
  }catch(e){fail(e);}finally{initializing=false;locks();}
}
$('manualArm').onclick=()=>manualMotor('ARMAR');
$('manualDisarm').onclick=()=>manualMotor('DESARMAR');
