'use strict';

const SUPPORTED = '1.10.163 (OG), 1.10.984 (NG), 1.11.240 (AE)';
const ORDER = ['play', 'mods', 'server', 'logs', 'quit'];
const DEFAULT_ORDER = ['play', 'mods', 'quit'];
const BUSY_LABEL = {
  check: 'CHECKING',
  play: 'LAUNCHING',
  mods: 'READING YOUR MOD ORGANIZER 2 SETUP…',
  crashes: 'SCANNING FOR CRASHES…',
  share: 'PACKAGING LOGS…',
  server: 'READING THE SERVER…',
  network: 'CHECKING YOUR NETWORK…',
  start: 'STARTING THE SERVER…',
  admin: 'SENDING TO THE SERVER…',
  save: 'SAVING THE SETTINGS…',
  restart: 'RESTARTING THE SERVER…',
};

const app = {
  state: null,
  view: 'status',
  hostMode: 'p2p',
  serverTab: 'overview',
  sent: null,
  log: [],
  warned: false,
  busy: false,
  what: '',
  failure: '',
  selected: '',
  moved: false,
  dialogOpen: false,
  returnFocus: null,
};

const $ = (id) => document.getElementById(id);

const el = (tag, className, text) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};

const send = (cmd) => window.ipc.postMessage(JSON.stringify(cmd));

const menuButton = (key) => document.querySelector(`#menu [data-action="${key}"]`);

function onEvent(ev) {
  switch (ev.type) {
    case 'busy':
      app.busy = ev.on;
      app.what = ev.what;
      if (!ev.on && app.modsWanted && app.view === 'mods') {
        app.modsWanted = false;
        send({ cmd: 'mods' });
      }
      if (ev.on) app.failure = '';
      else serverIdle();
      break;
    case 'state':
      app.state = ev;
      break;
    case 'mods':
      app.mods = ev;
      break;
    case 'crashes':
      app.crashes = ev.list;
      break;
    case 'logs_shared':
      app.shared = ev;
      break;
    case 'server':
      app.server = ev;
      break;
    case 'server_network':
      app.checks = ev.checks;
      break;
    case 'server_reply':
      noteReply(ev);
      break;
    case 'error':
      app.failure = ev.message;
      break;
  }
  render();
}

function render() {
  $('busy').hidden = !app.busy || app.what === 'open_mo2';
  $('busyLabel').textContent = BUSY_LABEL[app.what] || '';
  $('opening').hidden = !(app.busy && app.what === 'open_mo2');
  renderStatus();
  renderMenu();
  renderMods();
  renderServer();
  renderLogs();
}

function renderStatus() {
  const s = app.state || {};
  const warn = !s.error && !!s.unsupported;
  const text = s.error || (warn ? `Unsupported Fallout 4 version ${s.unsupported} -- supported: ${SUPPORTED}` : app.failure);
  $('notice').hidden = !text;
  $('notice').className = warn ? 'notice notice--warn' : 'notice notice--error';
  $('noticeText').textContent = text || '';

  $('playNote').hidden = s.canPlay || !s.playNote;
  $('playNote').textContent = s.playNote || '';

  $('gameDir').textContent = s.gameDir || '-';
  $('runtime').textContent = s.runtime ? `${s.runtime.label} ${s.runtime.gameVersion}` : '-';
  $('exeVersion').textContent = s.exeVersion || '-';
  const sha = $('sha256');
  sha.textContent = s.sha256 ? s.sha256.slice(0, 12) : '-';
  sha.title = s.sha256 || '';
}

function enabled(key) {
  const s = app.state || {};
  if (app.busy) return false;
  if (key === 'play') return !!s.canPlay && !s.unsupported;
  return true;
}

function renderMenu() {
  if (!app.moved || !enabled(app.selected)) app.selected = DEFAULT_ORDER.find(enabled);
  ORDER.forEach((key) => {
    const b = menuButton(key);
    const on = enabled(key);
    b.disabled = !on;
    b.classList.toggle('is-disabled', !on);
    b.classList.toggle('is-selected', key === app.selected);
  });
}

function select(key, focus) {
  app.moved = true;
  app.selected = key;
  renderMenu();
  if (focus) menuButton(key).focus();
}

function step(dir) {
  const list = ORDER.filter(enabled);
  if (!list.length) return;
  select(list[(list.indexOf(app.selected) + dir + list.length) % list.length], true);
}

function activate(key) {
  if (!enabled(key)) return;
  if (key === 'play') send({ cmd: 'play' });
  else if (key === 'mods') openMods();
  else if (key === 'server') openServer();
  else if (key === 'logs') openLogs();
  else if (key === 'quit') askConfirm('EXIT LAUNCHER?', () => send({ cmd: 'quit' }));
}

function showView(name) {
  app.view = name;
  $('viewStatus').hidden = name !== 'status';
  $('viewMods').hidden = name !== 'mods';
  $('viewServer').hidden = name !== 'server';
  $('viewLogs').hidden = name !== 'logs';
  render();
}

function openDialog(title, options, body, danger) {
  app.returnFocus = document.activeElement;
  app.dialogOpen = true;
  $('dialogTitle').textContent = title;
  const text = $('dialogBody');
  text.textContent = '';
  (body || []).forEach((line) => text.appendChild(el('p', undefined, line)));
  text.hidden = !body;
  $('dialog').querySelector('.dialog__panel').classList.toggle('dialog__panel--danger', !!danger);
  const box = $('dialogOptions');
  box.textContent = '';
  options.forEach((o) => {
    const b = el('button', 'menu-item dialog__option');
    b.type = 'button';
    const bar = el('span', 'menu-item__bar');
    bar.setAttribute('aria-hidden', 'true');
    b.append(el('span', 'menu-item__label', o.label), bar);
    b.addEventListener('click', o.run);
    box.appendChild(b);
  });
  $('dialog').hidden = false;
  box.firstElementChild.focus();
}

function closeDialog() {
  app.dialogOpen = false;
  $('dialog').hidden = true;
  if (app.returnFocus && app.returnFocus.isConnected) app.returnFocus.focus();
  app.returnFocus = null;
}

function askConfirm(title, onYes) {
  openDialog(title, [
    { label: 'YES', run: () => { closeDialog(); onYes(); } },
    { label: 'NO', run: closeDialog },
  ]);
}

function dialogKey(e) {
  if (e.key === 'Escape') {
    closeDialog();
    return;
  }
  const forward = e.key === 'ArrowDown' || (e.key === 'Tab' && !e.shiftKey);
  const backward = e.key === 'ArrowUp' || (e.key === 'Tab' && e.shiftKey);
  if (!forward && !backward) return;
  e.preventDefault();
  const items = [...$('dialog').querySelectorAll('button:not([disabled])')];
  const at = items.indexOf(document.activeElement);
  items[(at + (forward ? 1 : -1) + items.length) % items.length].focus();
}

function fitScale() {
  const k = Math.min(window.innerWidth / 1280, window.innerHeight / 720);
  document.documentElement.style.setProperty('--co-ui-scale', Math.min(1.25, Math.max(0.75, k)).toFixed(3));
}

document.addEventListener('keydown', (e) => {
  if (app.dialogOpen) {
    dialogKey(e);
    return;
  }
  if (e.key === 'Escape') {
    if (['mods', 'server', 'logs'].includes(app.view)) showView('status');
    return;
  }
  if (e.target.matches('input, select')) return;
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault();
    step(e.key === 'ArrowDown' ? 1 : -1);
  } else if (e.key === 'Enter' && (e.target === document.body || e.target.closest('#menu'))) {
    e.preventDefault();
    activate(app.selected);
  }
});

ORDER.forEach((key) => {
  const b = menuButton(key);
  b.addEventListener('click', () => activate(key));
  b.addEventListener('mouseenter', () => select(key, false));
});

wireMods();
wireServer();
wireServerSettings();
wireServerAdmin();
wireLogs();

window.coLauncher = { onEvent };
window.addEventListener('resize', fitScale);
fitScale();
if (window.ipc) boot();
else loadMock();

function boot() {
  render();
  send({ cmd: 'init' });
}

function loadMock() {
  const script = document.createElement('script');
  script.src = 'server-mock.js';
  script.onload = () => {
    installMock();
    boot();
  };
  script.onerror = () => {
    $('notice').hidden = false;
    $('notice').className = 'notice notice--error';
    $('noticeText').textContent = 'This page needs the launcher, and the mock page script is missing. Run the launcher, or serve server-mock.js next to this page.';
  };
  document.body.appendChild(script);
}

function installMock() {
  const dir = 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4';
  const sha = '3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273';
  const base = {
    gameDir: dir,
    error: null,
    exeVersion: '1.11.240',
    sha256: sha,
    runtime: { label: 'AE', gameVersion: '1.11.240' },
    unsupported: null,
    canPlay: true,
    playNote: null,
  };
  const SCENARIOS = {
    ok: {},
    nologs: {},
    noserver: {},
    running: {},
    nomo2: {
      canPlay: false,
      playNote: 'Mod Organizer 2 was not found. Download the Commonwealth Online modlist with Wabbajack and run this launcher from inside that folder.',
    },
    unsupported: {
      exeVersion: '1.10.980',
      runtime: null,
      unsupported: '1.10.980',
      canPlay: false,
      playNote: 'Fallout 4 1.10.980 is not a supported runtime',
    },
    nogame: {
      gameDir: null,
      error: 'Fallout 4 was not found through Steam.',
      exeVersion: null,
      sha256: null,
      runtime: null,
      canPlay: false,
      playNote: null,
    },
    slow: {},
  };
  const hash = location.hash.slice(1);
  const scenarioName = hash in SCENARIOS ? hash : 'ok';
  const busy = (on, what) => ({ type: 'busy', on, what });
  const play = (steps) => {
    let t = 0;
    steps.forEach(([ms, ev]) => {
      t += ms;
      setTimeout(() => window.coLauncher.onEvent(ev), t);
    });
  };
  const handlers = {
    init: () => play([[0, busy(true, 'check')], [300, { type: 'state', ...base, ...SCENARIOS[scenarioName] }], [0, busy(false, 'check')]]),
    mods: modsMock(play, busy),
    ...logsMock(play, busy, scenarioName === 'nologs'),
    ...serverMock(play, busy, scenarioName),
    open_mo2: () => play([[0, busy(true, 'open_mo2')], [scenarioName === 'slow' ? 60000 : 3000, busy(false, 'open_mo2')]]),
    play: () => play([[0, busy(true, 'play')], [600, busy(false, 'play')]]),
  };
  window.ipc = {
    postMessage(text) {
      const cmd = JSON.parse(text);
      if (handlers[cmd.cmd]) handlers[cmd.cmd](cmd);
    },
  };
}
