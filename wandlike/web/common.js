/* Shared helpers: backend bridge (pywebview or dev HTTP), tiny DOM builder, icons, cheat rows. */
const ICONS = {
  bolt: '<path d="M13 2 4 14h6l-1 8 9-12h-6z"/>', home: '<path d="M3 11 12 3l9 8v10h-6v-6H9v6H3z"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
  compass: '<circle cx="12" cy="12" r="9"/><path d="m15.5 8.5-2 5-5 2 2-5z"/>', gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3h0a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8v0a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
  star: '<path d="m12 3 2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1-4.4-4.3 6.1-.9z"/>',
  play: '<path d="M7 4v16l13-8z"/>', pin: '<path d="M9 3h6l-1 6 3 3v2H7v-2l3-3zM12 14v7"/>',
  check: '<path d="m5 12 5 5 9-10"/>', minus: '<path d="M6 12h12"/>', plus: '<path d="M12 6v12M6 12h12"/>',
  chev: '<path d="m6 9 6 6 6-6"/>', user: '<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0z"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>', kbd: '<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
  sliders: '<path d="M4 6h10M18 6h2M4 12h3M11 12h9M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  save: '<path d="M6 3h12v18l-6-4-6 4z"/>', layers: '<path d="m12 3 9 5-9 5-9-5zM3 13l9 5 9-5"/>',
  refresh: '<path d="M20 11a8 8 0 0 0-14-4M4 4v4h4M4 13a8 8 0 0 0 14 4M20 20v-4h-4"/>', x: '<path d="M6 6l12 12M18 6 6 18"/>',
  log: '<path d="M4 6h16M4 12h16M4 18h10"/>', folder: '<path d="M3 6a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  down: '<path d="M12 4v12m0 0-5-5m5 5 5-5M5 20h14"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>', unlink: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7M4 4l16 16"/>',
  bug: '<rect x="8" y="8" width="8" height="12" rx="4"/><path d="M9 5l1.5 2M15 5l-1.5 2M4 12h4M16 12h4M4 18l4-2M20 18l-4-2M4 6l4 3M20 6l-4 3"/>',
};
const icon = (n) => `<svg viewBox="0 0 24 24">${ICONS[n] || ''}</svg>`;

function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === false || v == null) continue;
    if (k === 'class') el.className = v;
    else if (k === 'html') el.innerHTML = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(kid));
  return el;
}

/* backend bridge: pywebview in the app, plain HTTP in tools/dev_server.py */
let _ready;
function whenReady() {
  if (!_ready) _ready = new Promise((res) => {
    if (window.pywebview && window.pywebview.api) return res();
    window.addEventListener('pywebviewready', () => res());
    setTimeout(() => { if (!(window.pywebview && window.pywebview.api)) res(); }, 1200);
  });
  return _ready;
}
async function call(name, ...args) {
  await whenReady();
  if (window.pywebview && window.pywebview.api) return window.pywebview.api[name](...args);
  const r = await fetch('/api/' + name, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(args) });
  return r.json();
}

function toast(msg, kind = 'err') {
  const t = h('div', { class: 'toast ' + (kind === 'info' ? 'info' : '') }, msg);
  document.getElementById('toasts').append(t);
  setTimeout(() => t.remove(), 5000);
}

/* hotkey capture ------------------------------------------------------ */
const CODE_NAMES = { Space: 'Space', Enter: 'Enter', Tab: 'Tab', Backquote: '`', Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']',
  Semicolon: ';', Quote: "'", Comma: ',', Period: '.', Slash: '/', Backslash: '\\', ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up',
  ArrowDown: 'Down', Insert: 'Insert', Delete: 'Delete', Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown' };
function comboFromEvent(e) {
  const c = e.code; let key = null;
  if (/^Key[A-Z]$/.test(c)) key = c.slice(3); else if (/^Digit\d$/.test(c)) key = c.slice(5);
  else if (/^F\d+$/.test(c)) key = c; else if (/^Numpad\d$/.test(c)) key = 'Num' + c.slice(6); else key = CODE_NAMES[c] || null;
  const mods = [e.ctrlKey && 'Ctrl', e.altKey && 'Alt', e.shiftKey && 'Shift', e.metaKey && 'Win'].filter(Boolean);
  return { mods, key };
}
function captureHotkey(title, current) {
  return new Promise((resolve) => {
    const box = h('div', { class: 'keybox' }, current || 'Press a key…');
    let result = current || '';
    const done = (v) => { document.removeEventListener('keydown', onkey, true); closeModal(); resolve(v); };
    const onkey = (e) => {
      e.preventDefault(); e.stopPropagation();
      if (e.code === 'Escape') return done(null);
      const { mods, key } = comboFromEvent(e);
      box.textContent = [...mods, key || '…'].join(' + ');
      if (key) { result = [...mods, key].join('+'); setTimeout(() => done(result), 180); }
    };
    document.addEventListener('keydown', onkey, true);
    openModal(h('div', {}, h('h3', {}, title), h('p', {}, 'Press the key combination you want. Esc cancels.'), box,
      h('div', { class: 'mrow' }, h('button', { class: 'btn sm', onclick: () => done('') }, 'Clear'),
        h('button', { class: 'btn sm', onclick: () => done(null) }, 'Cancel'))));
  });
}
function openModal(node) { const m = document.getElementById('modal'); m.replaceChildren(node); document.getElementById('scrim').classList.add('show'); }
function closeModal() { document.getElementById('scrim').classList.remove('show'); }

/* cheat rows ---------------------------------------------------------- */
const fmt = (v) => (Number.isInteger(+v) ? String(Math.trunc(+v)) : String(v));
function cheatRow(g, c, ctx) {
  /* ctx: { st(): latest state, onChange(): rerender, compact: bool } */
  const row = h('div', { class: 'row' + (c.available ? '' : ' dis'), 'data-cid': c.id });
  const tip = [c.info, c.requires && `Requires: ${c.requires}`, `[${c.verified}] ${c.unsupported || ''}`].filter(Boolean).join('\n\n');
  row.append(h('span', { class: 'bolt', html: icon(c.kind === 'button' ? 'play' : 'bolt') }), h('span', { class: 'nm' }, c.label),
    h('span', { class: 'info', title: tip }, 'i'));
  if (c.requires) row.append(h('span', { class: 'req' }, c.requires.replace(' DLC', '')));
  row.append(h('span', { class: 'grow' }));

  if (!ctx.compact && c.available) {
    const hk = h('button', { class: 'hk' + (c.hotkey ? ' set' : ''), title: 'Set hotkey', html: icon('kbd') + (c.hotkey ? ' ' + c.hotkey : '') });
    hk.onclick = async () => {
      const v = await captureHotkey(`Hotkey for ${c.label}`, c.hotkey);
      if (v === null) return;
      const err = await call('set_hotkey', g.id, c.id, v);
      c.hotkey = v; if (err) toast(`${v}: ${err}`); ctx.onChange();
    };
    row.append(hk);
  }

  if (c.kind === 'number') {
    if (c.has_modes) {
      const mc = h('button', { class: 'modechip', title: 'Set to value / add value' }, c.mode === 'add' ? 'Add' : 'Set');
      mc.onclick = () => { c.mode = c.mode === 'add' ? 'set' : 'add'; mc.textContent = c.mode === 'add' ? 'Add' : 'Set'; call('remember_value', g.id, c.id, +input.value || 0, c.mode); };
      row.append(mc);
    }
    const input = h('input', { value: fmt(c.value), inputmode: 'numeric', spellcheck: 'false' });
    const bump = (s) => { const v = Math.max(c.min, Math.min(c.max, (+input.value || 0) + s * c.step)); input.value = fmt(v); call('remember_value', g.id, c.id, v, c.mode); };
    const ok = h('button', { class: 'apply', title: 'Apply', html: icon('check') });
    const apply = async () => {
      const raw = +input.value; if (Number.isNaN(raw)) return toast(`${c.label}: not a number`);
      const r = await call('set_number', g.id, c.id, raw, c.mode); input.value = fmt(r.value);
      if (!r.ok) return toast(c.label + ': hook the game first (press Hook)');
      ok.classList.add('done'); setTimeout(() => ok.classList.remove('done'), 900);
    };
    ok.onclick = apply; input.onkeydown = (e) => { if (e.key === 'Enter') apply(); e.stopPropagation(); };
    row.append(h('div', { class: 'pill' }, h('button', { html: icon('minus'), onclick: () => bump(-1) }), input, h('button', { html: icon('plus'), onclick: () => bump(1) })), ok);
  } else if (c.kind === 'toggle') {
    const off = h('button', { 'data-v': '0' }, 'Off'), on = h('button', { 'data-v': '1' }, 'On');
    const seg = h('div', { class: 'seg', 'data-seg': c.id }, off, on);
    const set = async (v) => { if (!c.available) return toast(c.unsupported || 'Not available'); const ok = await call('set_toggle', g.id, c.id, v); paintSeg(seg, ok); if (v && !ok) toast(c.label + ': hook the game first (press Hook)'); };
    off.onclick = () => set(false); on.onclick = () => set(true);
    paintSeg(seg, !!(ctx.st().toggles[g.id] || {})[c.id]);
    row.append(seg);
    row.append(c.options.length ? h('button', { class: 'gear', title: 'Options', html: icon('sliders'), onclick: () => optionsModal(g, c) })
      : h('span', { class: 'gear ph' }));
  } else {
    row.append(h('button', { class: 'runbtn', onclick: () => call('press', g.id, c.id) }, c.launch ? 'Launch' : 'Run'));
  }
  if (!ctx.compact) {
    const pin = h('button', { class: 'pin' + (c.pinned ? ' on' : ''), title: c.pinned ? 'Unpin' : 'Pin to overlay', html: icon('pin') });
    pin.onclick = () => { c.pinned = !c.pinned; call('pin', g.id, c.id, c.pinned); ctx.onChange(); };
    row.append(pin);
  }
  return row;
}
function paintSeg(seg, on) {
  seg.classList.toggle('is-on', !!on);
  seg.children[0].classList.toggle('on', !on); seg.children[1].classList.toggle('on', !!on);
}
function optionsModal(g, c) {
  const fields = c.options.map((o) => {
    const inp = h('input', { value: o.value, type: 'number', min: o.min, max: o.max });
    inp.onchange = () => { const v = Math.max(o.min ?? -1e9, Math.min(o.max ?? 1e9, +inp.value)); inp.value = v; o.value = v; call('set_option', g.id, c.id, o.id, v); };
    inp.onkeydown = (e) => e.stopPropagation();
    return h('div', { class: 'field' }, h('label', {}, o.label), inp);
  });
  openModal(h('div', {}, h('h3', {}, c.label), h('p', {}, 'Options'), fields, h('div', { class: 'mrow' }, h('button', { class: 'btn sm primary', onclick: closeModal }, 'Done'))));
}
function syncToggles(root, g, st) {
  root.querySelectorAll('[data-seg]').forEach((seg) => paintSeg(seg, !!(st.toggles[g.id] || {})[seg.dataset.seg]));
}
