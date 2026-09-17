export const G=9.81, K=287*293.15/G;
export const defaults={tau:0.4, accelTau:0.1, launchHeight:3, launchSpeed:3, descentSpeed:0.5, drop:0.5, hold:0.15,lead:2,actuatorDelay:1};
export function parseEphemeris(text){
  if(!/DistanceUnit Meters/.test(text)||!/EphemerisLLATimePos/.test(text)) throw Error('Se requiere STK LLA con alturas en metros.');
  const body=text.split('EphemerisLLATimePos')[1].split('END Ephemeris')[0];
  const rows=body.trim().split(/\r?\n/).filter(x=>x.trim()).map(line=>{
    const a=line.trim().split(/\s+/).map(Number);
    if(a.length!==4||!a.every(Number.isFinite)) throw Error('Fila de efemérides inválida.');
    return {t:a[0],lat:a[1],lon:a[2],alt:a[3]};
  });
  if(rows.length<5||rows[0].t!==0||rows.some((r,i)=>i&&r.t<=rows[i-1].t)) throw Error('Tiempos inválidos: deben iniciar en cero y crecer.');
  const count=Number(text.match(/NumberOfEphemerisPoints\s+(\d+)/)?.[1]);
  if(count!==rows.length) throw Error('El número de puntos no coincide con la cabecera.');
  const base=rows[0].alt;
  rows.forEach(r=>r.h=r.alt-base);
  return rows;
}
// Offline sensor synthesis: resample to 50 Hz, then differentiate over 100 ms.
// These derivatives may use future trajectory points; only the generator sees them.
export function makeSamples(rows){
  let j=0; const end=rows.at(-1).t, out=[];
  for(let i=0;i*0.02<=end+1e-8;i++){
    const t=i*0.02;
    while(j+1<rows.length-1&&rows[j+1].t<t)j++;
    const a=rows[j],b=rows[j+1],w=(t-a.t)/(b.t-a.t);
    out.push({t,h:a.h+w*(b.h-a.h)});
  }
  const derivative=(values,i)=>{const a=Math.max(0,i-5),b=Math.min(values.length-1,i+5);return (values[b]-values[a])/((b-a)*0.02);};
  const heights=out.map(s=>s.h),vel=out.map((_,i)=>derivative(heights,i));
  out.forEach((s,i)=>{s.v=vel[i];s.az=derivative(vel,i);s.f=s.az+G;s.p=101325*Math.exp(-s.h/K);});
  return out;
}
export class FlightModel{
  constructor(c=defaults){this.c={...c};this.t=null;this.h=0;this.vr=0;this.vb=0;this.va=0;this.az=0;this.af=0;this.eta=-1;this.peak=0;this.since=-1;this.state=0;this.fired=false;}
  step(t,p,f){
    if(![t,p,f].every(Number.isFinite)||p<=0||t<0)throw Error('Muestra inválida');
    if(this.t!==null&&(t<=this.t||t-this.t>0.25))throw Error('Intervalo de muestra inválido');
    const a=f-G;
    if(this.t===null){this.p0=p;this.t=t;this.az=a;this.af=a;return this.snapshot();}
    const dt=t-this.t,h=K*Math.log(this.p0/p);
    this.vb=(h-this.h)/dt;this.va=this.vr+(this.az+a)*0.5*dt;
    const alpha=this.c.tau/(this.c.tau+dt);
    this.vr=alpha*this.va+(1-alpha)*this.vb;
    this.af+=dt/(this.c.accelTau+dt)*(a-this.af);
    this.az=a;this.h=h;this.t=t;this.peak=Math.max(this.peak,h);
    this.eta=this.vr>0&&this.af< -0.1 ? -this.vr/this.af : -1;
    if(this.state===0&&h>=this.c.launchHeight&&this.vr>=this.c.launchSpeed)this.state=1;
    if(this.state===1){
      const descent=this.vr< -this.c.descentSpeed&&this.peak-h>=this.c.drop;
      if(!descent)this.since=-1;else if(this.since<0)this.since=t;
      const anticipate=this.eta>0&&this.eta<=this.c.lead;
      if(anticipate||(this.since>=0&&t-this.since+1e-8>=this.c.hold)){this.state=2;this.fired=true;}
    }
    return this.snapshot();
  }
  snapshot(){return {t:this.t,h:this.h,vr:this.vr,vb:this.vb,va:this.va,af:this.af,eta:this.eta,state:this.state,fired:this.fired};}
}
