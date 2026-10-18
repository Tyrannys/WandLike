/* Main window: navigation, Home / Library / Explore / Game / Settings pages. Uses common.js. */
const S = { boot: null, page: 'home', gid: null, game: null, st: { running: {}, toggles: {}, log: [], applied: {} }, art: {}, shut: {}, q: '' };
const $main = document.getElementById('main');
const EXTRA = {};   // extra sidebar pages, registered by debug.js in debug builds: { id: { icon, label, page() } }

async function art(gid, kind) {
  const k = gid + kind;
  if (!(k in S.art)) S.art[k] = await call('art', gid, kind);
  return S.art[k];
}
function cover(g) {
  const card = h('button', { class: 'card', onclick: () => (g.catalog ? toast(g.installed ? 'Support for this game is listed in the catalog and will download shortly (or press Sync now in Settings).' : 'Not installed on this PC. Support downloads automatically once the game is detected.', 'info') : openGame(g.id)) });
  const ini = h('div', { class: 'initials' }, g.name.split(/\s+/).map((w) => w[0]).join('').slice(0, 3));
  card.append(ini);
  if (!g.catalog) art(g.id, 'cover').then((u) => { if (u) { ini.remove(); card.prepend(h('img', { src: u, alt: '' })); } });
  card.append(h('div', { class: 'shade' }));
  const run = S.st.running[g.id];
  card.append(h('span', { class: 'badge ' + (run ? 'live' : g.installed ? '' : 'off') }, run ? '● Running' : g.installed ? (g.catalog ? 'Support available' : 'Installed' + (g.store && g.store !== 'steam' ? ' · ' + g.store : '')) : 'Not installed'));
  if (g.favorite) card.append(h('span', { class: 'star', html: icon('star') }));
  card.append(h('div', { class: 'meta' }, h('div', { class: 'nm' }, g.name), h('div', { class: 'ct' }, `${g.cheat_count} cheats`)));
  return card;
}

/* nav ----------------------------------------------------------------- */
function renderNav() {
  const side = document.getElementById('side'); side.replaceChildren();
  const item = (id, ic, label) => h('button', { class: 'nav' + (S.page === id ? ' on' : ''), onclick: () => go(id), html: icon(ic) + `<span>${label}</span>` });
  side.append(item('home', 'home', 'Home'), item('library', 'grid', 'My Games'), item('explore', 'compass', 'Explore'),
    h('div', { class: 'sp' }), ...Object.entries(EXTRA).map(([id, x]) => item(id, x.icon, x.label)), item('settings', 'gear', 'Settings'));
}
function go(page) { S.page = page; renderNav(); render(); }
async function openGame(gid) {
  S.gid = gid; S.game = await call('game', gid); S.page = 'game'; renderNav(); render();
}

function render() {
  const fn = { home: pageHome, library: pageLibrary, explore: pageExplore, game: pageGame, settings: pageSettings,
    ...Object.fromEntries(Object.entries(EXTRA).map(([id, x]) => [id, x.page])) }[S.page];
  const top = $main.scrollTop;
  $main.replaceChildren(fn()); $main.scrollTop = S.page === 'game' ? top : 0;
}

/* pages --------------------------------------------------------------- */
function pageHome() {
  const games = S.boot.games, installed = games.filter((g) => g.installed);
  const recent = [...installed].sort((a, b) => b.last_played - a.last_played)[0] || games[0];
  const p = h('div', { class: 'page' });
  if (recent) {
    const hero = h('div', { class: 'hero' });
    art(recent.id, 'hero').then((u) => { if (u) hero.prepend(h('img', { class: 'bg', src: u, alt: '' })); });
    const run = S.st.running[recent.id];
    hero.append(h('div', { class: 'shade' }), h('div', { class: 'txt' },
      h('div', { class: 'tag' }, recent.last_played ? 'Continue playing' : 'Get started'), h('div', { class: 'ttl' }, recent.name),
      h('div', { class: 'actions' }, h('button', { class: 'btn primary', onclick: () => openGame(recent.id) }, h('span', { html: icon('bolt') }), 'Open cheats'),
        h('button', { class: 'btn', onclick: () => play(recent.id) }, h('span', { html: icon('play') }), run ? 'Running' : 'Play'))));
    p.append(hero);
  }
  const favs = games.filter((g) => g.favorite);
  if (favs.length) p.append(h('h2', {}, h('span', { html: icon('star') }), 'Favorites'), h('div', { class: 'grid' }, favs.map(cover)));
  p.append(h('h2', {}, 'Your games'));
  p.append(installed.length ? h('div', { class: 'grid' }, installed.map(cover))
    : h('div', { class: 'empty' }, 'No supported games found in your Steam libraries yet. Browse Explore to see what is supported.'));
  return p;
}
function listPage(title, sub, games, withFilter) {
  const p = h('div', { class: 'page' }, h('h1', {}, title), h('div', { class: 'sub' }, sub));
  const grid = h('div', { class: 'grid' });
  const fill = () => {
    const q = S.q.toLowerCase(); grid.replaceChildren(...games.filter((g) => g.name.toLowerCase().includes(q)).map(cover));
    if (!grid.children.length) grid.append(h('div', { class: 'empty' }, 'No games match.'));
  };
  const inp = h('input', { placeholder: 'Search games', value: S.q, spellcheck: 'false' });
  inp.oninput = () => { S.q = inp.value; fill(); }; inp.onkeydown = (e) => e.stopPropagation();
  p.append(h('div', { class: 'toolbar' }, h('label', { class: 'search' }, h('span', { html: icon('search') }), inp)), grid); fill();
  return p;
}
const pageLibrary = () => listPage('My Games', 'Supported games installed on this PC', S.boot.games.filter((g) => g.installed));
const pageExplore = () => listPage('Explore', 'Every game WandLike supports. Support for installed games downloads automatically from the catalog.',
  [...S.boot.games, ...(S.boot.catalog_only || []).map((c) => ({ ...c, catalog: true }))]);

function updateLocks() {
  const g = S.game, list = document.getElementById('cheats'), bar = document.getElementById('lockbar');
  if (!g || !list) return;
  const reasons = applyLocks(list, g, S.st);
  if (!bar) return;
  const hooked = !!(S.st.hooked || {})[g.id];
  bar.style.display = reasons.length ? 'flex' : 'none';
  bar.textContent = !hooked ? 'Cheats are locked: start the game, then press Hook (the big button above).'
    : reasons.includes('Turn on Enable Console first') ? 'Console cheats are locked: turn on Enable Console first. Memory cheats like Freeze Time already work.' : '';
}

/* The big button: Play when the game is closed, Hook when it runs, Unhook when hooked. */
function playState(gid) {
  const run = !!S.st.running[gid], hooked = !!(S.st.hooked || {})[gid];
  if (!run) return { label: 'Play', cls: '', icon: 'play', chip: 'Not running', title: 'Launch the game' };
  if (!hooked) return { label: 'Hook', cls: '', icon: 'link', chip: 'Running \u00b7 not hooked', title: 'Attach to the running game for real-time cheats' };
  return { label: 'Unhook', cls: ' live', icon: 'unlink', chip: 'Hooked', title: 'Detach from the game' };
}
async function playAction(gid) {
  const run = !!S.st.running[gid];
  if (!run) {   // never hook blindly: check the game is actually there first
    const st = await call('state'); if (!st.running[gid]) return play(gid);
  }
  if ((S.st.hooked || {})[gid]) { await call('unhook', gid); return applyState(await call('state')); }
  const r = await call('hook', gid);
  if (!r.ok) toast(r.error); else toast('Hooked ' + S.game.name + (Object.keys(r.info.unresolved || {}).length ? ' (some cheats unavailable on this game version)' : ''), 'info');
  applyState(await call('state'));
}

async function play(gid) { const r = await call('play', gid); toast(r === 'running' ? 'Already running' : 'Launching through Steam…', 'info'); }

function pageGame() {
  const g = S.game, p = h('div', { class: 'page' });
  const head = h('div', { class: 'ghead' });
  art(g.id, 'hero').then((u) => { if (u) head.prepend(h('img', { class: 'bg', src: u, alt: '' })); });
  const running = S.st.running[g.id], ps = playState(g.id);
  const playBtn = h('button', { class: 'btn primary' + ps.cls, id: 'playbtn', title: ps.title, onclick: () => playAction(g.id) },
    h('span', { html: icon(ps.icon) }), ps.label);
  head.append(h('div', { class: 'shade' }), h('div', { class: 'in' },
    h('div', { class: 't' }, h('h1', {}, g.name), h('div', { class: 'chips' }, h('span', { class: 'chip' + (running ? ' ok' : ''), id: 'chip' }, h('i'), ps.chip),
      h('span', { class: 'chip' }, `${g.cheat_count} cheats`))),
    h('div', { class: 'actions' }, playBtn,
      h('button', { class: 'iconbtn' + (g.favorite ? ' on' : ''), title: 'Favorite', html: icon('star'), onclick: async (e) => { g.favorite = !g.favorite; await call('favorite', g.id, g.favorite); S.boot.games.find((x) => x.id === g.id).favorite = g.favorite; e.currentTarget.classList.toggle('on', g.favorite); } }),
      h('button', { class: 'iconbtn', title: 'Save current cheats as your preset', html: icon('save'), onclick: async () => { await call('save_preset', g.id); g.has_preset = true; toast('Cheats saved', 'info'); render(); } }),
      h('button', { class: 'btn sm', disabled: !g.has_preset, title: 'Re-apply your saved cheats', onclick: () => call('apply_preset', g.id) }, 'Apply saved'),
      h('button', { class: 'iconbtn', title: 'Toggle overlay', html: icon('layers'), onclick: () => call('toggle_overlay') }))));
  p.append(head);

  const q = h('input', { placeholder: 'Search cheats', value: S.cq || '', spellcheck: 'false' });
  q.onkeydown = (e) => e.stopPropagation();
  const list = h('div', { id: 'cheats' });
  const autoRow = h('label', { class: 'search', style: 'flex:none;cursor:pointer;gap:10px', title: 'Re-apply your saved cheats ~20 s after the game starts' },
    h('span', { class: 'switch' + (g.auto_apply ? ' on' : ''), id: 'autosw' }), 'Auto-apply');
  autoRow.onclick = async (e) => { e.preventDefault(); g.auto_apply = !g.auto_apply; document.getElementById('autosw').classList.toggle('on', g.auto_apply); await call('set_auto_apply', g.id, g.auto_apply); };
  p.append(h('div', { class: 'toolbar' }, h('label', { class: 'search' }, h('span', { html: icon('search') }), q), autoRow));
  p.append(h('div', { class: 'lockbar', id: 'lockbar', style: 'display:none' }));
  if (g.notes) p.append(h('div', { class: 'note', html: '<b>How it works:</b> ' + g.notes.replace(/</g, '&lt;') }));
  p.append(list);
  const fill = () => {
    S.cq = q.value; const term = q.value.toLowerCase(); list.replaceChildren();
    const ctx = { st: () => S.st, onChange: fill };
    const visible = g.cheats.filter((c) => c.label.toLowerCase().includes(term));
    const pinned = visible.filter((c) => c.pinned);
    const groups = [];
    if (pinned.length) groups.push(['Pinned', 'pin', pinned]);
    const secs = [...new Set(visible.map((c) => c.section))];
    for (const s of secs) {
      const cs = visible.filter((c) => c.section === s && !c.pinned);
      if (cs.length) groups.push([s, 'user', cs]);
    }
    for (const [name, ic, cs] of groups) {
      const key = g.id + name, shut = !!S.shut[key];
      const hd = h('div', { class: 'sec' + (shut ? ' shut' : '') }, h('span', { html: icon(ic) }), name, h('span', { class: 'chev', html: icon('chev') }));
      hd.onclick = () => { S.shut[key] = !S.shut[key]; fill(); };
      list.append(hd);
      if (!shut) cs.forEach((c) => list.append(cheatRow(g, c, ctx)));
    }
    if (!visible.length) list.append(h('div', { class: 'empty' }, 'No cheats match.'));
    updateLocks();
  };
  q.oninput = fill; fill();
  return p;
}

function pageSettings() {
  const s = S.settings, p = h('div', { class: 'page' }, h('h1', {}, 'Settings'), h('div', { class: 'sub' }, 'Everything here is free: hotkeys, overlay, saved cheats, auto-updates.'));
  const sw = (key) => {
    const el = h('button', { class: 'switch' + (s[key] ? ' on' : '') });
    el.onclick = async () => { s[key] = !s[key]; el.classList.toggle('on', s[key]); await call('set_setting', key, s[key]); };
    return el;
  };
  const row = (t, d, ctl) => h('div', { class: 'set-row' }, h('div', { class: 'l' }, h('b', {}, t), h('span', {}, d)), ctl);
  const ovBtn = h('button', { class: 'hk set', style: 'opacity:1;height:32px;padding:0 14px', html: icon('kbd') + ' ' + (s.overlay_hotkey || 'None') });
  ovBtn.onclick = async () => {
    const v = await captureHotkey('Overlay hotkey', s.overlay_hotkey); if (v === null) return;
    const err = await call('set_setting', 'overlay_hotkey', v); s.overlay_hotkey = v; if (err) toast(`${v}: ${err}`); render();
  };
  const consoleSel = h('select', {}, h('option', { value: 'key' }, 'Backtick / tilde key'), h('option', { value: 'combo' }, 'Shift + Alt + C'));
  consoleSel.value = s.console_mode; consoleSel.onchange = () => { s.console_mode = consoleSel.value; call('set_setting', 'console_mode', consoleSel.value); };
  p.append(h('div', { class: 'set-card' },
    row('Global hotkeys', 'Trigger cheats from inside the game with the hotkeys you assigned.', sw('hotkeys_enabled')),
    row('Overlay hotkey', 'Show or hide the in-game overlay with your pinned cheats (run the game windowed or borderless).', ovBtn),
    row('In-game console key', 'The key WandLike presses to open the game’s console. Change it if your layout needs the alternative.', consoleSel),
    row('Hook games automatically', 'A few seconds after a game with memory cheats starts, hook it so real-time cheats like Freeze Time just work.', sw('auto_hook')),
    row('Start with Windows', 'Launch WandLike when you sign in (installed build only).', sw('start_with_windows'))));
  const updBtn = h('button', { class: 'btn sm', onclick: () => call('check_update') }, 'Check now');
  p.append(h('div', { class: 'set-card' },
    row('Check for updates automatically', s.repo ? `Releases from github.com/${s.repo}` : 'Not configured: set UPDATE_REPO in wandlike/version.py', sw('autoupdate')),
    row(`WandLike v${s.version}`, S.st.update ? `Version ${S.st.update.version} is available` : 'You are on the latest version', updBtn)));
  const ago = (t) => (t ? new Date(t * 1000).toLocaleTimeString() : 'never');
  const libText = h('span', {}, '');
  const refreshLib = async () => { const s2 = await call('settings'); Object.assign(s, s2); libText.textContent = `${s.library_count} games found · last scan ${ago(s.library_scanned_at)} · ${s.catalog_status}`; };
  refreshLib();
  const rescan = h('button', { class: 'btn sm', onclick: async (e) => { e.currentTarget.textContent = 'Scanning…'; const r = await call('rescan_library', true); e.currentTarget.textContent = 'Rescan'; toast(`Found ${r.count} games (${r.stats.files_parsed || 0} manifests read)`, 'info'); setTimeout(refreshLib, 1500); } }, 'Rescan');
  const listBtn = h('button', { class: 'btn sm', onclick: async () => {
    const items = await call('library_info'); const sup = items.filter((i) => i.supported);
    openModal(h('div', {}, h('h3', {}, 'Detected games'), h('p', {}, `${items.length} found in Steam, Epic and GOG; ${sup.length} supported.`),
      h('div', { style: 'max-height:300px;overflow:auto;margin-bottom:12px;user-select:text' }, [...sup, ...items.filter((i) => !i.supported)].map((i) => h('div', { style: 'padding:4px 0;font-size:13px;color:' + (i.supported ? 'var(--text)' : 'var(--dim)') }, `${i.supported ? '⚡ ' : ''}${i.name}  ·  ${i.store}`))),
      h('div', { class: 'mrow' }, h('button', { class: 'btn sm primary', onclick: closeModal }, 'Close'))));
  } }, 'View');
  p.append(h('div', { class: 'set-card' },
    row('Game library', libText, h('div', { class: 'actions' }, listBtn, rescan)),
    row('Download game support automatically', s.repo ? `When a game is detected, its cheat plugin is pulled from github.com/${s.repo}` : 'Not configured', sw('catalog_sync')),
    row('Game catalog', 'Check the repository for new or updated game plugins now.', h('button', { class: 'btn sm', onclick: async () => { await call('sync_catalog'); toast('Syncing the catalog…', 'info'); setTimeout(refreshLib, 2500); } }, 'Sync now')),
    row('Game plugins folder', 'Drop your own game .json files here; they override catalog versions.', h('button', { class: 'btn sm', onclick: () => call('open_plugins_folder') }, 'Open folder'))));
  return p;
}

/* chrome -------------------------------------------------------------- */
function applyState(st) {
  S.st = st;
  if (S.rev !== undefined && st.rev !== S.rev) { S.rev = st.rev; call('bootstrap').then((b) => { S.boot = b; if (['home', 'library', 'explore'].includes(S.page)) render(); }); }
  S.rev = st.rev;
  const live = Object.values(st.running).some(Boolean);
  const pill = document.getElementById('upd-pill');
  if (st.update) { pill.style.display = 'block'; pill.textContent = st.progress != null ? `Downloading ${Math.round(st.progress * 100)}%` : `Update v${st.update.version}`; }
  else pill.style.display = 'none';
  const lb = document.getElementById('logbtn');
  const errs = st.log.filter((l) => l.msg.startsWith('!')).length;
  lb.innerHTML = icon('log') + ` Activity${errs ? ` · ${errs} error${errs > 1 ? 's' : ''}` : ''}`;
  const lp = document.getElementById('logpane');
  lp.innerHTML = st.log.map((l) => `<div><span class="t">${l.t}</span><span class="${l.msg.startsWith('!') ? 'e' : ''}">${l.msg.replace(/</g, '&lt;')}</span></div>`).join('') || '<div>No activity yet.</div>';
  if (lp.classList.contains('show')) lp.scrollTop = lp.scrollHeight;
  if (S.page === 'game' && S.game) {
    const run = !!st.running[S.game.id], ps = playState(S.game.id);
    const pb = document.getElementById('playbtn'), ch = document.getElementById('chip');
    if (pb) { pb.classList.toggle('live', ps.cls !== ''); pb.title = ps.title; pb.lastChild.textContent = ps.label; pb.firstChild.innerHTML = icon(ps.icon); }
    if (ch) { ch.classList.toggle('ok', run); ch.lastChild.textContent = ps.chip; }
    syncToggles(document.getElementById('cheats') || document, S.game, st);
    updateLocks();
  }
  if (S.page === 'home' || S.page === 'library' || S.page === 'explore') {
    const key = JSON.stringify(st.running); if (key !== S._runKey) { S._runKey = key; render(); }
  }
  const dot = document.querySelector('#side .nav'); if (dot) dot.title = live ? 'A supported game is running' : '';
}
async function poll() { try { applyState(await call('state')); } catch (e) { /* backend not ready */ } setTimeout(poll, 1500); }

function wireChrome() {
  document.querySelectorAll('[data-win]').forEach((b) => b.onclick = () => {
    const a = b.dataset.win;
    if (a === 'max') { S.max = !S.max; call('win', S.max ? 'maximize' : 'restore'); } else call('win', a);
  });
  document.getElementById('drag').ondblclick = () => { S.max = !S.max; call('win', S.max ? 'maximize' : 'restore'); };
  document.getElementById('logbtn').onclick = () => document.getElementById('logpane').classList.toggle('show');
  document.getElementById('scrim').onclick = (e) => { if (e.target.id === 'scrim') closeModal(); };
  document.getElementById('upd-pill').onclick = () => {
    const u = S.st.update; if (!u) return;
    openModal(h('div', {}, h('h3', {}, `WandLike ${u.version}`), h('p', {}, (u.notes || 'A new version is available.').slice(0, 400)),
      h('div', { class: 'mrow' }, h('button', { class: 'btn sm', onclick: closeModal }, 'Later'),
        h('button', { class: 'btn sm primary', onclick: () => { call('install_update'); closeModal(); } }, 'Update & restart'))));
  };
}

(async function init() {
  wireChrome();
  S.boot = await call('bootstrap'); S.settings = await call('settings');
  S.st = await call('state');
  if (S.boot.dry_run) toast('Dry-run mode: commands are logged, not sent to the game.', 'info');
  if (S.boot.debug) {   // debug.js only exists in debug builds
    await new Promise((res) => document.head.append(h('script', { src: 'debug.js', onload: res, onerror: res })));
  }
  renderNav(); render(); poll();
})();
