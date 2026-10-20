'use strict';

require('dotenv').config();

const path = require('path');
const express = require('express');
const { createClient } = require('@supabase/supabase-js');

const PORT = Number(process.env.PORT || 3000);
const ONLINE_TIMEOUT_SECONDS = Math.max(
  5,
  Number(process.env.ONLINE_TIMEOUT_SECONDS || 12)
);
const DEVICE_API_KEY = String(process.env.DEVICE_API_KEY || '').trim();
const APP_TIMEZONE = String(process.env.APP_TIMEZONE || 'Asia/Jakarta').trim();

const SUPABASE_URL = String(process.env.SUPABASE_URL || '').trim();
const SUPABASE_KEY = String(
  process.env.SUPABASE_SERVICE_ROLE_KEY || process.env.SUPABASE_ANON_KEY || ''
).trim();

if (!SUPABASE_URL || !SUPABASE_KEY) {
  console.warn('[PERINGATAN] SUPABASE_URL atau SUPABASE_KEY belum diisi pada file .env.');
}

// Inisialisasi client Supabase
const supabase = createClient(SUPABASE_URL, SUPABASE_KEY, {
  auth: { persistSession: false }
});

function nullableNumber(value) {
  if (value === null || value === undefined || value === '') return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function normalizeStatus(value) {
  const status = String(value || '').trim().toUpperCase();
  if (status === 'ADA') return 'ADA';
  if (['TIDAK ADA', 'TIDAK_ADA', 'TIDAKADA', 'NONE', 'AMAN'].includes(status)) {
    return 'TIDAK ADA';
  }
  return null;
}

function formatNode(row) {
  const now = Date.now();
  const lastSeenMs = row.last_seen_at ? new Date(row.last_seen_at).getTime() : 0;
  const secondsSinceSeen = lastSeenMs > 0 ? Math.floor((now - lastSeenMs) / 1000) : 999999;
  const isOnline = secondsSinceSeen <= ONLINE_TIMEOUT_SECONDS;

  return {
    id: Number(row.id),
    nodeId: row.node_id,
    nodeName: row.node_name || row.node_id,
    latitude: nullableNumber(row.latitude),
    longitude: nullableNumber(row.longitude),
    trashStatus: row.trash_status || 'TIDAK ADA',
    servoAngle: nullableNumber(row.servo_angle),
    objectDistanceCm: nullableNumber(row.object_distance_cm),
    rssi: nullableNumber(row.rssi_dbm),
    senderTime: row.sender_time,
    lastSeenAt: row.last_seen_at,
    secondsSinceSeen,
    connectionStatus: isOnline ? 'ONLINE' : 'OFFLINE'
  };
}

function formatTelemetry(row) {
  return {
    id: Number(row.id),
    nodeId: row.node_id,
    senderTime: row.sender_time,
    latitude: Number(row.latitude),
    longitude: Number(row.longitude),
    trashStatus: row.trash_status,
    servoAngle: nullableNumber(row.servo_angle),
    objectDistanceCm: nullableNumber(row.object_distance_cm),
    rssi: nullableNumber(row.rssi_dbm),
    receivedAt: row.received_at,
    rawPayload: row.raw_payload || null
  };
}

function requireDeviceKey(req, res, next) {
  const key = String(req.get('x-device-key') || '');
  if (!DEVICE_API_KEY || key !== DEVICE_API_KEY) {
    return res.status(401).json({
      success: false,
      message: 'Device key tidak valid.'
    });
  }
  return next();
}

function validateTelemetry(body) {
  const nodeId = String(body.nodeId || '').trim();
  const latitude = Number(body.latitude);
  const longitude = Number(body.longitude);
  const trashStatus = normalizeStatus(body.trashStatus);
  const servoAngle = nullableNumber(body.servoAngle);
  const objectDistanceCm = nullableNumber(body.objectDistanceCm);
  const rssi = nullableNumber(body.rssi);
  const senderTime = body.senderTime == null
    ? null
    : String(body.senderTime).trim().slice(0, 30);
  const rawPayload = body.rawPayload == null
    ? null
    : String(body.rawPayload).slice(0, 2000);

  const errors = [];
  if (!/^[A-Za-z0-9_-]{1,100}$/.test(nodeId)) {
    errors.push('nodeId wajib 1-100 karakter: huruf, angka, underscore, atau strip.');
  }
  if (!Number.isFinite(latitude) || latitude < -90 || latitude > 90) {
    errors.push('latitude tidak valid.');
  }
  if (!Number.isFinite(longitude) || longitude < -180 || longitude > 180) {
    errors.push('longitude tidak valid.');
  }
  if (!trashStatus) errors.push('trashStatus harus ADA atau TIDAK ADA.');
  if (servoAngle !== null && (servoAngle < 0 || servoAngle > 180)) {
    errors.push('servoAngle harus berada pada 0-180 derajat.');
  }
  if (objectDistanceCm !== null && objectDistanceCm < 0) {
    errors.push('objectDistanceCm tidak boleh negatif.');
  }

  return {
    errors,
    data: {
      nodeId,
      senderTime,
      latitude,
      longitude,
      trashStatus,
      servoAngle,
      objectDistanceCm,
      rssi,
      rawPayload
    }
  };
}

const app = express();

app.disable('x-powered-by');
app.use(express.json({ limit: '64kb' }));
app.use(express.urlencoded({ extended: false }));
app.use(express.static(path.join(__dirname, 'public')));

// Konfigurasi frontend untuk Supabase Realtime
app.get('/api/config', (_req, res) => {
  res.json({
    supabaseUrl: SUPABASE_URL,
    supabaseAnonKey: process.env.SUPABASE_ANON_KEY || '',
    onlineTimeoutSeconds: ONLINE_TIMEOUT_SECONDS,
    appTimezone: APP_TIMEZONE
  });
});

app.get('/api/health', async (_req, res) => {
  try {
    const { error } = await supabase.from('nodes').select('id').limit(1);
    if (error) throw error;

    res.json({
      success: true,
      service: 'lora-monitoring-api-supabase',
      database: 'Supabase PostgreSQL',
      mapProvider: 'OpenFreeMap',
      onlineTimeoutSeconds: ONLINE_TIMEOUT_SECONDS,
      serverTime: new Date().toISOString(),
      appTimezone: APP_TIMEZONE
    });
  } catch (error) {
    res.status(503).json({ success: false, message: error.message });
  }
});

app.get('/api/nodes', async (_req, res, next) => {
  try {
    const { data: rows, error } = await supabase
      .from('nodes')
      .select('*')
      .order('last_seen_at', { ascending: false });

    if (error) throw error;
    res.json({ success: true, data: (rows || []).map(formatNode) });
  } catch (error) {
    next(error);
  }
});

app.get('/api/nodes/:nodeId/history', async (req, res, next) => {
  try {
    const nodeId = String(req.params.nodeId || '').trim();
    const requestedLimit = Number(req.query.limit || 200);
    const limit = Math.min(1000, Math.max(1, Number.isFinite(requestedLimit) ? requestedLimit : 200));

    const { data: rows, error } = await supabase
      .from('telemetry')
      .select('*')
      .eq('node_id', nodeId)
      .order('received_at', { ascending: false })
      .limit(limit);

    if (error) throw error;
    res.json({
      success: true,
      data: (rows || []).reverse().map(formatTelemetry)
    });
  } catch (error) {
    next(error);
  }
});

app.get('/api/telemetry/latest', async (req, res, next) => {
  try {
    const requestedLimit = Number(req.query.limit || 100);
    const limit = Math.min(1000, Math.max(1, Number.isFinite(requestedLimit) ? requestedLimit : 100));

    const { data: rows, error } = await supabase
      .from('telemetry')
      .select('*')
      .order('received_at', { ascending: false })
      .limit(limit);

    if (error) throw error;
    res.json({ success: true, data: (rows || []).map(formatTelemetry) });
  } catch (error) {
    next(error);
  }
});

app.get('/api/stats', async (_req, res, next) => {
  try {
    const { data: nodes, error: nodeErr } = await supabase
      .from('nodes')
      .select('*');
    if (nodeErr) throw nodeErr;

    const { count: totalTelemetry, error: telemErr } = await supabase
      .from('telemetry')
      .select('*', { count: 'exact', head: true });
    if (telemErr) throw telemErr;

    const { count: totalAlerts, error: alertErr } = await supabase
      .from('telemetry')
      .select('*', { count: 'exact', head: true })
      .eq('trash_status', 'ADA');
    if (alertErr) throw alertErr;

    const now = Date.now();
    let onlineNodes = 0;
    let alertNodes = 0;

    (nodes || []).forEach((n) => {
      const lastSeenMs = n.last_seen_at ? new Date(n.last_seen_at).getTime() : 0;
      if (Math.floor((now - lastSeenMs) / 1000) <= ONLINE_TIMEOUT_SECONDS) {
        onlineNodes += 1;
      }
      if (n.trash_status === 'ADA') {
        alertNodes += 1;
      }
    });

    res.json({
      success: true,
      data: {
        totalNodes: nodes ? nodes.length : 0,
        onlineNodes,
        alertNodes,
        totalTelemetry: totalTelemetry || 0,
        totalAlerts: totalAlerts || 0
      }
    });
  } catch (error) {
    next(error);
  }
});

app.get('/api/export.csv', async (_req, res, next) => {
  try {
    const { data: rows, error } = await supabase
      .from('telemetry')
      .select('id, node_id, sender_time, latitude, longitude, trash_status, servo_angle, object_distance_cm, rssi_dbm, received_at')
      .order('received_at', { ascending: false })
      .limit(10000);

    if (error) throw error;

    const columns = [
      'id', 'node_id', 'sender_time', 'latitude', 'longitude',
      'trash_status', 'servo_angle', 'object_distance_cm', 'rssi_dbm', 'received_at'
    ];

    const csvEscape = (value) => {
      if (value === null || value === undefined) return '';
      const text = String(value).replaceAll('"', '""');
      return `"${text}"`;
    };

    const csv = [
      columns.join(','),
      ...(rows || []).map((row) => columns.map((column) => csvEscape(row[column])).join(','))
    ].join('\n');

    res.setHeader('Content-Type', 'text/csv; charset=utf-8');
    res.setHeader('Content-Disposition', 'attachment; filename="telemetry-lora.csv"');
    res.send(`\uFEFF${csv}`);
  } catch (error) {
    next(error);
  }
});

app.post('/api/telemetry', requireDeviceKey, async (req, res, next) => {
  const { errors, data } = validateTelemetry(req.body);
  if (errors.length > 0) {
    return res.status(400).json({ success: false, errors });
  }

  const nowIso = new Date().toISOString();

  try {
    // 1. Upsert ke tabel nodes
    const { error: nodeError } = await supabase.from('nodes').upsert(
      {
        node_id: data.nodeId,
        latitude: data.latitude,
        longitude: data.longitude,
        trash_status: data.trashStatus,
        servo_angle: data.servoAngle,
        object_distance_cm: data.objectDistanceCm,
        rssi_dbm: data.rssi,
        sender_time: data.senderTime,
        last_seen_at: nowIso,
        updated_at: nowIso
      },
      { onConflict: 'node_id' }
    );

    if (nodeError) throw nodeError;

    // 2. Insert ke tabel telemetry
    const { data: savedRows, error: telemError } = await supabase
      .from('telemetry')
      .insert({
        node_id: data.nodeId,
        sender_time: data.senderTime,
        latitude: data.latitude,
        longitude: data.longitude,
        trash_status: data.trashStatus,
        servo_angle: data.servoAngle,
        object_distance_cm: data.objectDistanceCm,
        rssi_dbm: data.rssi,
        raw_payload: data.rawPayload,
        received_at: nowIso
      })
      .select();

    if (telemError) throw telemError;

    const saved = savedRows && savedRows.length > 0
      ? formatTelemetry(savedRows[0])
      : {
          nodeId: data.nodeId,
          latitude: data.latitude,
          longitude: data.longitude,
          trashStatus: data.trashStatus,
          servoAngle: data.servoAngle,
          objectDistanceCm: data.objectDistanceCm,
          rssi: data.rssi,
          senderTime: data.senderTime,
          receivedAt: nowIso
        };

    return res.status(201).json({
      success: true,
      message: 'Telemetry berhasil disimpan ke Supabase.',
      data: saved
    });
  } catch (error) {
    return next(error);
  }
});

app.use((req, res) => {
  res.status(404).json({ success: false, message: 'Endpoint tidak ditemukan.' });
});

app.use((error, _req, res, _next) => {
  console.error('[SERVER ERROR]', error);
  res.status(500).json({
    success: false,
    message: 'Terjadi kesalahan pada server.',
    detail: process.env.NODE_ENV === 'production' ? undefined : error.message
  });
});

if (process.env.NODE_ENV !== 'test' && !process.env.VERCEL) {
  app.listen(PORT, '0.0.0.0', () => {
    console.log('');
    console.log('Backend LoRa Monitoring (Supabase) aktif');
    console.log(`Dashboard : http://localhost:${PORT}`);
    console.log(`Health API: http://localhost:${PORT}/api/health`);
    console.log(`Database  : ${SUPABASE_URL}`);
    console.log('Realtime  : Supabase Realtime Channel');
    console.log('');
  });
}

module.exports = app;
