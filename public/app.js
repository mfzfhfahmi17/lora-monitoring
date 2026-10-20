'use strict';

const state = {
  map: null,
  mapReady: false,
  nodes: new Map(),
  markers: new Map(),
  history: [],
  selectedNodeId: null,
  routeLayer: null,
  hasFittedNodes: false,
  onlineTimeoutSeconds: 12,
  appTimezone: 'Asia/Jakarta',
  serverClockOffsetMs: 0,
  socketConnected: false,
  totalTelemetry: 0,
  autoSyncRunning: false
};

const elements = {
  socketDot: document.getElementById('socketDot'),
  socketStatus: document.getElementById('socketStatus'),
  serverClock: document.getElementById('serverClock'),
  mapStatus: document.getElementById('mapStatus'),
  totalNodes: document.getElementById('totalNodes'),
  onlineNodes: document.getElementById('onlineNodes'),
  alertNodes: document.getElementById('alertNodes'),
  totalTelemetry: document.getElementById('totalTelemetry'),
  nodesTableBody: document.getElementById('nodesTableBody'),
  historyTableBody: document.getElementById('historyTableBody'),
  nodeSearch: document.getElementById('nodeSearch'),
  refreshButton: document.getElementById('refreshButton'),
  fitNodesButton: document.getElementById('fitNodesButton'),
  selectedNodeTitle: document.getElementById('selectedNodeTitle'),
  selectedNodeBadge: document.getElementById('selectedNodeBadge'),
  emptySelection: document.getElementById('emptySelection'),
  selectedNodeDetails: document.getElementById('selectedNodeDetails'),
  detailTrash: document.getElementById('detailTrash'),
  detailDistance: document.getElementById('detailDistance'),
  detailServo: document.getElementById('detailServo'),
  detailRssi: document.getElementById('detailRssi'),
  detailCoordinates: document.getElementById('detailCoordinates'),
  detailLastSeen: document.getElementById('detailLastSeen'),
  showRouteButton: document.getElementById('showRouteButton'),
  toast: document.getElementById('toast')
};

function showToast(message, type = 'normal') {
  elements.toast.textContent = message;
  elements.toast.className = `toast show${type === 'error' ? ' error' : ''}`;
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => {
    elements.toast.className = 'toast';
  }, 3800);
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function isValidCoordinate(latitude, longitude) {
  return Number.isFinite(Number(latitude)) &&
    Number.isFinite(Number(longitude)) &&
    Number(latitude) >= -90 && Number(latitude) <= 90 &&
    Number(longitude) >= -180 && Number(longitude) <= 180;
}

function formatDate(value) {
  if (!value) return '-';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return String(value);
  return new Intl.DateTimeFormat('id-ID', {
    dateStyle: 'medium',
    timeStyle: 'medium',
    timeZone: state.appTimezone
  }).format(date);
}

function formatNumber(value, digits = 1) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(digits) : '-';
}

function isNodeOnline(node) {
  if (!node.lastSeenAt) return false;
  const ageSeconds = (Date.now() - new Date(node.lastSeenAt).getTime()) / 1000;
  return ageSeconds <= state.onlineTimeoutSeconds;
}

function connectionStatus(node) {
  return isNodeOnline(node) ? 'ONLINE' : 'OFFLINE';
}

function markerClass(node) {
  if (!isNodeOnline(node)) return 'node-marker--offline';
  return node.trashStatus === 'ADA' ? 'node-marker--alert' : 'node-marker--normal';
}

function statusLabel(node) {
  if (!isNodeOnline(node)) {
    return '<span class="status-label status-label--offline">OFFLINE</span>';
  }
  if (node.trashStatus === 'ADA') {
    return '<span class="status-label status-label--alert">ADA</span>';
  }
  return '<span class="status-label status-label--normal">TIDAK ADA</span>';
}

function createMarkerIcon(node) {
  return L.divIcon({
    className: '',
    html: `<div class="node-marker ${markerClass(node)}"></div>`,
    iconSize: [24, 24],
    iconAnchor: [12, 12],
    popupAnchor: [0, -15]
  });
}

function popupContent(node) {
  const lat = Number(node.latitude);
  const lon = Number(node.longitude);
  const osmUrl = `https://www.openstreetmap.org/?mlat=${lat}&mlon=${lon}#map=18/${lat}/${lon}`;
  return `
    <div class="popup-title"><strong>${escapeHtml(node.nodeName || node.nodeId)}</strong></div>
    <div class="popup-grid">
      <span>Koneksi</span><strong>${connectionStatus(node)}</strong>
      <span>Deteksi</span><strong>${escapeHtml(node.trashStatus)}</strong>
      <span>Jarak</span><strong>${formatNumber(node.objectDistanceCm)} cm</strong>
      <span>Sudut</span><strong>${formatNumber(node.servoAngle, 0)} derajat</strong>
      <span>RSSI</span><strong>${formatNumber(node.rssi, 0)} dBm</strong>
    </div>
    <a class="popup-link" href="${osmUrl}" target="_blank" rel="noopener">Buka koordinat</a>
  `;
}

function initMap() {
  try {
    if (!window.L) throw new Error('Library Leaflet gagal dimuat.');

    state.map = L.map('map', { zoomControl: true }).setView([-2.5, 118], 5);

    // Layer raster ini menjadi cadangan jika OpenFreeMap atau plugin MapLibre gagal.
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      attribution: '&copy; OpenStreetMap contributors'
    }).addTo(state.map);

    if (typeof L.maplibreGL === 'function') {
      L.maplibreGL({
        style: 'https://tiles.openfreemap.org/styles/liberty'
      }).addTo(state.map);
      elements.mapStatus.textContent = 'OpenFreeMap aktif';
    } else {
      elements.mapStatus.textContent = 'OSM fallback aktif';
    }

    state.mapReady = true;
    setTimeout(() => state.map.invalidateSize(), 200);
  } catch (error) {
    elements.mapStatus.textContent = 'Peta gagal';
    document.getElementById('map').innerHTML = `
      <div class="empty-state">
        ${escapeHtml(error.message)}<br>
        Dashboard dan database tetap dapat digunakan.
      </div>`;
    showToast(error.message, 'error');
  }
}

function upsertMarker(node) {
  if (!state.mapReady || !isValidCoordinate(node.latitude, node.longitude)) return;
  const position = [Number(node.latitude), Number(node.longitude)];
  let marker = state.markers.get(node.nodeId);

  if (!marker) {
    marker = L.marker(position, { icon: createMarkerIcon(node) })
      .addTo(state.map)
      .bindPopup(popupContent(node));
    marker.on('click', () => selectNode(node.nodeId, false));
    state.markers.set(node.nodeId, marker);
  } else {
    marker.setLatLng(position);
    marker.setIcon(createMarkerIcon(node));
    marker.setPopupContent(popupContent(node));
  }
}

function refreshAllMarkers() {
  for (const node of state.nodes.values()) upsertMarker(node);
}

function fitAllNodes() {
  if (!state.mapReady) return;
  const positions = [...state.nodes.values()]
    .filter((node) => isValidCoordinate(node.latitude, node.longitude))
    .map((node) => [Number(node.latitude), Number(node.longitude)]);

  if (positions.length === 0) {
    showToast('Belum ada koordinat node.', 'error');
    return;
  }
  if (positions.length === 1) {
    state.map.setView(positions[0], 16);
    return;
  }
  state.map.fitBounds(positions, { padding: [45, 45], maxZoom: 17 });
}

function renderNodesTable() {
  const query = elements.nodeSearch.value.trim().toLowerCase();
  const nodes = [...state.nodes.values()]
    .filter((node) => `${node.nodeId} ${node.nodeName}`.toLowerCase().includes(query))
    .sort((a, b) => new Date(b.lastSeenAt || 0) - new Date(a.lastSeenAt || 0));

  if (nodes.length === 0) {
    elements.nodesTableBody.innerHTML = '<tr><td colspan="7" class="table-empty">Node tidak ditemukan.</td></tr>';
    return;
  }

  elements.nodesTableBody.innerHTML = nodes.map((node) => `
    <tr data-node-id="${escapeHtml(node.nodeId)}">
      <td><strong>${escapeHtml(node.nodeName || node.nodeId)}</strong><br><small>${escapeHtml(node.nodeId)}</small></td>
      <td><span class="status-label ${isNodeOnline(node) ? 'status-label--normal' : 'status-label--offline'}">${connectionStatus(node)}</span></td>
      <td>${statusLabel(node)}</td>
      <td>${formatNumber(node.objectDistanceCm)} cm</td>
      <td>${formatNumber(node.servoAngle, 0)} derajat</td>
      <td>${formatNumber(node.rssi, 0)} dBm</td>
      <td>${formatDate(node.lastSeenAt)}</td>
    </tr>
  `).join('');

  elements.nodesTableBody.querySelectorAll('tr[data-node-id]').forEach((row) => {
    row.addEventListener('click', () => selectNode(row.dataset.nodeId, true));
  });
}

function renderHistoryTable() {
  if (state.history.length === 0) {
    elements.historyTableBody.innerHTML = '<tr><td colspan="8" class="table-empty">Belum ada telemetry.</td></tr>';
    return;
  }

  elements.historyTableBody.innerHTML = state.history.slice(0, 100).map((item) => `
    <tr data-node-id="${escapeHtml(item.nodeId)}">
      <td>${formatDate(item.receivedAt)}</td>
      <td>${escapeHtml(item.senderTime || '-')}</td>
      <td><strong>${escapeHtml(item.nodeId)}</strong></td>
      <td>${formatNumber(item.latitude, 6)}, ${formatNumber(item.longitude, 6)}</td>
      <td>${item.trashStatus === 'ADA'
        ? '<span class="status-label status-label--alert">ADA</span>'
        : '<span class="status-label status-label--normal">TIDAK ADA</span>'}</td>
      <td>${formatNumber(item.objectDistanceCm)} cm</td>
      <td>${formatNumber(item.servoAngle, 0)} derajat</td>
      <td>${formatNumber(item.rssi, 0)} dBm</td>
    </tr>
  `).join('');

  elements.historyTableBody.querySelectorAll('tr[data-node-id]').forEach((row) => {
    row.addEventListener('click', () => selectNode(row.dataset.nodeId, true));
  });
}

function renderSelectedNode() {
  const node = state.nodes.get(state.selectedNodeId);
  if (!node) {
    elements.selectedNodeTitle.textContent = 'Belum dipilih';
    elements.selectedNodeBadge.textContent = '-';
    elements.selectedNodeBadge.className = 'badge badge--offline';
    elements.emptySelection.classList.remove('hidden');
    elements.selectedNodeDetails.classList.add('hidden');
    elements.showRouteButton.disabled = true;
    return;
  }

  elements.selectedNodeTitle.textContent = node.nodeName || node.nodeId;
  elements.selectedNodeBadge.textContent = connectionStatus(node);
  elements.selectedNodeBadge.className = `badge ${isNodeOnline(node) ? 'badge--online' : 'badge--offline'}`;
  if (node.trashStatus === 'ADA' && isNodeOnline(node)) {
    elements.selectedNodeBadge.textContent = 'PERINGATAN';
    elements.selectedNodeBadge.className = 'badge badge--alert';
  }
  elements.emptySelection.classList.add('hidden');
  elements.selectedNodeDetails.classList.remove('hidden');
  elements.detailTrash.textContent = node.trashStatus;
  elements.detailDistance.textContent = `${formatNumber(node.objectDistanceCm)} cm`;
  elements.detailServo.textContent = `${formatNumber(node.servoAngle, 0)} derajat`;
  elements.detailRssi.textContent = `${formatNumber(node.rssi, 0)} dBm`;
  elements.detailCoordinates.textContent = `${formatNumber(node.latitude, 6)}, ${formatNumber(node.longitude, 6)}`;
  elements.detailLastSeen.textContent = formatDate(node.lastSeenAt);
  elements.showRouteButton.disabled = false;
}

function selectNode(nodeId, centerMap = true) {
  const node = state.nodes.get(nodeId);
  if (!node) return;
  state.selectedNodeId = nodeId;
  renderSelectedNode();

  if (centerMap && state.mapReady && isValidCoordinate(node.latitude, node.longitude)) {
    state.map.setView([Number(node.latitude), Number(node.longitude)], 17);
    const marker = state.markers.get(nodeId);
    if (marker) marker.openPopup();
  }
}

async function loadRoute() {
  if (!state.selectedNodeId || !state.mapReady) return;
  elements.showRouteButton.disabled = true;
  elements.showRouteButton.textContent = 'Memuat jalur...';
  try {
    const response = await fetch(`/api/nodes/${encodeURIComponent(state.selectedNodeId)}/history?limit=300`);
    const result = await response.json();
    if (!response.ok || !result.success) throw new Error(result.message || 'Gagal mengambil riwayat.');

    const points = result.data
      .filter((item) => isValidCoordinate(item.latitude, item.longitude))
      .map((item) => [Number(item.latitude), Number(item.longitude)]);

    if (state.routeLayer) state.routeLayer.remove();
    if (points.length < 2) {
      showToast('Jalur membutuhkan minimal dua titik koordinat.', 'error');
      return;
    }

    state.routeLayer = L.polyline(points, {
      color: '#177c51',
      weight: 4,
      opacity: 0.85
    }).addTo(state.map);
    state.map.fitBounds(state.routeLayer.getBounds(), { padding: [40, 40], maxZoom: 18 });
    showToast(`Jalur ${state.selectedNodeId} ditampilkan.`);
  } catch (error) {
    showToast(error.message, 'error');
  } finally {
    elements.showRouteButton.disabled = false;
    elements.showRouteButton.textContent = 'Tampilkan jalur riwayat';
  }
}

function updateStatsLocally() {
  const nodes = [...state.nodes.values()];
  elements.totalNodes.textContent = String(nodes.length);
  elements.onlineNodes.textContent = String(nodes.filter(isNodeOnline).length);
  elements.alertNodes.textContent = String(nodes.filter((node) => isNodeOnline(node) && node.trashStatus === 'ADA').length);
}

async function fetchJson(url) {
  const response = await fetch(url, { headers: { Accept: 'application/json' } });
  const result = await response.json();
  if (!response.ok || !result.success) throw new Error(result.message || `HTTP ${response.status}`);
  return result.data;
}

async function loadAllData(showMessage = false) {
  elements.refreshButton.disabled = true;
  try {
    const [healthResponse, nodes, history, stats] = await Promise.all([
      fetch('/api/health').then((response) => response.json()),
      fetchJson('/api/nodes'),
      fetchJson('/api/telemetry/latest?limit=100'),
      fetchJson('/api/stats')
    ]);

    if (healthResponse.success) {
      state.onlineTimeoutSeconds = Number(healthResponse.onlineTimeoutSeconds || 12);
      state.appTimezone = healthResponse.appTimezone || 'Asia/Jakarta';
      if (healthResponse.serverTime) {
        state.serverClockOffsetMs = new Date(healthResponse.serverTime).getTime() - Date.now();
      }
    }

    state.nodes.clear();
    nodes.forEach((node) => {
      state.nodes.set(node.nodeId, node);
      upsertMarker(node);
    });
    state.history = history;

    elements.totalNodes.textContent = stats.totalNodes.toLocaleString('id-ID');
    elements.onlineNodes.textContent = stats.onlineNodes.toLocaleString('id-ID');
    elements.alertNodes.textContent = stats.alertNodes.toLocaleString('id-ID');
    state.totalTelemetry = Number(stats.totalTelemetry || 0);
    elements.totalTelemetry.textContent = state.totalTelemetry.toLocaleString('id-ID');

    renderNodesTable();
    renderHistoryTable();
    renderSelectedNode();

    if (!state.hasFittedNodes && nodes.length > 0) {
      fitAllNodes();
      state.hasFittedNodes = true;
    }
    if (showMessage) showToast('Data berhasil diperbarui.');
  } catch (error) {
    showToast(`Gagal mengambil data: ${error.message}`, 'error');
  } finally {
    elements.refreshButton.disabled = false;
  }
}

function handleRealtimeTelemetry(item) {
  const previous = state.nodes.get(item.nodeId) || {};
  const node = {
    ...previous,
    nodeId: item.nodeId,
    nodeName: previous.nodeName || item.nodeId,
    latitude: item.latitude,
    longitude: item.longitude,
    trashStatus: item.trashStatus,
    servoAngle: item.servoAngle,
    objectDistanceCm: item.objectDistanceCm,
    rssi: item.rssi,
    senderTime: item.senderTime,
    lastSeenAt: item.receivedAt,
    connectionStatus: 'ONLINE'
  };

  state.nodes.set(node.nodeId, node);
  state.history.unshift(item);
  state.history = state.history.slice(0, 100);
  upsertMarker(node);
  renderNodesTable();
  renderHistoryTable();
  updateStatsLocally();
  state.totalTelemetry += 1;
  elements.totalTelemetry.textContent = state.totalTelemetry.toLocaleString('id-ID');
  if (state.selectedNodeId === node.nodeId) renderSelectedNode();

  if (node.trashStatus === 'ADA') {
    showToast(`Peringatan: sampah terdeteksi oleh ${node.nodeId}.`, 'error');
  }
}

function formatTelemetryPayload(item) {
  return {
    id: Number(item.id || 0),
    nodeId: item.node_id || item.nodeId,
    senderTime: item.sender_time || item.senderTime,
    latitude: Number(item.latitude),
    longitude: Number(item.longitude),
    trashStatus: item.trash_status || item.trashStatus,
    servoAngle: item.servo_angle != null ? Number(item.servo_angle) : (item.servoAngle != null ? Number(item.servoAngle) : null),
    objectDistanceCm: item.object_distance_cm != null ? Number(item.object_distance_cm) : (item.objectDistanceCm != null ? Number(item.objectDistanceCm) : null),
    rssi: item.rssi_dbm != null ? Number(item.rssi_dbm) : (item.rssi != null ? Number(item.rssi) : null),
    receivedAt: item.received_at || item.receivedAt || new Date().toISOString(),
    rawPayload: item.raw_payload || item.rawPayload || null
  };
}

async function initRealtime() {
  try {
    const res = await fetch('/api/config');
    const config = await res.json();
    if (config.onlineTimeoutSeconds) state.onlineTimeoutSeconds = config.onlineTimeoutSeconds;
    if (config.appTimezone) state.appTimezone = config.appTimezone;

    if (!window.supabase || !config.supabaseUrl || !config.supabaseAnonKey) {
      elements.socketDot.className = 'status-dot status-dot--pending';
      elements.socketStatus.textContent = 'Auto-Sync Aktif';
      return;
    }

    const client = window.supabase.createClient(config.supabaseUrl, config.supabaseAnonKey);

    client
      .channel('realtime:telemetry_feed')
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'telemetry' }, (payload) => {
        if (payload?.new) {
          const item = formatTelemetryPayload(payload.new);
          handleRealtimeTelemetry(item);
        }
      })
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          state.socketConnected = true;
          elements.socketDot.className = 'status-dot status-dot--online';
          elements.socketStatus.textContent = 'Supabase Realtime';
        } else if (status === 'CHANNEL_ERROR' || status === 'TIMED_OUT') {
          state.socketConnected = false;
          elements.socketDot.className = 'status-dot status-dot--offline';
          elements.socketStatus.textContent = 'Realtime terputus';
        }
      });
  } catch (error) {
    console.warn('Realtime init error:', error);
    elements.socketDot.className = 'status-dot status-dot--pending';
    elements.socketStatus.textContent = 'Mode Polling';
  }
}

function initEvents() {
  elements.refreshButton.addEventListener('click', () => loadAllData(true));
  elements.fitNodesButton.addEventListener('click', fitAllNodes);
  elements.nodeSearch.addEventListener('input', renderNodesTable);
  elements.showRouteButton.addEventListener('click', loadRoute);

  document.querySelectorAll('.menu-item[data-target]').forEach((button) => {
    button.addEventListener('click', () => {
      document.querySelectorAll('.menu-item').forEach((item) => item.classList.remove('active'));
      button.classList.add('active');
      document.getElementById(button.dataset.target)?.scrollIntoView({ behavior: 'smooth' });
    });
  });
}

function startClock() {
  const update = () => {
    const serverNow = new Date(Date.now() + state.serverClockOffsetMs);
    elements.serverClock.textContent = new Intl.DateTimeFormat('id-ID', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false,
      timeZone: state.appTimezone
    }).format(serverNow) + ' WIB';
  };
  update();
  setInterval(update, 1000);
}

function startAutoSync() {
  if (state.autoSyncRunning) return;
  state.autoSyncRunning = true;

  // Polling cadangan setiap 10 detik memastikan data selalu sinkron
  setInterval(() => {
    loadAllData(false);
  }, 10000);
}

function startOfflineRefresh() {
  setInterval(() => {
    refreshAllMarkers();
    renderNodesTable();
    updateStatsLocally();
    renderSelectedNode();
  }, 3000);
}

initMap();
initEvents();
initRealtime();
startClock();
startOfflineRefresh();
startAutoSync();
loadAllData();

