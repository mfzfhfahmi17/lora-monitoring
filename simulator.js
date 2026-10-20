'use strict';

require('dotenv').config();

const PORT = Number(process.env.PORT || 3000);
const API_URL = String(
  process.env.SIMULATOR_API_URL || `http://127.0.0.1:${PORT}/api/telemetry`
).trim();
const HEALTH_URL = new URL('/api/health', API_URL).toString();
const DEVICE_API_KEY = String(process.env.DEVICE_API_KEY || '').trim();
const INTERVAL_MS = Math.max(1000, Number(process.env.SIMULATOR_INTERVAL_MS || 3000));

let step = 0;
const baseLatitude = -6.5971;
const baseLongitude = 106.8060;

function getErrorDetail(error) {
  const details = [error?.message];
  if (error?.cause?.code) details.push(`code=${error.cause.code}`);
  if (error?.cause?.address) details.push(`address=${error.cause.address}`);
  if (error?.cause?.port) details.push(`port=${error.cause.port}`);
  return details.filter(Boolean).join(' | ');
}

async function checkBackend() {
  try {
    const response = await fetch(HEALTH_URL, {
      signal: AbortSignal.timeout(5000)
    });
    const text = await response.text();

    if (!response.ok) {
      throw new Error(`Health API HTTP ${response.status}: ${text.slice(0, 200)}`);
    }

    console.log(`Backend terhubung: ${HEALTH_URL}`);
    return true;
  } catch (error) {
    console.error(`Backend tidak dapat dijangkau: ${getErrorDetail(error)}`);
    console.error(`Pastikan \"npm.cmd start\" aktif dan buka ${HEALTH_URL} di browser.`);
    return false;
  }
}

async function sendTelemetry() {
  step += 1;
  const alert = step % 7 === 0 || step % 7 === 1;
  const latitude = baseLatitude + Math.sin(step / 4) * 0.0012;
  const longitude = baseLongitude + Math.cos(step / 4) * 0.0012;

  const payload = {
    nodeId: 'NODE-DEMO-01',
    senderTime: new Date().toLocaleTimeString('id-ID', { hour12: false }),
    latitude,
    longitude,
    trashStatus: alert ? 'ADA' : 'TIDAK ADA',
    servoAngle: (step * 15) % 181,
    objectDistanceCm: alert ? 18 + Math.random() * 12 : 80 + Math.random() * 40,
    rssi: -65 - Math.floor(Math.random() * 35),
    rawPayload: `NODE-DEMO-01|SIMULATOR|${latitude}|${longitude}|${alert ? 'ADA' : 'TIDAK ADA'}`
  };

  try {
    const response = await fetch(API_URL, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-Device-Key': DEVICE_API_KEY
      },
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(8000)
    });

    const text = await response.text();
    let result;
    try {
      result = JSON.parse(text);
    } catch {
      result = { message: text || 'Respons kosong' };
    }

    if (!response.ok) {
      console.error(
        `${new Date().toISOString()} HTTP ${response.status}:`,
        result.message || result.errors || result
      );
      return;
    }

    console.log(
      `${new Date().toISOString()} HTTP ${response.status}:`,
      result.message || 'Telemetry terkirim.'
    );
  } catch (error) {
    console.error(`Simulator gagal: ${getErrorDetail(error)}`);
  }
}

async function main() {
  if (!DEVICE_API_KEY || DEVICE_API_KEY.includes('GANTI_DENGAN')) {
    console.error('Isi DEVICE_API_KEY pada file .env terlebih dahulu.');
    process.exit(1);
  }

  console.log(`API simulator : ${API_URL}`);
  console.log(`Interval      : ${INTERVAL_MS} ms`);

  const backendReady = await checkBackend();
  if (!backendReady) process.exit(1);

  await sendTelemetry();
  setInterval(sendTelemetry, INTERVAL_MS);
}

main().catch((error) => {
  console.error(`Simulator berhenti: ${getErrorDetail(error)}`);
  process.exit(1);
});
