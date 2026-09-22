/*
 * RideMap - unified ride-tracking map for Route.
 * Primary: MapLibre GL JS + OpenFreeMap vector tiles (style: positron) - free, no API key, unlimited.
 * Routing: OSRM road geometry (FOSSGIS + project-osrm), straight-line great-circle fallback.
 * Fallback engine: a self-contained hand-built "Route City" canvas map with its own road graph
 *                  + Dijkstra router, used automatically when MapLibre or its tiles are unavailable.
 * Public interface (identical for both engines):
 *   const rm = RideMap.create(containerId, { onPick(which,{lat,lng,label}) });
 *   rm.setActive('pickup'|'dropoff'); rm.useMyLocation(); rm.getPins();
 *   await rm.previewRoute(); rm.startRide(driverStart?); rm.onStatus(status); rm.reset();
 */
window.RideMap = (function () {
  const OSRM_BASES = [
    'https://routing.openstreetmap.de/routed-car',
    'https://router.project-osrm.org',
  ];
  const OFM_STYLE = (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches)
    ? 'https://tiles.openfreemap.org/styles/dark'
    : 'https://tiles.openfreemap.org/styles/positron';
  const CITY_CENTER = { lat: 24.8607, lng: 67.0011 }; // Karachi

  function haversine(a, b) {
    const R = 6371000, r = Math.PI / 180;
    const dLat = (b.lat - a.lat) * r, dLng = (b.lng - a.lng) * r;
    const s = Math.sin(dLat / 2) ** 2 +
      Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dLng / 2) ** 2;
    return 2 * R * Math.atan2(Math.sqrt(s), Math.sqrt(1 - s));
  }
  function straightRoute(from, to, steps) {
    steps = steps || 60;
    const path = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      path.push([from.lat + (to.lat - from.lat) * t, from.lng + (to.lng - from.lng) * t]);
    }
    const dist = haversine(from, to);
    return { path, distance: dist, duration: dist / (30 * 1000 / 3600), onRoad: false };
  }
  async function osrmRoute(from, to) {
    for (const base of OSRM_BASES) {
      try {
        const url = base + '/route/v1/driving/' + from.lng + ',' + from.lat + ';' +
          to.lng + ',' + to.lat + '?overview=full&geometries=geojson';
        const res = await fetch(url);
        if (!res.ok) continue;
        const d = await res.json();
        if (d.code !== 'Ok' || !d.routes || !d.routes.length) continue;
        const r = d.routes[0];
        return { path: r.geometry.coordinates.map(c => [c[1], c[0]]), distance: r.distance, duration: r.duration, onRoad: true };
      } catch (_) { /* try next, then straight */ }
    }
    return straightRoute(from, to);
  }
  // bearing on [lat,lng] tuples -> compass degrees (0 = north, clockwise)
  function bearing(p0, p1) { return Math.atan2(p1[1] - p0[1], p1[0] - p0[0]) * 180 / Math.PI; }

  function walkPath(path, seconds, onFrame, onDone) {
    const D = [0];
    for (let i = 1; i < path.length; i++)
      D.push(D[i - 1] + Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]));
    const total = D[D.length - 1] || 1;
    const start = performance.now();
    function frame(now) {
      const t = Math.min(1, (now - start) / (seconds * 1000));
      const want = t * total;
      let i = 1; while (i < D.length && D[i] < want) i++;
      const p0 = path[i - 1], p1 = path[Math.min(i, path.length - 1)];
      const seg = (D[i] - D[i - 1]) || 1, f = (want - D[i - 1]) / seg;
      onFrame([p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * f], bearing(p0, p1), t);
      if (t < 1) requestAnimationFrame(frame); else onDone && onDone();
    }
    requestAnimationFrame(frame);
  }
  function driverOffset(pickup) { return { lat: pickup.lat - 0.0055, lng: pickup.lng + 0.006 }; }

  /* ==================== REAL ENGINE (MapLibre GL + OpenFreeMap) ==================== */
  function RealEngine(container, opts) {
    const el = typeof container === 'string' ? document.getElementById(container) : container;
    const map = new maplibregl.Map({
      container: el, style: OFM_STYLE, center: [CITY_CENTER.lng, CITY_CENTER.lat], zoom: 12, attributionControl: true,
    });
    map.addControl(new maplibregl.NavigationControl({ showCompass: false }), 'top-right');

    // The map is often created just as the container becomes visible (its panel
    // starts display:none) or before webfonts finish reflowing, so MapLibre can
    // latch a stale/zero size and paint black. Resize on load and whenever the
    // container's box actually changes — the canonical fix for the black map.
    map.on('load', () => map.resize());
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(() => map.resize());
      ro.observe(el);
    }

    let ready = false; const readyQ = [];
    function markReady() { if (!ready) { ready = true; readyQ.forEach(f => f()); } map.resize(); }
    map.on('load', markReady);
    map.on('idle', markReady); // safety: fires once tiles settle even if 'load' was missed
    function whenReady(f) { if (ready) f(); else readyQ.push(f); }
    let failed = false;
    // Only fall back to Route City if the vector style genuinely never loads.
    setTimeout(() => {
      if (failed || ready) return;
      if (map.isStyleLoaded && map.isStyleLoaded()) { markReady(); return; }
      failed = true; opts.onTileFail && opts.onTileFail();
    }, 15000);

    let active = 'pickup', pins = { pickup: null, dropoff: null };
    let markers = { pickup: null, dropoff: null, car: null };

    function makeEl(glyph, size) {
      const wrap = document.createElement('div');
      wrap.style.cssText = 'width:' + size + 'px;height:' + size + 'px;display:grid;place-items:center;cursor:pointer';
      const inner = document.createElement('div');
      inner.textContent = glyph;
      inner.style.cssText = 'font-size:' + size + 'px;line-height:1;filter:drop-shadow(0 2px 3px rgba(0,0,0,.35))';
      wrap.appendChild(inner); wrap.__inner = inner; return wrap;
    }
    function place(which, ll) {
      pins[which] = { lat: ll.lat, lng: ll.lng, label: which === 'pickup' ? 'Pickup pin' : 'Destination pin' };
      if (markers[which]) markers[which].setLngLat([ll.lng, ll.lat]);
      else {
        markers[which] = new maplibregl.Marker({ element: makeEl(which === 'pickup' ? '📍' : '🏁', 30), anchor: 'bottom', draggable: true })
          .setLngLat([ll.lng, ll.lat]).addTo(map);
        markers[which].on('dragend', () => { const g = markers[which].getLngLat(); pins[which] = { lat: g.lat, lng: g.lng, label: pins[which].label }; opts.onPick && opts.onPick(which, pins[which]); });
      }
      opts.onPick && opts.onPick(which, pins[which]);
    }
    map.on('click', e => place(active, { lat: e.lngLat.lat, lng: e.lngLat.lng }));

    function setLine(id, path, color, width, dash) {
      whenReady(() => {
        const data = { type: 'Feature', geometry: { type: 'LineString', coordinates: path.map(p => [p[1], p[0]]) } };
        if (map.getSource(id)) { map.getSource(id).setData(data); return; }
        map.addSource(id, { type: 'geojson', data });
        const paint = { 'line-color': color, 'line-width': width, 'line-opacity': 0.95 };
        if (dash) paint['line-dasharray'] = dash;
        map.addLayer({ id, type: 'line', source: id, layout: { 'line-cap': 'round', 'line-join': 'round' }, paint });
      });
    }
    function removeLine(id) { whenReady(() => { if (map.getLayer(id)) map.removeLayer(id); if (map.getSource(id)) map.removeSource(id); }); }
    function fit(path) {
      let a = 90, b = 180, c = -90, d = -180;
      path.forEach(p => { a = Math.min(a, p[0]); c = Math.max(c, p[0]); b = Math.min(b, p[1]); d = Math.max(d, p[1]); });
      whenReady(() => map.fitBounds([[b, a], [d, c]], { padding: 60, duration: 600, maxZoom: 16 }));
    }
    function carAt(ll, br) {
      if (!markers.car) markers.car = new maplibregl.Marker({ element: makeEl('🚕', 26), anchor: 'center' }).setLngLat([ll[1], ll[0]]).addTo(map);
      else markers.car.setLngLat([ll[1], ll[0]]);
      markers.car.getElement().__inner.style.transform = 'rotate(' + br + 'deg)';
    }

    return {
      kind: 'real',
      setActive(w) { active = w; },
      getPins() { return pins; },
      setPin(which, ll, label) { place(which, ll); if (label) pins[which].label = label; },
      useMyLocation(cb) {
        if (!('geolocation' in navigator)) { cb && cb(false); return; }
        navigator.geolocation.getCurrentPosition(
          p => { const ll = { lat: p.coords.latitude, lng: p.coords.longitude }; whenReady(() => map.flyTo({ center: [ll.lng, ll.lat], zoom: 15 })); place('pickup', ll); cb && cb(true); },
          () => cb && cb(false), { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 });
      },
      async previewRoute() {
        if (!pins.pickup || !pins.dropoff) return null;
        const r = await osrmRoute(pins.pickup, pins.dropoff);
        setLine('leg2', r.path, '#d8973c', 6); fit(r.path);
        return { distance: r.distance, duration: r.duration, onRoad: r.onRoad };
      },
      async startRide(driverStart) {
        if (!pins.pickup) return;
        const ds = driverStart || driverOffset(pins.pickup);
        const leg1 = await osrmRoute(ds, pins.pickup);
        setLine('leg1', leg1.path, '#5d6f79', 4, [2, 2]);
        carAt(leg1.path[0], 0);
        fit(leg1.path.concat(pins.dropoff ? [[pins.dropoff.lat, pins.dropoff.lng]] : []));
        walkPath(leg1.path, 8, (ll, br) => carAt(ll, br), () => opts.onArrive && opts.onArrive());
      },
      async legTwo() {
        if (!pins.pickup || !pins.dropoff) return;
        const leg2 = await osrmRoute(pins.pickup, pins.dropoff);
        removeLine('leg1'); setLine('leg2', leg2.path, '#d8973c', 6); fit(leg2.path);
        walkPath(leg2.path, 10, (ll, br) => carAt(ll, br), () => opts.onComplete && opts.onComplete());
      },
      reset() {
        removeLine('leg1'); removeLine('leg2');
        ['pickup', 'dropoff', 'car'].forEach(k => { if (markers[k]) { markers[k].remove(); markers[k] = null; } });
        pins = { pickup: null, dropoff: null }; active = 'pickup';
      },
    };
  }

  /* ===================== FALLBACK ENGINE: "Route City" canvas ===================== */
  const CITY = buildCity();
  function buildCity() {
    const gx = [200, 600, 1000, 1400, 1800], gy = [200, 600, 1000, 1400, 1800];
    const roads = [];
    const vN = ['1st', '2nd', '3rd', '4th', '5th'];
    const hN = ['North Ave', 'Park Ave', 'Central Blvd', 'Market St', 'South Ave'];
    gx.forEach((x, i) => roads.push({ name: vN[i] + ' Street', class: 'street', pts: gy.map(y => [x, y]) }));
    gy.forEach((y, i) => roads.push({ name: hN[i], class: i === 2 ? 'boulevard' : 'avenue', pts: gx.map(x => [x, y]) }));
    roads.push({ name: 'Harbor Diagonal', class: 'avenue', pts: [[200, 200], [600, 600], [1000, 1000], [1400, 1400], [1800, 1800]] });
    roads.push({ name: 'Riverside Diagonal', class: 'avenue', pts: [[1800, 200], [1400, 600], [1000, 1000], [600, 1400], [200, 1800]] });
    roads.push({ name: 'Ring Road', class: 'boulevard', pts: [[200, 200], [1800, 200], [1800, 1800], [200, 1800], [200, 200]] });
    const river = [[0, 1250], [500, 1180], [900, 1320], [1300, 1180], [1750, 1300], [2000, 1240], [2000, 1440], [1300, 1400], [700, 1520], [0, 1460]];
    const bridges = [[1000, 1000], [1400, 1400]];
    const park = [[640, 640], [960, 640], [960, 960], [640, 960]];
    const districts = [{ name: 'Old Town', at: [400, 400] }, { name: 'Harbor', at: [1550, 1650] }, { name: 'Greenside', at: [800, 800] }];
    const pois = [{ name: 'Central Market', at: [1000, 1400] }, { name: 'Grand Station', at: [1400, 600] }, { name: 'City Park', at: [800, 800] }, { name: 'Harbor Pier', at: [1800, 1800] }];
    const key = p => p[0] + ',' + p[1];
    const nodes = {}, adj = {};
    function add(p) { const k = key(p); if (!nodes[k]) { nodes[k] = p; adj[k] = []; } return k; }
    roads.forEach(r => { for (let i = 0; i < r.pts.length; i++) { const k = add(r.pts[i]); if (i) { const pk = key(r.pts[i - 1]); const w = Math.hypot(r.pts[i][0] - r.pts[i - 1][0], r.pts[i][1] - r.pts[i - 1][1]); adj[pk].push([k, w]); adj[k].push([pk, w]); } } });
    return { roads, river, bridges, park, districts, pois, nodes, adj, key };
  }
  function nearestNode(pt) {
    let best = null, bd = 1e18;
    for (const k in CITY.nodes) { const n = CITY.nodes[k]; const d = Math.hypot(n[0] - pt[0], n[1] - pt[1]); if (d < bd) { bd = d; best = k; } }
    return best;
  }
  function dijkstra(aPt, bPt) {
    const s = nearestNode(aPt), t = nearestNode(bPt);
    const dist = {}, prev = {}, seen = {};
    for (const k in CITY.nodes) dist[k] = 1e18;
    dist[s] = 0;
    while (true) {
      let u = null, ud = 1e18;
      for (const k in dist) if (!seen[k] && dist[k] < ud) { ud = dist[k]; u = k; }
      if (u === null || u === t) break;
      seen[u] = 1;
      for (const e of CITY.adj[u]) { const v = e[0], w = e[1]; if (dist[u] + w < dist[v]) { dist[v] = dist[u] + w; prev[v] = u; } }
    }
    const path = []; let cur = t;
    while (cur) { path.unshift(CITY.nodes[cur]); if (cur === s) break; cur = prev[cur]; }
    return [aPt].concat(path).concat([bPt]);
  }

  function CityEngine(container, opts) {
    const el = typeof container === 'string' ? document.getElementById(container) : container;
    el.innerHTML = '';
    const cv = document.createElement('canvas');
    cv.style.cssText = 'width:100%;height:100%;display:block';
    el.appendChild(cv);
    const ctx = cv.getContext('2d');
    let W = 0, H = 0, scale = 1;
    function resize() { const r = el.getBoundingClientRect(); W = cv.width = Math.max(300, r.width); H = cv.height = Math.max(300, r.height); scale = Math.min(W, H) / 2000; }
    function toXY(p) { return [p[0] * scale + (W - 2000 * scale) / 2, p[1] * scale + (H - 2000 * scale) / 2]; }
    function toWorld(mx, my) { return [(mx - (W - 2000 * scale) / 2) / scale, (my - (H - 2000 * scale) / 2) / scale]; }

    let active = 'pickup', pins = { pickup: null, dropoff: null };
    let leg1 = null, leg2 = null, car = null, carBr = 0;

    function poly(pts, close) { ctx.beginPath(); pts.forEach((p, i) => { const q = toXY(p); i ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1]); }); if (close) ctx.closePath(); }
    function glyph(p, g) { const q = toXY([p.x, p.y]); ctx.font = '26px serif'; ctx.textAlign = 'center'; ctx.fillText(g, q[0], q[1] - 6); }
    function draw() {
      ctx.fillStyle = '#eae4d3'; ctx.fillRect(0, 0, W, H);
      ctx.fillStyle = '#bcd6b0'; poly(CITY.park, true); ctx.fill();
      ctx.fillStyle = '#9ec6e0'; poly(CITY.river, true); ctx.fill();
      const casing = { boulevard: 14, avenue: 10, street: 7 }, fill = { boulevard: 9, avenue: 6, street: 4 };
      const fc = { boulevard: '#ffffff', avenue: '#ffffff', street: '#fbf8f1' };
      ctx.lineCap = 'round'; ctx.lineJoin = 'round';
      ['boulevard', 'avenue', 'street'].forEach(cls => { ctx.strokeStyle = '#cdbf9e'; ctx.lineWidth = casing[cls] * scale * 2; CITY.roads.filter(r => r.class === cls).forEach(r => { poly(r.pts); ctx.stroke(); }); });
      ['boulevard', 'avenue', 'street'].forEach(cls => { ctx.strokeStyle = fc[cls]; ctx.lineWidth = fill[cls] * scale * 2; CITY.roads.filter(r => r.class === cls).forEach(r => { poly(r.pts); ctx.stroke(); }); });
      ctx.fillStyle = '#d8cba8'; CITY.bridges.forEach(b => { const q = toXY(b); ctx.fillRect(q[0] - 16, q[1] - 8, 32, 16); });
      ctx.fillStyle = '#5d6f79'; ctx.font = '600 13px "Hanken Grotesk",sans-serif'; ctx.textAlign = 'center';
      CITY.districts.forEach(d => { const q = toXY(d.at); ctx.fillText(d.name, q[0], q[1]); });
      ctx.fillStyle = '#7a5214'; ctx.font = '600 11px "Hanken Grotesk",sans-serif';
      CITY.pois.forEach(p => { const q = toXY(p.at); ctx.beginPath(); ctx.arc(q[0], q[1], 3, 0, 7); ctx.fill(); ctx.fillText(p.name, q[0], q[1] - 8); });
      if (leg1) { ctx.strokeStyle = '#5d6f79'; ctx.lineWidth = 4; ctx.setLineDash([3, 8]); poly(leg1); ctx.stroke(); ctx.setLineDash([]); }
      if (leg2) { ctx.strokeStyle = '#d8973c'; ctx.lineWidth = 6; poly(leg2); ctx.stroke(); }
      if (pins.pickup) glyph(pins.pickup, '📍'); if (pins.dropoff) glyph(pins.dropoff, '🏁');
      if (car) { const q = toXY(car); ctx.save(); ctx.translate(q[0], q[1]); ctx.rotate(carBr * Math.PI / 180); ctx.font = '22px serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('🚕', 0, 0); ctx.restore(); }
    }
    function place(which, worldPt) { pins[which] = { x: worldPt[0], y: worldPt[1], lat: CITY_CENTER.lat, lng: CITY_CENTER.lng, label: (which === 'pickup' ? 'Pickup' : 'Destination') + ' in Route City' }; opts.onPick && opts.onPick(which, pins[which]); draw(); }
    cv.addEventListener('click', e => { const r = cv.getBoundingClientRect(); place(active, toWorld(e.clientX - r.left, e.clientY - r.top)); });
    window.addEventListener('resize', () => { resize(); draw(); });
    resize(); draw();
    function ride(pathWorld, seconds, which, done) { if (which === 'leg1') leg1 = pathWorld; else leg2 = pathWorld; walkPath(pathWorld.map(p => [p[1], p[0]]), seconds, (ll, br) => { car = [ll[1], ll[0]]; carBr = br; draw(); }, done); }
    return {
      kind: 'city',
      setActive(w) { active = w; },
      getPins() { return pins; },
      setPin(which) { place(which, which === 'dropoff' ? [1400, 1400] : [400, 400]); },
      useMyLocation(cb) { place('pickup', [600, 1000]); cb && cb(true); },
      async previewRoute() { if (!pins.pickup || !pins.dropoff) return null; leg2 = dijkstra([pins.pickup.x, pins.pickup.y], [pins.dropoff.x, pins.dropoff.y]); draw(); let dist = 0; for (let i = 1; i < leg2.length; i++) dist += Math.hypot(leg2[i][0] - leg2[i - 1][0], leg2[i][1] - leg2[i - 1][1]); return { distance: dist * 1.2, duration: dist / 6, onRoad: true }; },
      startRide() { if (!pins.pickup) return; const ds = [pins.pickup.x + 300, pins.pickup.y + 300]; const p = dijkstra(ds, [pins.pickup.x, pins.pickup.y]); car = ds.slice(); ride(p, 8, 'leg1', () => opts.onArrive && opts.onArrive()); },
      legTwo() { if (!pins.pickup || !pins.dropoff) return; leg1 = null; const p = dijkstra([pins.pickup.x, pins.pickup.y], [pins.dropoff.x, pins.dropoff.y]); ride(p, 10, 'leg2', () => opts.onComplete && opts.onComplete()); },
      reset() { pins = { pickup: null, dropoff: null }; leg1 = leg2 = car = null; active = 'pickup'; draw(); },
    };
  }

  return {
    create(containerId, opts) {
      opts = opts || {};
      const el = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
      const useCity = opts.forceCity || typeof maplibregl === 'undefined';
      let engine;
      if (useCity) { engine = CityEngine(el, opts); }
      else {
        engine = RealEngine(el, Object.assign({}, opts, {
          onTileFail() {
            if (engine && engine.kind === 'real') {
              try { el.innerHTML = ''; } catch (_) {}
              const c = CityEngine(el, opts);
              for (const k in c) engine[k] = c[k];
              engine.kind = 'city';
              opts.onFallback && opts.onFallback();
            }
          },
        }));
      }
      engine.onStatus = function (status) { if (status === 'arrived') { engine.legTwo && engine.legTwo(); } };
      return engine;
    },
    _city: CITY,
  };
})();
