/* Route payment window. openPay({ amount, symbol, breakdown, onPaid }) opens a
 * bottom-sheet checkout: pick a method, (for card) enter details, then it runs
 * intent -> confirm -> processing -> success. No more instant "paid". */
(function () {
  function el(tag, style, html) {
    const e = document.createElement(tag);
    if (style) e.style.cssText = style;
    if (html != null) e.innerHTML = html;
    return e;
  }
  window.openPay = function (opts) {
    const symbol = opts.symbol || 'Rs';
    const amount = Number(opts.amount || 0);
    const fmt = n => symbol + ' ' + Number(n).toLocaleString(undefined, { maximumFractionDigits: 2 });
    let method = 'card', busy = false;

    const back = el('div', 'position:fixed;inset:0;z-index:9999;background:rgba(13,24,38,.55);display:flex;align-items:flex-end;justify-content:center');
    const sheet = el('div', 'background:var(--surface);width:100%;max-width:520px;border-radius:18px 18px 0 0;padding:18px 18px 22px;font-family:var(--font);max-height:92vh;overflow:auto');
    back.appendChild(sheet);

    let bd = '';
    if (opts.breakdown) {
      bd = '<div style="border:1px dashed var(--line-2);border-radius:10px;padding:10px 12px;margin:10px 0;font-size:13px;color:var(--muted)">' +
        opts.breakdown.map(r => '<div style="display:flex;justify-content:space-between;padding:2px 0"><span>' + r[0] + '</span><span>' + r[1] + '</span></div>').join('') +
        '</div>';
    }
    sheet.innerHTML =
      '<div style="width:40px;height:4px;border-radius:9px;background:var(--line-2);margin:0 auto 14px"></div>' +
      '<div style="display:flex;justify-content:space-between;align-items:baseline">' +
        '<h3 style="font-family:var(--display);margin:0">Pay ' + fmt(amount) + '</h3>' +
        '<span id="pcount" style="font-size:13px;color:var(--muted)">expires in 2:00</span></div>' +
      bd +
      '<div id="pmethods" style="display:flex;gap:8px;margin:14px 0 12px;flex-wrap:wrap"></div>' +
      '<div id="ppanel"></div>' +
      '<p id="perr" style="color:var(--rose);font-size:13px;min-height:16px;margin:6px 0 0"></p>' +
      '<div style="display:flex;gap:10px;margin-top:8px">' +
        '<button id="pcancel" class="btn ghost" style="flex:1;justify-content:center">Cancel</button>' +
        '<button id="ppay" class="btn primary" style="flex:2;justify-content:center">Pay ' + fmt(amount) + '</button>' +
      '</div>';
    document.body.appendChild(back);

    const methods = [['card', 'Card'], ['wallet', 'Wallet'], ['cash', 'Cash'], ['qr', 'Scan to pay']];
    const mWrap = sheet.querySelector('#pmethods');
    methods.forEach(m => {
      const b = el('button', 'border:1px solid var(--line-2);background:var(--surface);color:var(--ink);font-weight:600;font-size:13px;padding:8px 12px;border-radius:9px;cursor:pointer', m[1]);
      b.onclick = () => { method = m[0]; paint(); };
      b.dataset.m = m[0];
      mWrap.appendChild(b);
    });

    const panel = sheet.querySelector('#ppanel');
    const payBtn = sheet.querySelector('#ppay');
    const err = sheet.querySelector('#perr');

    function paint() {
      mWrap.querySelectorAll('button').forEach(b => {
        const on = b.dataset.m === method;
        b.style.background = on ? 'var(--accent)' : 'var(--surface)';
        b.style.color = on ? 'var(--on-accent)' : 'var(--ink)';
        b.style.borderColor = on ? 'var(--accent)' : 'var(--line-2)';
      });
      err.textContent = '';
      if (method === 'card') {
        panel.innerHTML =
          '<div style="display:flex;flex-direction:column;gap:8px">' +
          '<input id="cnum" inputmode="numeric" placeholder="Card number  4242 4242 4242 4242" style="border:1px solid var(--line-2);background:var(--surface-2);border-radius:9px;padding:11px 12px;font-size:14px;font-family:var(--font);color:var(--ink)" />' +
          '<div style="display:flex;gap:8px"><input id="cexp" placeholder="MM/YY" style="flex:1;border:1px solid var(--line-2);background:var(--surface-2);border-radius:9px;padding:11px 12px;font-size:14px;color:var(--ink)" />' +
          '<input id="ccvv" inputmode="numeric" placeholder="CVV" style="flex:1;border:1px solid var(--line-2);background:var(--surface-2);border-radius:9px;padding:11px 12px;font-size:14px;color:var(--ink)" /></div></div>';
        payBtn.textContent = 'Pay ' + fmt(amount);
      } else if (method === 'wallet') {
        panel.innerHTML = '<div style="font-size:14px;color:var(--muted)">Loading wallet…</div>';
        payBtn.textContent = 'Pay with wallet';
        API.get('/api/wallet').then(w => {
          panel.innerHTML = '<div style="font-size:14px;color:var(--muted)">Route Wallet balance <b style="color:var(--ink)">' + w.symbol + ' ' + Number(w.balance).toLocaleString() + '</b>. This ride will be deducted from your wallet.</div>';
        }).catch(() => {});
      } else if (method === 'cash') {
        panel.innerHTML = '<div style="font-size:14px;color:var(--muted)">Pay <b style="color:var(--ink)">' + fmt(amount) + '</b> in cash to the driver. Confirm to place the trip.</div>';
        payBtn.textContent = 'Confirm cash';
      } else {
        panel.innerHTML =
          '<div style="text-align:center">' +
          '<div style="width:150px;height:150px;margin:6px auto;border-radius:12px;background:repeating-linear-gradient(90deg,var(--ink) 0 8px,transparent 8px 16px),repeating-linear-gradient(0deg,var(--ink) 0 8px,transparent 8px 16px);opacity:.85"></div>' +
          '<div style="font-size:13px;color:var(--muted);margin-top:6px">Scan with any bank app (Raast / JazzCash / Easypaisa / UPI)</div></div>';
        payBtn.textContent = "I've paid in my app";
      }
    }
    paint();

    let left = 120;
    const cd = sheet.querySelector('#pcount');
    const tick = setInterval(() => {
      left--; if (left < 0) { close(); return; }
      cd.textContent = 'expires in ' + Math.floor(left / 60) + ':' + String(left % 60).padStart(2, '0');
    }, 1000);

    function close() { clearInterval(tick); back.remove(); }
    sheet.querySelector('#pcancel').onclick = close;
    back.addEventListener('click', e => { if (e.target === back) close(); });

    payBtn.onclick = async () => {
      if (busy) return;
      err.textContent = '';
      let card = '';
      if (method === 'card') {
        card = (document.getElementById('cnum').value || '').replace(/\D/g, '');
        if (card.length < 12) { err.textContent = 'Enter a valid card number.'; return; }
      }
      busy = true;
      const orig = payBtn.textContent;
      payBtn.textContent = 'Processing…'; payBtn.style.opacity = '.7';
      try {
        const intent = await API.post('/api/payments/intent', { amount, symbol, ref: opts.ref || '' });
        await new Promise(r => setTimeout(r, 1200));
        await API.post('/api/payments/' + intent.payment_id + '/confirm', { method, card });
        close();
        if (opts.onPaid) opts.onPaid({ payment_id: intent.payment_id, method });
      } catch (ex) {
        busy = false; payBtn.textContent = orig; payBtn.style.opacity = '1';
        err.textContent = ex.message || 'Payment failed';
      }
    };
  };
})();
