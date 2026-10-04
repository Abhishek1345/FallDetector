const express = require('express');

const PORT = process.env.PORT || 3000;
const OFFLINE_AFTER_MS = 150000;
const MAX_EVENTS = 200;
const TELEGRAM_TOKEN = process.env.TELEGRAM_BOT_TOKEN;
const TELEGRAM_CHAT = process.env.TELEGRAM_CHAT_ID;

const VALID_EVENTS = new Set(['fall_alert', 'fall_cancelled', 'sos', 'heartbeat']);
const ALERT_EVENTS = new Set(['fall_alert', 'sos']);

const app = express();
app.use(express.json());

const events = [];
const devices = new Map();

async function notifyTelegram(text) {
  if (!TELEGRAM_TOKEN || !TELEGRAM_CHAT) return;
  try {
    const res = await fetch(`https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: TELEGRAM_CHAT, text }),
    });
    if (!res.ok) console.error('Telegram error', res.status);
  } catch (err) {
    console.error('Telegram request failed:', err.message);
  }
}

function finiteOrNull(value) {
  return Number.isFinite(value) ? value : null;
}

app.post('/api/events', (req, res) => {
  const body = req.body || {};

  if (typeof body.deviceId !== 'string' || !VALID_EVENTS.has(body.event)) {
    return res.status(400).json({ error: 'deviceId (string) and a valid event are required' });
  }

  const record = {
    deviceId: body.deviceId.slice(0, 64),
    event: body.event,
    peakG: finiteOrNull(body.peakG),
    uptimeMs: finiteOrNull(body.uptimeMs),
    eventAgeMs: finiteOrNull(body.eventAgeMs),
    rssi: finiteOrNull(body.rssi),
    receivedAt: new Date().toISOString(),
  };

  devices.set(record.deviceId, {
    deviceId: record.deviceId,
    lastSeen: record.receivedAt,
    rssi: record.rssi,
    uptimeMs: record.uptimeMs,
    offlineNotified: false,
  });

  if (record.event !== 'heartbeat') {
    events.push(record);
    if (events.length > MAX_EVENTS) events.shift();
  }

  console.log(`[${record.receivedAt}] ${record.deviceId}: ${record.event}` +
    (record.peakG ? ` (peak ${record.peakG} g)` : ''));

  if (ALERT_EVENTS.has(record.event)) {
    const label = record.event === 'sos' ? 'SOS button pressed' : 'FALL DETECTED';
    notifyTelegram(`${label} on ${record.deviceId}` +
      (record.peakG ? ` (impact ${record.peakG} g)` : ''));
  }

  res.status(201).json({ ok: true });
});

app.get('/api/status', (req, res) => {
  const now = Date.now();
  const list = [...devices.values()].map((d) => ({
    deviceId: d.deviceId,
    lastSeen: d.lastSeen,
    rssi: d.rssi,
    uptimeMs: d.uptimeMs,
    online: now - Date.parse(d.lastSeen) <= OFFLINE_AFTER_MS,
  }));
  res.json({ devices: list, events: events.slice(-50).reverse() });
});

setInterval(() => {
  const now = Date.now();
  for (const d of devices.values()) {
    if (!d.offlineNotified && now - Date.parse(d.lastSeen) > OFFLINE_AFTER_MS) {
      d.offlineNotified = true;
      console.log(`Device ${d.deviceId} went offline`);
      notifyTelegram(`Device ${d.deviceId} has stopped reporting`);
    }
  }
}, 15000);

const PAGE = `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fall Detector</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:1rem;background:#111;color:#eee}
h1,h2{font-weight:600}
.card{padding:.75rem 1rem;border-radius:8px;margin-bottom:.5rem}
.online{background:#14351f}
.offline{background:#4a1a1a}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:.4rem .6rem;border-bottom:1px solid #333}
tr.alert td{color:#ff7b7b;font-weight:600}
</style>
</head>
<body>
<h1>Fall Detector</h1>
<div id="devices"></div>
<h2>Events</h2>
<table id="events">
<thead><tr><th>Time</th><th>Device</th><th>Event</th><th>Peak g</th></tr></thead>
<tbody></tbody>
</table>
<script>
async function refresh() {
  try {
    const r = await fetch('/api/status');
    const data = await r.json();

    const dv = document.getElementById('devices');
    dv.replaceChildren();
    for (const d of data.devices) {
      const el = document.createElement('div');
      el.className = 'card ' + (d.online ? 'online' : 'offline');
      el.textContent = d.deviceId + ' - ' + (d.online ? 'online' : 'OFFLINE') +
        ' - last seen ' + new Date(d.lastSeen).toLocaleTimeString() +
        (d.rssi !== null ? ' - ' + d.rssi + ' dBm' : '');
      dv.appendChild(el);
    }

    const tb = document.querySelector('#events tbody');
    tb.replaceChildren();
    for (const e of data.events) {
      const tr = document.createElement('tr');
      if (e.event === 'fall_alert' || e.event === 'sos') tr.className = 'alert';
      const cells = [new Date(e.receivedAt).toLocaleTimeString(), e.deviceId, e.event,
        e.peakG !== null ? e.peakG : ''];
      for (const v of cells) {
        const td = document.createElement('td');
        td.textContent = v;
        tr.appendChild(td);
      }
      tb.appendChild(tr);
    }
  } catch (err) {}
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>`;

app.get('/', (req, res) => {
  res.type('html').send(PAGE);
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(`Fall detector server listening on http://0.0.0.0:${PORT}`);
});
