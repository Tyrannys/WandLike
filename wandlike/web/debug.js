/* Debug-only page. Not shipped in release builds (build.ps1 leaves this file out). */
(function () {
  const pill = h('span', { style: 'background:#7a1f1f;color:#ffb4b4;border-radius:99px;padding:2px 10px;font-size:11px;font-weight:700;margin-right:8px' }, 'DEBUG');
  document.getElementById('titlebar').insertBefore(pill, document.getElementById('upd-pill'));

  const pre = (id) => h('pre', { id, style: 'background:#0d0f14;border:1px solid var(--line);border-radius:8px;padding:12px;margin:8px 0 0;font:12px/1.5 Consolas,monospace;color:#9aa3b5;white-space:pre-wrap;max-height:260px;overflow:auto;user-select:text' });
  const btn = (label, fn) => h('button', { class: 'btn sm', onclick: async () => { try { await fn(); } catch (e) { toast(String(e)); } } }, label);
  const gid = () => (S.boot.games[0] || {}).id;

  async function refresh() {
    const el = (i) => document.getElementById(i); if (!el('dbg-info')) return;
    el('dbg-info').textContent = JSON.stringify(await call('debug_info'), null, 2);
    const lines = await call('debug_log_tail', 120);
    el('dbg-log').textContent = lines.join('\n'); el('dbg-log').scrollTop = 1e9;
  }

  EXTRA.debug = {
    icon: 'bug', label: 'Debug',
    page() {
      const p = h('div', { class: 'page' }, h('h1', {}, 'Debug'), h('div', { class: 'sub' }, 'Only present in debug builds. Same output goes to the console window and %APPDATA%\\WandLike\\debug.log.'));
      const cmd = h('input', { placeholder: 'raw console line, e.g. add_gold 100', style: 'flex:1;background:#0d0f14;border:1px solid var(--line);border-radius:8px;padding:8px 10px;outline:0;user-select:text' });
      cmd.onkeydown = (e) => { e.stopPropagation(); if (e.key === 'Enter') send.click(); };
      const send = btn('Send to game console', async () => { if (cmd.value.trim()) await call('debug_send_console', gid(), cmd.value.trim()); });
      p.append(
        h('h2', {}, 'Simulate'),
        h('div', { class: 'actions', style: 'flex-wrap:wrap' },
          btn('Fake update available', () => call('debug_fake_update', '9.9.9')),
          btn('Pretend game is running', () => call('debug_fake_running', gid(), true)),
          btn('Pretend game is closed', () => call('debug_fake_running', gid(), false)),
          btn('Use real game state', () => call('debug_fake_running', gid(), null)),
          btn('Throw backend exception', () => call('debug_throw')),
          btn('Toast', () => toast('debug toast', 'info'))),
        h('h2', {}, 'Raw console command'), h('div', { class: 'actions' }, cmd, send),
        h('h2', {}, 'Environment'), pre('dbg-info'),
        h('h2', {}, 'Log tail'), pre('dbg-log'),
        h('h2', {}, 'Saved settings'), btn('Show settings.json', async () => { document.getElementById('dbg-set').textContent = JSON.stringify(await call('debug_dump_settings'), null, 2); }), pre('dbg-set'));
      refresh(); clearInterval(window._dbgTimer); window._dbgTimer = setInterval(refresh, 2000);
      return p;
    },
  };
  window.addEventListener('error', (e) => toast('JS error: ' + e.message));
  window.addEventListener('unhandledrejection', (e) => toast('Unhandled: ' + (e.reason && e.reason.message || e.reason)));
  renderNav();
})();
