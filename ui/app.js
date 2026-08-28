const BAUD_RATE = 115200;
const MAX_HISTORY = 180;
const DEG = 180 / Math.PI;

const els = {
  connectSerial: document.querySelector("#connectSerial"),
  demoMode: document.querySelector("#demoMode"),
  clearData: document.querySelector("#clearData"),
  exportCsv: document.querySelector("#exportCsv"),
  connectionState: document.querySelector("#connectionState"),
  missionTime: document.querySelector("#missionTime"),
  sampleCount: document.querySelector("#sampleCount"),
  packetsOk: document.querySelector("#packetsOk"),
  packetsLost: document.querySelector("#packetsLost"),
  packetRate: document.querySelector("#packetRate"),
  rollValue: document.querySelector("#rollValue"),
  pitchValue: document.querySelector("#pitchValue"),
  yawValue: document.querySelector("#yawValue"),
  gyroX: document.querySelector("#gyroX"),
  gyroY: document.querySelector("#gyroY"),
  gyroZ: document.querySelector("#gyroZ"),
  accX: document.querySelector("#accX"),
  accY: document.querySelector("#accY"),
  accZ: document.querySelector("#accZ"),
  headingValue: document.querySelector("#headingValue"),
  rssiValue: document.querySelector("#rssiValue"),
  snrValue: document.querySelector("#snrValue"),
  imuTemp: document.querySelector("#imuTemp"),
  baroTemp: document.querySelector("#baroTemp"),
  pressureValue: document.querySelector("#pressureValue"),
  altitudeNow: document.querySelector("#altitudeNow"),
  altitudeChart: document.querySelector("#altitudeChart"),
  velocityChart: document.querySelector("#velocityChart"),
  serialLog: document.querySelector("#serialLog"),
  commandForm: document.querySelector("#commandForm"),
  commandInput: document.querySelector("#commandInput"),
  lastCommand: document.querySelector("#lastCommand")
};

const state = {
  port: null,
  reader: null,
  writer: null,
  serialBuffer: "",
  connected: false,
  demoTimer: null,
  demoSample: 0,
  missionStart: null,
  packetsOk: 0,
  packetsLost: 0,
  lastSample: null,
  history: [],
  recentTimestamps: [],
  prettyPacket: null
};

function fixed(value, digits = 1, fallback = "--") {
  return Number.isFinite(value) ? value.toFixed(digits) : fallback;
}

function nowStamp() {
  const date = new Date();
  return date.toLocaleTimeString("es-MX", {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3
  });
}

function elapsedTime() {
  if (!state.missionStart) return "00:00:00";
  const total = Math.max(0, Math.floor((Date.now() - state.missionStart) / 1000));
  const hours = String(Math.floor(total / 3600)).padStart(2, "0");
  const minutes = String(Math.floor((total % 3600) / 60)).padStart(2, "0");
  const seconds = String(total % 60).padStart(2, "0");
  return `${hours}:${minutes}:${seconds}`;
}

function logLine(message) {
  const li = document.createElement("li");
  const time = document.createElement("time");
  const text = document.createElement("span");
  time.textContent = `[${nowStamp()}]`;
  text.textContent = message;
  li.append(time, text);
  els.serialLog.prepend(li);

  while (els.serialLog.children.length > 80) {
    els.serialLog.lastElementChild.remove();
  }
}

function setConnection(label, className) {
  els.connectionState.className = className;
  els.connectionState.textContent = label;
}

function splitCsv(line) {
  return line.split(",").map((value) => value.trim());
}

function numberAt(fields, index) {
  const value = Number(fields[index]);
  return Number.isFinite(value) ? value : NaN;
}

function parseTlmFields(fields, extra = {}) {
  if (fields.length < 16 || fields[0] !== "TLM") return null;

  return {
    sample: numberAt(fields, 1),
    imuOk: numberAt(fields, 2) === 1,
    accX: numberAt(fields, 3),
    accY: numberAt(fields, 4),
    accZ: numberAt(fields, 5),
    gyroX: numberAt(fields, 6),
    gyroY: numberAt(fields, 7),
    gyroZ: numberAt(fields, 8),
    imuTempC: numberAt(fields, 9),
    magOk: numberAt(fields, 10) === 1,
    headingDeg: numberAt(fields, 11),
    baroOk: numberAt(fields, 12) === 1,
    baroTempC: numberAt(fields, 13),
    pressureHpa: numberAt(fields, 14),
    altitudeM: numberAt(fields, 15),
    rssi: Number.isFinite(extra.rssi) ? extra.rssi : NaN,
    snr: Number.isFinite(extra.snr) ? extra.snr : NaN
  };
}

function parseMachineTelemetry(line) {
  const trimmed = line.trim();
  if (!trimmed) return null;

  if (trimmed.startsWith("UI_TLM,")) {
    const fields = splitCsv(trimmed.slice("UI_TLM,".length));
    return parseTlmFields(fields, {
      rssi: numberAt(fields, 16),
      snr: numberAt(fields, 17)
    });
  }

  const txMarker = "LoRa TX -> ";
  const txIndex = trimmed.indexOf(txMarker);
  if (txIndex >= 0) {
    return parseTlmFields(splitCsv(trimmed.slice(txIndex + txMarker.length)));
  }

  const rawIndex = trimmed.indexOf("TLM,");
  if (rawIndex >= 0) {
    return parseTlmFields(splitCsv(trimmed.slice(rawIndex)));
  }

  return null;
}

function parsePrettyTelemetry(line) {
  const sampleMatch = line.match(/^#(\d+)/);
  if (sampleMatch) {
    state.prettyPacket = {
      sample: Number(sampleMatch[1]),
      imuOk: false,
      magOk: false,
      baroOk: false,
      accX: NaN,
      accY: NaN,
      accZ: NaN,
      gyroX: NaN,
      gyroY: NaN,
      gyroZ: NaN,
      imuTempC: NaN,
      headingDeg: NaN,
      baroTempC: NaN,
      pressureHpa: NaN,
      altitudeM: NaN,
      rssi: NaN,
      snr: NaN
    };
    return null;
  }

  if (!state.prettyPacket) return null;

  const imuMatch = line.match(/IMU\s+\|\s+Acc\[g\]:\s+X:([-+]?\d+(?:\.\d+)?)\s+Y:([-+]?\d+(?:\.\d+)?)\s+Z:([-+]?\d+(?:\.\d+)?)\s+\|\s+Gyro\[dps\]:\s+X:([-+]?\d+(?:\.\d+)?)\s+Y:([-+]?\d+(?:\.\d+)?)\s+Z:([-+]?\d+(?:\.\d+)?)\s+\|\s+Temp:\s+([-+]?\d+(?:\.\d+)?)\s+C/);
  if (imuMatch) {
    Object.assign(state.prettyPacket, {
      imuOk: true,
      accX: Number(imuMatch[1]),
      accY: Number(imuMatch[2]),
      accZ: Number(imuMatch[3]),
      gyroX: Number(imuMatch[4]),
      gyroY: Number(imuMatch[5]),
      gyroZ: Number(imuMatch[6]),
      imuTempC: Number(imuMatch[7])
    });
    return null;
  }

  const magMatch = line.match(/MAG\s+\|\s+Heading:\s+([-+]?\d+(?:\.\d+)?)\s+deg/);
  if (magMatch) {
    Object.assign(state.prettyPacket, {
      magOk: true,
      headingDeg: Number(magMatch[1])
    });
    return null;
  }

  const baroMatch = line.match(/BARO\s+\|\s+Temp:\s+([-+]?\d+(?:\.\d+)?)\s+C\s+\|\s+Presion:\s+([-+]?\d+(?:\.\d+)?)\s+hPa\s+\|\s+Altitud:\s+([-+]?\d+(?:\.\d+)?)\s+m/);
  if (baroMatch) {
    Object.assign(state.prettyPacket, {
      baroOk: true,
      baroTempC: Number(baroMatch[1]),
      pressureHpa: Number(baroMatch[2]),
      altitudeM: Number(baroMatch[3])
    });
    return null;
  }

  const loraMatch = line.match(/LoRa\s+\|\s+RSSI:\s+(-?\d+)\s+dBm\s+\|\s+SNR:\s+([-+]?\d+(?:\.\d+)?)\s+dB/);
  if (loraMatch) {
    state.prettyPacket.rssi = Number(loraMatch[1]);
    state.prettyPacket.snr = Number(loraMatch[2]);
    const packet = state.prettyPacket;
    state.prettyPacket = null;
    return packet;
  }

  return null;
}

function estimateAttitude(packet) {
  const roll = Math.atan2(packet.accY, packet.accZ) * DEG;
  const pitch = Math.atan2(-packet.accX, Math.sqrt(packet.accY ** 2 + packet.accZ ** 2)) * DEG;
  const yaw = packet.headingDeg;
  return { roll, pitch, yaw };
}

function normalizePacket(packet) {
  const timestamp = Date.now();
  const previous = state.history.at(-1);
  const dt = previous ? Math.max(0.001, (timestamp - previous.timestamp) / 1000) : NaN;
  const verticalSpeed = previous && Number.isFinite(packet.altitudeM) && Number.isFinite(previous.altitudeM)
    ? (packet.altitudeM - previous.altitudeM) / dt
    : 0;

  return {
    ...packet,
    ...estimateAttitude(packet),
    timestamp,
    verticalSpeed
  };
}

function recordPacket(packet) {
  if (!state.missionStart) state.missionStart = Date.now();

  const normalized = normalizePacket(packet);
  if (Number.isFinite(normalized.sample) && Number.isFinite(state.lastSample) && normalized.sample > state.lastSample + 1) {
    state.packetsLost += normalized.sample - state.lastSample - 1;
  }
  if (Number.isFinite(normalized.sample)) {
    state.lastSample = normalized.sample;
  }

  state.packetsOk += 1;
  state.history.push(normalized);
  if (state.history.length > MAX_HISTORY) state.history.shift();

  state.recentTimestamps.push(normalized.timestamp);
  const minTime = normalized.timestamp - 5000;
  state.recentTimestamps = state.recentTimestamps.filter((time) => time >= minTime);

  setConnection(state.connected ? "CONECTADO" : "RECIBIENDO", "ok");
  renderTelemetry(normalized);
  drawCharts();
}

function renderTelemetry(packet) {
  els.sampleCount.textContent = Number.isFinite(packet.sample) ? String(packet.sample) : "--";
  els.packetsOk.textContent = String(state.packetsOk);
  els.packetsLost.textContent = String(state.packetsLost);
  els.packetRate.textContent = `${fixed(state.recentTimestamps.length / 5, 1)} Hz`;
  els.missionTime.textContent = elapsedTime();

  els.rollValue.textContent = fixed(packet.roll, 2);
  els.pitchValue.textContent = fixed(packet.pitch, 2);
  els.yawValue.textContent = fixed(packet.yaw, 2);

  els.gyroX.textContent = fixed(packet.gyroX, 1);
  els.gyroY.textContent = fixed(packet.gyroY, 1);
  els.gyroZ.textContent = fixed(packet.gyroZ, 1);

  els.accX.textContent = fixed(packet.accX, 2);
  els.accY.textContent = fixed(packet.accY, 2);
  els.accZ.textContent = fixed(packet.accZ, 2);

  els.headingValue.textContent = `${fixed(packet.headingDeg, 1)} deg`;
  els.rssiValue.textContent = Number.isFinite(packet.rssi) ? `${fixed(packet.rssi, 0)} dBm` : "-- dBm";
  els.snrValue.textContent = Number.isFinite(packet.snr) ? `${fixed(packet.snr, 1)} dB` : "-- dB";

  els.imuTemp.textContent = fixed(packet.imuTempC, 1);
  els.baroTemp.textContent = fixed(packet.baroTempC, 1);
  els.pressureValue.textContent = fixed(packet.pressureHpa, 1);
  els.altitudeNow.textContent = fixed(packet.altitudeM, 1);
}

function handleSerialLine(line) {
  const trimmed = line.trim();
  if (!trimmed) return;

  logLine(trimmed);
  const packet = parseMachineTelemetry(trimmed) || parsePrettyTelemetry(trimmed);
  if (packet) recordPacket(packet);
}

async function connectSerial() {
  if (state.connected) {
    await disconnectSerial();
    return;
  }

  if (!("serial" in navigator)) {
    setConnection("SIN WEB SERIAL", "bad");
    logLine("Este navegador no expone Web Serial. Usa Chrome o Edge en localhost.");
    return;
  }

  try {
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate: BAUD_RATE });
    state.reader = state.port.readable.getReader();
    state.writer = state.port.writable.getWriter();
    state.connected = true;
    stopDemo();
    els.connectSerial.textContent = "Desconectar";
    setConnection("CONECTADO", "ok");
    logLine("Puerto serial abierto a 115200 baudios.");
    readSerialLoop();
  } catch (error) {
    setConnection("ERROR SERIAL", "bad");
    logLine(`No se pudo abrir serial: ${error.message}`);
  }
}

async function readSerialLoop() {
  const decoder = new TextDecoder();
  try {
    while (state.connected && state.reader) {
      const { value, done } = await state.reader.read();
      if (done) break;
      state.serialBuffer += decoder.decode(value, { stream: true });
      const lines = state.serialBuffer.split(/\r?\n/);
      state.serialBuffer = lines.pop() || "";
      lines.forEach(handleSerialLine);
    }
  } catch (error) {
    if (state.connected) logLine(`Lectura serial interrumpida: ${error.message}`);
  } finally {
    if (state.connected) await disconnectSerial();
  }
}

async function disconnectSerial() {
  state.connected = false;
  els.connectSerial.textContent = "Conectar";

  try {
    if (state.reader) {
      await state.reader.cancel();
      state.reader.releaseLock();
    }
  } catch (error) {
    logLine(`Aviso al cerrar lector: ${error.message}`);
  }

  try {
    if (state.writer) state.writer.releaseLock();
    if (state.port) await state.port.close();
  } catch (error) {
    logLine(`Aviso al cerrar puerto: ${error.message}`);
  }

  state.reader = null;
  state.writer = null;
  state.port = null;
  setConnection("DESCONECTADO", "warn");
  logLine("Puerto serial cerrado.");
}

function demoPacket() {
  state.demoSample += 1;
  const t = state.demoSample / 2;
  const ascent = Math.min(t, 32);
  const coast = Math.max(0, t - 32);
  const altitude = Math.max(0, 16 * ascent - 0.72 * coast * coast + 26 * Math.sin(t / 8));
  const heading = (86 + t * 1.8 + Math.sin(t / 5) * 12) % 360;

  return {
    sample: state.demoSample,
    imuOk: true,
    accX: 0.08 * Math.sin(t / 2.7),
    accY: 0.04 * Math.cos(t / 4.1),
    accZ: 1 + 0.08 * Math.sin(t / 3.4),
    gyroX: 1.4 * Math.sin(t / 3.2),
    gyroY: 1.1 * Math.cos(t / 4.6),
    gyroZ: 2.4 * Math.sin(t / 6.2),
    imuTempC: 29.4 + Math.sin(t / 12) * 1.6,
    magOk: true,
    headingDeg: heading,
    baroOk: true,
    baroTempC: 27.8 - altitude / 900,
    pressureHpa: 1013.25 * Math.pow(1 - altitude / 44330, 5.255),
    altitudeM: altitude,
    rssi: -75 - Math.abs(Math.sin(t / 9) * 18),
    snr: 8.5 - Math.abs(Math.sin(t / 11) * 4)
  };
}

function startDemo() {
  if (state.demoTimer) {
    stopDemo();
    return;
  }
  if (state.connected) disconnectSerial();
  resetData();
  state.demoSample = 0;
  state.missionStart = Date.now();
  state.demoTimer = window.setInterval(() => {
    const packet = demoPacket();
    logLine(`UI_TLM,TLM,${packet.sample},1,${fixed(packet.accX, 2)},${fixed(packet.accY, 2)},${fixed(packet.accZ, 2)},${fixed(packet.gyroX, 1)},${fixed(packet.gyroY, 1)},${fixed(packet.gyroZ, 1)},${fixed(packet.imuTempC, 1)},1,${fixed(packet.headingDeg, 1)},1,${fixed(packet.baroTempC, 1)},${fixed(packet.pressureHpa, 1)},${fixed(packet.altitudeM, 1)},${fixed(packet.rssi, 0)},${fixed(packet.snr, 1)}`);
    recordPacket(packet);
  }, 500);
  els.demoMode.classList.add("active");
  setConnection("DEMO", "ok");
  logLine("Modo demo iniciado.");
}

function stopDemo() {
  if (!state.demoTimer) return;
  window.clearInterval(state.demoTimer);
  state.demoTimer = null;
  els.demoMode.classList.remove("active");
  if (!state.connected) setConnection("ESPERANDO", "warn");
  logLine("Modo demo detenido.");
}

function resetData() {
  state.packetsOk = 0;
  state.packetsLost = 0;
  state.lastSample = null;
  state.history = [];
  state.recentTimestamps = [];
  state.prettyPacket = null;
  state.missionStart = null;
  els.serialLog.replaceChildren();
  renderTelemetry({
    sample: 0,
    roll: 0,
    pitch: 0,
    yaw: 0,
    accX: 0,
    accY: 0,
    accZ: 0,
    gyroX: 0,
    gyroY: 0,
    gyroZ: 0,
    headingDeg: 0,
    imuTempC: 0,
    baroTempC: 0,
    pressureHpa: 0,
    altitudeM: 0,
    rssi: NaN,
    snr: NaN
  });
  drawCharts();
}

async function sendCommand(command) {
  const clean = command.trim().toUpperCase();
  if (!clean) return;

  els.lastCommand.textContent = clean;
  logLine(`>> ${clean}`);

  if (!state.writer) {
    logLine("Comando preparado, pero no hay puerto serial conectado.");
    return;
  }

  try {
    await state.writer.write(new TextEncoder().encode(`${clean}\n`));
    logLine(`Comando enviado por serial: ${clean}`);
  } catch (error) {
    logLine(`No se pudo enviar comando: ${error.message}`);
  }
}

function exportCsv() {
  if (state.history.length === 0) {
    logLine("No hay datos para exportar.");
    return;
  }

  const header = [
    "timestamp",
    "sample",
    "accX",
    "accY",
    "accZ",
    "gyroX",
    "gyroY",
    "gyroZ",
    "roll",
    "pitch",
    "headingDeg",
    "baroTempC",
    "pressureHpa",
    "altitudeM",
    "verticalSpeed",
    "rssi",
    "snr"
  ];
  const rows = state.history.map((packet) => [
    new Date(packet.timestamp).toISOString(),
    packet.sample,
    packet.accX,
    packet.accY,
    packet.accZ,
    packet.gyroX,
    packet.gyroY,
    packet.gyroZ,
    packet.roll,
    packet.pitch,
    packet.headingDeg,
    packet.baroTempC,
    packet.pressureHpa,
    packet.altitudeM,
    packet.verticalSpeed,
    packet.rssi,
    packet.snr
  ].join(","));

  const blob = new Blob([[header.join(","), ...rows].join("\n")], { type: "text/csv" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `telemetria-cpv-${Date.now()}.csv`;
  anchor.click();
  URL.revokeObjectURL(url);
}

function drawCharts() {
  drawChart(els.altitudeChart, state.history.map((packet) => ({
    x: packet.timestamp,
    y: packet.altitudeM
  })), {
    color: "#31d55f",
    baseline: null,
    fallbackMin: 0,
    fallbackMax: 1500
  });

  drawChart(els.velocityChart, state.history.map((packet) => ({
    x: packet.timestamp,
    y: packet.verticalSpeed
  })), {
    color: "#31d55f",
    baseline: 0,
    fallbackMin: -60,
    fallbackMax: 60
  });
}

function setupCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  const width = Math.max(320, rect.width);
  const height = Math.max(180, rect.height);
  canvas.width = Math.floor(width * scale);
  canvas.height = Math.floor(height * scale);
  const ctx = canvas.getContext("2d");
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  return { ctx, width, height };
}

function drawChart(canvas, points, options) {
  const { ctx, width, height } = setupCanvas(canvas);
  const pad = { left: 56, right: 24, top: 18, bottom: 34 };
  const chartW = width - pad.left - pad.right;
  const chartH = height - pad.top - pad.bottom;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#020404";
  ctx.fillRect(0, 0, width, height);

  const values = points.map((point) => point.y).filter(Number.isFinite);
  let minY = values.length ? Math.min(...values) : options.fallbackMin;
  let maxY = values.length ? Math.max(...values) : options.fallbackMax;
  if (options.baseline !== null) {
    minY = Math.min(minY, options.baseline);
    maxY = Math.max(maxY, options.baseline);
  }
  if (Math.abs(maxY - minY) < 1) {
    maxY += 1;
    minY -= 1;
  }
  const padY = (maxY - minY) * 0.12;
  minY -= padY;
  maxY += padY;

  const startX = points.length ? points[0].x : Date.now() - 60000;
  const endX = points.length ? points.at(-1).x : Date.now();
  const spanX = Math.max(1000, endX - startX);

  ctx.strokeStyle = "rgba(174, 181, 184, 0.7)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(pad.left, pad.top);
  ctx.lineTo(pad.left, pad.top + chartH);
  ctx.lineTo(pad.left + chartW, pad.top + chartH);
  ctx.stroke();

  ctx.strokeStyle = "rgba(174, 181, 184, 0.28)";
  ctx.fillStyle = "#cfd2d1";
  ctx.font = "14px Consolas, monospace";
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";

  for (let i = 0; i <= 4; i += 1) {
    const ratio = i / 4;
    const y = pad.top + chartH - chartH * ratio;
    const value = minY + (maxY - minY) * ratio;
    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(pad.left + chartW, y);
    ctx.stroke();
    ctx.fillText(fixed(value, 0), pad.left - 10, y);
  }

  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  for (let i = 0; i <= 4; i += 1) {
    const ratio = i / 4;
    const x = pad.left + chartW * ratio;
    const seconds = Math.round((spanX * (1 - ratio)) / 1000);
    ctx.strokeStyle = "rgba(174, 181, 184, 0.2)";
    ctx.beginPath();
    ctx.moveTo(x, pad.top);
    ctx.lineTo(x, pad.top + chartH);
    ctx.stroke();
    ctx.fillStyle = "#cfd2d1";
    ctx.fillText(`-${seconds}s`, x, pad.top + chartH + 10);
  }

  if (options.baseline !== null) {
    const zeroY = pad.top + chartH - ((options.baseline - minY) / (maxY - minY)) * chartH;
    ctx.strokeStyle = "rgba(231, 233, 234, 0.72)";
    ctx.setLineDash([6, 6]);
    ctx.beginPath();
    ctx.moveTo(pad.left, zeroY);
    ctx.lineTo(pad.left + chartW, zeroY);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  const finitePoints = points.filter((point) => Number.isFinite(point.y));
  if (finitePoints.length > 1) {
    ctx.strokeStyle = options.color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    finitePoints.forEach((point, index) => {
      const x = pad.left + ((point.x - startX) / spanX) * chartW;
      const y = pad.top + chartH - ((point.y - minY) / (maxY - minY)) * chartH;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
}

window.addEventListener("resize", drawCharts);

els.connectSerial.addEventListener("click", connectSerial);
els.demoMode.addEventListener("click", startDemo);
els.clearData.addEventListener("click", () => {
  stopDemo();
  resetData();
  setConnection(state.connected ? "CONECTADO" : "ESPERANDO", state.connected ? "ok" : "warn");
});
els.exportCsv.addEventListener("click", exportCsv);

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", () => sendCommand(button.dataset.command));
});

els.commandForm.addEventListener("submit", (event) => {
  event.preventDefault();
  sendCommand(els.commandInput.value);
  els.commandInput.value = "";
});

window.setInterval(() => {
  els.missionTime.textContent = elapsedTime();
}, 500);

resetData();
logLine("UI lista. Conecta la estacion terrena por USB o activa Demo.");
