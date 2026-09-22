// Browser audio is independent of USB, radio and flight logic. No external
// audio files or network access; unlock() runs from a user gesture.
// Original cockpit-inspired alarm: three bursts of a low rattling buzzer
// under alternating high/low warning tones. Shared by preview and deployment.
const PARACHUTE_ALARM = [];
for (let burst = 0; burst < 3; burst++) {
  const base = burst * 1.4;
  for (let pulse = 0; pulse < 12; pulse++) {
    PARACHUTE_ALARM.push([165, base + pulse * .085, .05, "square", .075]);
  }
  for (let tone = 0; tone < 4; tone++) {
    PARACHUTE_ALARM.push([tone % 2 ? 640 : 960, base + tone * .28, .24, "triangle", .12]);
  }
}

class StationSounds {
  constructor() {
    this.enabled = true;
    this.volume = 0.6;
    this.context = null;
    this.master = null;
    this.nodes = [];
    this.activeUntil = 0;
    this.records = new Map();
    this.lastBoot = null;
  }

  async unlock() {
    if (!this.enabled) return false;
    try {
      const Audio = window.AudioContext || window.webkitAudioContext;
      if (!Audio) return false;
      if (!this.context) {
        this.context = new Audio();
        this.master = this.context.createGain();
        this.master.gain.value = this.volume;
        this.master.connect(this.context.destination);
      }
      if (this.context.state === "suspended") await this.context.resume();
      return this.context.state === "running";
    } catch {
      return false; // Audio must never prevent sending/receiving a command.
    }
  }

  setVolume(value) {
    if (!Number.isFinite(value)) return;
    this.volume = Math.max(0, Math.min(1, value));
    if (this.master) this.master.gain.value = this.volume;
  }

  setEnabled(enabled) {
    this.enabled = Boolean(enabled);
    if (!this.enabled) this.stop();
  }

  stop() {
    for (const node of this.nodes) {
      try { node.stop(); } catch { /* Already finished. */ }
    }
    this.nodes = [];
    this.activeUntil = 0;
  }

  play(event) {
    if (!this.enabled || !this.volume || this.context?.state !== "running") return false;
    const patterns = {
      parachute: PARACHUTE_ALARM,
      armed: [[440, 0, .12], [660, .18, .18]],
      calibrated: [[523, 0, .12], [659, .17, .12], [784, .34, .22]],
      ready: [[784, 0, .16], [1047, .22, .3]],
      restart: [[784, 0, .12], [523, .18, .12], [660, .36, .22]],
      warning: [[330, 0, .22], [330, .34, .22]]
    };
    const notes = patterns[event === "test" ? "parachute" : event];
    if (!notes) return false;
    const now = this.context.currentTime;
    if (event === "parachute") this.stop(); // Deployment always takes audible priority.
    else if (now < this.activeUntil) return false;
    try {
      for (const [frequency, offset, duration, waveform = "sine", level = .18] of notes) {
        const oscillator = this.context.createOscillator();
        const envelope = this.context.createGain();
        const start = now + .015 + offset;
        oscillator.type = waveform;
        oscillator.frequency.value = frequency;
        envelope.gain.setValueAtTime(0, start);
        envelope.gain.linearRampToValueAtTime(level, start + .015);
        envelope.gain.setValueAtTime(level, start + duration - .025);
        envelope.gain.linearRampToValueAtTime(0, start + duration);
        oscillator.connect(envelope);
        envelope.connect(this.master);
        this.nodes.push(oscillator);
        oscillator.onended = () => {
          oscillator.disconnect();
          envelope.disconnect();
          this.nodes = this.nodes.filter((node) => node !== oscillator);
        };
        oscillator.start(start);
        oscillator.stop(start + duration + .01);
      }
      this.activeUntil = now + .03 + Math.max(...notes.map(([, offset, duration]) => offset + duration));
      return true;
    } catch {
      this.stop();
      return false;
    }
  }

  observe(packet, { ready = false, demoSession = null } = {}) {
    if (packet.version !== 2 && demoSession === null) return;
    const key = demoSession === null ? `cpv:${packet.boot}` : `demo:${demoSession}`;
    let record = this.records.get(key);
    if (!record) {
      record = { epoch: packet.epoch, deployed: false, armed: false, calibrated: false, ready: false };
      this.records.set(key, record);
    }
    if (record.epoch !== packet.epoch) {
      record.epoch = packet.epoch;
      record.calibrated = record.ready = false;
    }
    const restarted = demoSession === null && this.lastBoot !== null && this.lastBoot !== packet.boot;
    if (demoSession === null) this.lastBoot = packet.boot;
    let event = null;
    if (packet.deployed && !record.deployed) event = "parachute";
    else if (restarted) event = "restart";
    else if (ready && !record.ready) event = "ready";
    else if (packet.calibrated && !record.calibrated && !packet.calibrating) event = "calibrated";
    else if (packet.armed && !record.armed) event = "armed";
    // Remember even while muted; enabling sound never invents a new activation.
    record.deployed ||= Boolean(packet.deployed);
    record.armed = Boolean(packet.armed);
    record.calibrated ||= Boolean(packet.calibrated);
    record.ready ||= ready;
    if (this.records.size > 32) this.records.delete(this.records.keys().next().value);
    if (event) this.play(event);
  }
}
window.StationSounds = StationSounds;
