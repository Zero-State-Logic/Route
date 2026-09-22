const API = {
  token: localStorage.getItem('route_token') || null,
  user: JSON.parse(localStorage.getItem('route_user') || 'null'),
  setAuth(t, u) {
    this.token = t; this.user = u;
    localStorage.setItem('route_token', t);
    localStorage.setItem('route_user', JSON.stringify(u));
  },
  clear() {
    this.token = null; this.user = null;
    localStorage.removeItem('route_token');
    localStorage.removeItem('route_user');
  },
  async req(path, opts = {}) {
    const headers = Object.assign({ 'Content-Type': 'application/json' }, opts.headers || {});
    if (this.token) headers['Authorization'] = 'Bearer ' + this.token;
    const res = await fetch(path, Object.assign({}, opts, { headers }));
    const json = await res.json().catch(() => ({ success: false, error: 'Bad response' }));
    if (!json.success) throw new Error(json.error || ('HTTP ' + res.status));
    return json.data;
  },
  get(p) { return this.req(p); },
  post(p, body) { return this.req(p, { method: 'POST', body: JSON.stringify(body) }); }
};

function liveSocket(onMessage) {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = proto + '://' + location.host + '/ws/live' +
    (API.token ? '?token=' + encodeURIComponent(API.token) : '');
  const ws = new WebSocket(url);
  ws.onmessage = e => { try { onMessage(JSON.parse(e.data)); } catch (_) {} };
  return ws;
}

function toast(msg) {
  let t = document.getElementById('toast');
  if (!t) { t = document.createElement('div'); t.id = 'toast'; t.className = 'toast'; document.body.appendChild(t); }
  t.textContent = msg; t.classList.add('show');
  clearTimeout(window.__tt); window.__tt = setTimeout(() => t.classList.remove('show'), 2600);
}

function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
