'use strict';

const P2P_NOTE = 'STEAM P2P is the easiest way to play with friends. Press START HOSTING in the game and the server starts for you. Your friends join through Steam, so you do not change anything on your router. Steam must be running and signed in. The people you host for can see your internet address (IP), so only host for people you trust.';
const DANGER_NOTE = 'A dedicated server is meant to run on a rented online computer (a VPS) or a PC that is always on. If you run it on your home internet, anyone who can find your address could try to connect, and you have to change your router settings to let players in. That puts your home network on the open internet. If you only want to play with friends, use STEAM P2P instead.';
const DANGER_DIALOG = [
  'A dedicated server is meant for a VPS (a rented online computer) or a PC that is always on.',
  'At home, players can only reach it if you forward the game port (7777 unless you changed it) on your router. That opens the port to the whole internet: anyone who finds your address can try to connect, and players learn your home internet address (IP).',
  'This launcher never opens ports or changes your router or firewall for you.',
  'If you only want to play with friends, use STEAM P2P. If you host at home anyway, set a server password in SETTINGS.',
];
const WARNED_KEY = 'co.dedicatedWarned';
const WORLD_LABEL = { 'coop-story': 'Co-op story', 'free-roam': 'Free roam' };
const CHECK_CHIP = {
  ok: ['OK', 'chip--ok'],
  warn: ['CHECK', 'chip--warn'],
  fail: ['FIX', 'chip--error'],
  info: ['INFO', 'chip--info'],
};
const PANES = ['overview', 'players', 'admin', 'settings'];

function warned() {
  if (app.warned) return true;
  try {
    return localStorage.getItem(WARNED_KEY) === '1';
  } catch (e) {
    return false;
  }
}

function rememberWarned() {
  app.warned = true;
  try {
    localStorage.setItem(WARNED_KEY, '1');
  } catch (e) {
    app.warned = true;
  }
}

function makeControl(spec) {
  let node;
  if (spec.t === 'select') {
    node = el('select', 'field');
    (spec.blank ? [['', spec.blank], ...spec.options] : spec.options).forEach(([value, text]) => {
      const o = el('option', undefined, text);
      o.value = value;
      node.appendChild(o);
    });
  } else if (spec.t === 'bool') {
    node = el('input');
    node.type = 'checkbox';
    node.checked = !!spec.def;
  } else {
    node = el('input', 'field');
    node.type = spec.t === 'int' ? 'number' : spec.t === 'password' ? 'password' : 'text';
    node.autocomplete = 'off';
    node.spellcheck = false;
    if (spec.t === 'int') {
      if (spec.min !== undefined) node.min = spec.min;
      if (spec.max !== undefined) node.max = spec.max;
    }
    if (spec.maxLength) node.maxLength = spec.maxLength;
    if (spec.hint) node.placeholder = spec.hint;
  }
  node.setAttribute('aria-label', spec.label);
  return {
    node,
    read: () => (spec.t === 'bool' ? (node.checked ? 'yes' : 'no') : node.value.trim()),
    set: (v) => {
      if (spec.t === 'bool') node.checked = !!v;
      else node.value = v === undefined || v === null ? '' : String(v);
    },
  };
}

function openServer() {
  app.server = null;
  app.checks = null;
  app.reply = null;
  app.serverTab = 'overview';
  showView('server');
  send({ cmd: 'server' });
}

function adminCall(command, args, label, to) {
  if (app.busy) return;
  app.sent = { label: label || command, to: to || 'players' };
  send({ cmd: 'server_admin', command, args: args || {} });
}

function noteReply(ev) {
  const sent = app.sent || { label: 'SERVER', to: 'players' };
  app.sent = null;
  app.replyTo = sent.to;
  app.reply = ev;
  if (sent.to === 'admin') pushLog(sent.label, ev.ok, ev.text, ev.data);
  if (sent.to === 'settings') settingsReplied(ev);
}

function serverIdle() {
  if (!app.restartNext) return;
  app.restartNext = false;
  app.sent = { label: 'RESTART', to: 'settings' };
  send({ cmd: 'server_restart' });
}

function withReason(args) {
  const reason = $('serverReason').value.trim();
  $('serverReason').value = '';
  return reason ? { ...args, reason } : args;
}

function rowButton(label, run) {
  const b = el('button', 'btn', label);
  b.type = 'button';
  b.disabled = app.busy;
  b.addEventListener('click', run);
  return b;
}

function playerRow(p) {
  const row = el('div', 'server-row');
  const text = `#${p.id}  ${p.address || 'unknown address'}${p.steamId ? `  STEAM ${p.steamId}` : ''}`;
  row.appendChild(el('span', 'server-row__text', text));
  const who = { playerId: p.id };
  const act = (label, command, args, sure) => rowButton(label, () => {
    const go = () => adminCall(command, args(), `${label} #${p.id}`);
    if (sure) askConfirm(sure, go);
    else go();
  });
  row.append(
    act('KICK', 'kick', () => withReason(who)),
    act('REVIVE', 'revive', () => who),
    act('MUTE', 'voice_mute', () => {
      const seconds = Number($('serverMuteFor').value);
      return seconds ? { ...who, seconds } : who;
    }),
    act('UNMUTE', 'voice_unmute', () => who),
    act('KILL', 'kill', () => who, `KILL PLAYER #${p.id}?`),
    act('BAN', 'ban', () => withReason(who), `BAN PLAYER #${p.id}?`),
  );
  if (p.steamId) row.appendChild(act('STEAM BAN', 'steam_ban', () => withReason(who), `STEAM BAN PLAYER #${p.id}?`));
  return row;
}

const UNBAN = {
  ip: (id) => ['unban', { ip: id }],
  steam: (id) => ['steam_unban', { steamId: id }],
  profile: (id) => ['profile_unban', { profileId: id }],
};

function banRow(b) {
  const row = el('div', 'server-row');
  row.appendChild(el('span', 'server-row__text', `${b.kind.toUpperCase()}  ${b.id}${b.reason ? `  - ${b.reason}` : ''}`));
  const [command, args] = UNBAN[b.kind](b.id);
  row.appendChild(rowButton('UNBAN', () => adminCall(command, args, `UNBAN ${b.id}`)));
  return row;
}

function addBan() {
  const kind = $('serverBanKind').value;
  const value = $('serverBanValue').value.trim();
  const bad = kind === 'steam' ? !/^\d{17}$/.test(value) : !/^[0-9a-fA-F.:]{3,45}$/.test(value);
  if (bad) {
    app.reply = { ok: false, text: kind === 'steam' ? 'A Steam ID has exactly 17 digits.' : 'Type a valid IP address, for example 203.0.113.7.' };
    app.replyTo = 'players';
    renderServer();
    return;
  }
  const reason = $('serverReason').value.trim();
  askConfirm(`BAN ${kind === 'steam' ? 'STEAM ID' : 'IP'} ${value}?`, () => {
    $('serverBanValue').value = '';
    const extra = reason ? { reason } : {};
    if (kind === 'steam') adminCall('steam_ban', { steamId: value, ...extra }, `BAN ${value}`);
    else adminCall('ban', { ip: value, ...extra }, `BAN ${value}`);
  });
}

function fill(box, rows, build, empty) {
  box.textContent = '';
  if (rows.length) rows.forEach((r) => box.appendChild(build(r)));
  else box.appendChild(el('p', 'status-note', empty));
}

function cardRows(s) {
  const rows = [['FILES', !s ? '-' : s.installed ? 'INSTALLED' : s.bundled ? 'NOT INSTALLED' : 'NOT INCLUDED IN THIS LAUNCHER']];
  rows.push(['SERVER', !s ? '-' : s.running ? 'RUNNING' : 'NOT RUNNING']);
  const st = s && s.status;
  if (st) {
    rows.push(['NAME', st.name], ['PLAYERS', `${st.players} of ${st.maxPlayers}`]);
    rows.push(['GAME MODE', WORLD_LABEL[st.worldMode] || st.worldMode], ['GAME PORT', `${st.port} (UDP)`]);
  }
  if (s && s.listing) rows.push(['PUBLIC LISTING', s.listing.text]);
  rows.push(['FOLDER', (s && s.folder) || '-']);
  return rows;
}

function tabEnabled(tab) {
  const s = app.server;
  if (tab === 'overview') return true;
  if (!s) return false;
  return tab === 'settings' ? !!s.settings : s.running;
}

function paneHint(s) {
  if (!s) return '';
  if (!s.running) return 'PLAYERS and ADMIN work while the server is running. Start it first.';
  return '';
}

function renderServer() {
  const s = app.server;
  const running = !!(s && s.running);
  if (!tabEnabled(app.serverTab)) app.serverTab = 'overview';
  PANES.forEach((tab) => {
    const b = $(`serverPane_${tab}`);
    b.setAttribute('aria-selected', String(app.serverTab === tab));
    b.disabled = !tabEnabled(tab);
    $(`serverBody_${tab}`).hidden = app.serverTab !== tab;
  });
  const hint = paneHint(s);
  $('serverPaneHint').hidden = !hint;
  $('serverPaneHint').textContent = hint;

  const problem = app.failure || (s && s.problem) || '';
  $('serverProblem').hidden = !problem;
  $('serverProblem').textContent = problem;

  const showReply = !!app.reply && app.replyTo !== 'admin';
  $('serverReply').hidden = !showReply;
  $('serverReply').className = `notice ${app.reply && app.reply.ok ? 'notice--ok' : 'notice--error'}`;
  $('serverReplyText').textContent = showReply ? app.reply.text : '';

  renderOverview(s, running);
  if (running) {
    fill($('serverPlayers'), s.players, playerRow, 'No players are connected.');
    fill($('serverBans'), s.bans, banRow, 'Nobody is banned.');
  }
  $('serverBanAdd').disabled = app.busy || !running;
  renderServerSettings(s, running);
  renderServerAdmin(running);
}

function renderOverview(s, running) {
  const dedicated = app.hostMode === 'dedicated';
  ['p2p', 'dedicated'].forEach((mode) => {
    $(`serverTab_${mode}`).setAttribute('aria-selected', String(app.hostMode === mode));
  });
  $('serverModeNote').hidden = dedicated;
  $('serverModeNote').textContent = P2P_NOTE;
  $('serverDanger').hidden = !dedicated;
  $('serverDangerText').textContent = DANGER_NOTE;
  $('serverNote').hidden = !(s && s.note);
  $('serverNoteText').textContent = (s && s.note) || '';

  const card = $('serverCard');
  card.textContent = '';
  cardRows(s).forEach(([k, v]) => card.append(el('dt', undefined, k), el('dd', undefined, v)));

  $('serverRefresh').disabled = app.busy;
  $('serverCheck').disabled = app.busy;
  $('serverStart').hidden = running || !dedicated;
  $('serverStart').disabled = app.busy || !(s && s.installed);
  $('serverStop').hidden = !running;
  $('serverStop').disabled = app.busy;

  const checks = $('serverChecks');
  checks.textContent = '';
  $('serverChecksEmpty').hidden = !!app.checks;
  (app.checks || []).forEach((c) => {
    const [label, tone] = CHECK_CHIP[c.level] || CHECK_CHIP.info;
    const item = el('li', 'check');
    item.append(el('span', undefined, c.text), el('span', `chip ${tone}`, label));
    checks.appendChild(item);
  });
}

function setMode(mode) {
  if (app.hostMode === mode) return;
  app.hostMode = mode;
  app.checks = null;
  renderServer();
  if (!app.busy) send({ cmd: 'server_network', mode });
}

function chooseMode(mode) {
  if (mode !== 'dedicated' || warned()) {
    setMode(mode);
    return;
  }
  openDialog('BEFORE YOU RUN A DEDICATED SERVER', [
    { label: 'USE STEAM P2P INSTEAD', run: closeDialog },
    { label: 'I UNDERSTAND, CONTINUE', run: () => { closeDialog(); rememberWarned(); setMode('dedicated'); } },
  ], DANGER_DIALOG, true);
}

function showPane(tab) {
  if (!tabEnabled(tab)) return;
  app.serverTab = tab;
  app.reply = null;
  renderServer();
}

function wireServer() {
  PANES.forEach((tab) => $(`serverPane_${tab}`).addEventListener('click', () => showPane(tab)));
  $('serverTab_p2p').addEventListener('click', () => chooseMode('p2p'));
  $('serverTab_dedicated').addEventListener('click', () => chooseMode('dedicated'));
  $('serverRefresh').addEventListener('click', () => {
    if (app.busy) return;
    app.reply = null;
    send({ cmd: 'server' });
  });
  $('serverCheck').addEventListener('click', () => {
    if (!app.busy) send({ cmd: 'server_network', mode: app.hostMode });
  });
  $('serverStart').addEventListener('click', () => {
    if (!app.busy) send({ cmd: 'server_start' });
  });
  $('serverStop').addEventListener('click', () => askConfirm('STOP THE SERVER?', () => adminCall('shutdown', {}, 'STOP SERVER', 'overview')));
  $('serverBanAdd').addEventListener('click', addBan);
  $('serverBanValue').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') addBan();
  });
  $('serverBack').addEventListener('click', () => showView('status'));
}
