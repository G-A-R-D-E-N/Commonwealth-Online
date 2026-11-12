'use strict';

const SETUP_PAGES = ['Where', 'Check', 'Install'];
const STEP_WHERE = 0;
const STEP_CHECK = 1;
const STEP_RUN = 2;
const SETUP_STEP_LABEL = {
  detect: 'Finding Fallout 4',
  download: 'Downloading the release',
  verify: 'Checking the download',
  address_library: 'Address Library',
  link: 'Linking the files',
};
const SETUP_ORDER = ['detect', 'download', 'verify', 'address_library', 'link'];
const NEXUS_NOTE = 'The Address Library is hosted on Nexus Mods, which needs you to sign in. Download it there yourself, then choose the file below. The launcher never downloads it for you.';

const wiz = {
  view: null,
  opened: false,
  pendingScan: false,
  playPending: false,
  step: STEP_WHERE,
  game: '',
  editGame: false,
  choice: '',
  customDir: '',
  addressPath: '',
  progress: {},
  current: '',
  failed: '',
  running: false,
  result: null,
};

function setupBox(text, className) {
  return el('p', className || 'status-note', text);
}

function actionButton(label, onClick) {
  const button = el('button', 'btn', label);
  button.type = 'button';
  button.addEventListener('click', onClick);
  return button;
}

function scanSetup() {
  if (app.busy) wiz.pendingScan = true;
  else send({ cmd: 'setup_scan', game: wiz.game.trim() });
}

function textField(label, value, onInput, placeholder) {
  const row = el('label', 'location-field', label);
  const input = document.createElement('input');
  input.type = 'text';
  input.autocomplete = 'off';
  input.value = value || '';
  if (placeholder) input.placeholder = placeholder;
  input.addEventListener('input', () => {
    onInput(input.value);
    refreshNav();
  });
  row.appendChild(input);
  return row;
}

function gameField() {
  const row = el('label', 'location-field', 'Fallout 4 folder');
  const input = document.createElement('input');
  input.type = 'text';
  input.autocomplete = 'off';
  input.value = wiz.game;
  input.placeholder = 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4';
  input.addEventListener('input', () => {
    wiz.game = input.value;
  });
  input.addEventListener('change', () => {
    wiz.game = input.value;
    scanSetup();
  });
  row.appendChild(input);
  return row;
}

function gameOk(v) {
  return !!(v && v.gameDir && v.runtime);
}

function gameProblem(v) {
  if (v.gameError) return v.gameError;
  if (v.unsupported) return `Fallout 4 ${v.unsupported} is not supported. Supported: ${SUPPORTED}.`;
  return wiz.step === STEP_WHERE ? 'Fallout 4 was not found. Type its folder below.' : 'Fallout 4 was not found. Go back and type its folder.';
}

function addressOk(v) {
  return !!v && (v.addressStatus !== 'missing' || !!wiz.addressPath.trim());
}

function whereOptions(v) {
  const list = [];
  (v.managers || []).forEach((m) => {
    if (m.kind === 'mo2' && m.dir) {
      let reason = '';
      if (m.channel === 'wabbajack') reason = 'managed by Wabbajack';
      else if (m.channel !== 'standalone' && m.channel !== 'unknown') reason = 'not supported';
      else if (gameOk(v) && !m.managesThisGame) reason = 'not set up for this Fallout 4';
      list.push({ id: `mo2:${m.dir}`, manager: 'mo2', instance: m.dir, label: m.name || 'Mod Organizer 2', detail: m.dir, reason, manages: !!m.managesThisGame, adopt: m.channel === 'unknown' });
    } else if (m.kind === 'vortex') {
      list.push({ id: 'vortex', manager: 'vortex', instance: '', label: 'Vortex', detail: m.dir || '', reason: '', manages: !!m.managesThisGame });
    }
  });
  list.push({ id: 'custom', manager: 'mo2', instance: wiz.customDir.trim(), label: 'Another Mod Organizer 2 folder', detail: '', reason: '', manages: false, adopt: true });
  list.push({ id: 'none', manager: 'none', instance: '', label: 'Fallout 4 folder directly (no mod manager)', detail: '', reason: '', manages: false });
  return list;
}

function defaultOption(v, list) {
  const mo2 = list.filter((o) => o.manager === 'mo2' && o.id !== 'custom' && !o.reason);
  const standalone = mo2.filter((o) => !o.adopt);
  const vortex = list.find((o) => o.id === 'vortex');
  const standaloneManaging = standalone.find((o) => o.manages);
  if (v.savedManager === 'mo2') {
    const saved = standaloneManaging || mo2.find((o) => o.manages) || mo2[0];
    if (saved) return saved;
  }
  if (v.savedManager === 'vortex' && vortex) return vortex;
  if (standaloneManaging) return standaloneManaging;
  if (vortex) return vortex;
  if (mo2.length) return null;
  return list.find((o) => o.id === 'none');
}

function chosenOption(v) {
  const list = whereOptions(v);
  return list.find((o) => o.id === wiz.choice && !o.reason) || defaultOption(v, list);
}

function chosenManager(v) {
  const option = chosenOption(v);
  return option ? { manager: option.manager, instance: option.instance } : null;
}

function whereHint(v) {
  if (!v) return '';
  const option = chosenOption(v);
  if (!option) return 'Choose where to install.';
  if (option.id === 'custom' && !option.instance) return 'Type the folder first.';
  return '';
}

function installAllowed(v) {
  if (!v || wiz.step !== STEP_CHECK || wiz.running || app.busy) return false;
  const pick = chosenManager(v);
  if (!pick) return false;
  const blocked = v.gameRunning || (pick.manager === 'mo2' && v.mo2Running) || (pick.manager === 'mo2' && !pick.instance);
  return gameOk(v) && addressOk(v) && !!v.f4sePresent && !blocked;
}

function refreshNav() {
  const pick = wiz.view ? chosenManager(wiz.view) : null;
  $('setupNext').disabled = !pick || app.busy || wiz.pendingScan || (pick.manager === 'mo2' && !pick.instance);
  $('setupInstall').disabled = !installAllowed(wiz.view);
  const hint = $('setupWhereHint');
  if (hint) {
    hint.textContent = whereHint(wiz.view);
    hint.hidden = !hint.textContent;
  }
}

function requirement(list, ok, text, fixLabel, fix) {
  const row = el('li', ok ? 'check' : 'check guide__warn');
  row.appendChild(el('span', undefined, text));
  const side = el('span');
  if (!ok && fixLabel) {
    side.appendChild(actionButton(fixLabel, fix));
    side.appendChild(document.createTextNode(' '));
  }
  side.appendChild(el('span', ok ? 'chip chip--ok' : 'chip chip--warn', ok ? 'OK' : 'MISSING'));
  row.appendChild(side);
  list.appendChild(row);
}

function choiceRow(option, selected) {
  const row = el('li', option.reason ? 'check choice-row guide__warn' : 'check choice-row');
  row.setAttribute('role', 'presentation');
  const label = el('label', 'choice-row__label');
  const radio = document.createElement('input');
  radio.type = 'radio';
  radio.name = 'setupWhere';
  radio.checked = selected;
  radio.disabled = !!option.reason;
  radio.addEventListener('change', () => {
    wiz.choice = option.id;
    refreshNav();
  });
  const text = el('span', 'choice-row__text');
  text.appendChild(el('span', undefined, option.label));
  if (option.detail) text.appendChild(el('span', 'status-note', option.detail));
  if (option.reason) text.appendChild(el('span', 'chip chip--warn', option.reason));
  else if (option.adopt && option.id !== 'custom') text.appendChild(el('span', 'chip chip--info', 'regular install, will be adopted'));
  if (option.id === 'custom') {
    const input = document.createElement('input');
    input.type = 'text';
    input.autocomplete = 'off';
    input.value = wiz.customDir;
    input.placeholder = 'D:\\Modding\\MO2 instance';
    const selectCustom = () => {
      wiz.choice = 'custom';
      radio.checked = true;
      refreshNav();
    };
    input.addEventListener('focus', selectCustom);
    input.addEventListener('input', () => {
      wiz.customDir = input.value;
      selectCustom();
    });
    text.appendChild(input);
  }
  label.appendChild(radio);
  label.appendChild(text);
  row.appendChild(label);
  return row;
}

function pageWhere(box, v) {
  const heading = el('h2', 'mods-sub', 'WHERE SHOULD IT GO?');
  heading.id = 'setupWhereHeading';
  box.appendChild(heading);
  const option = chosenOption(v);
  const picked = option ? option.id : '';
  const list = el('ol', 'checklist');
  list.setAttribute('role', 'radiogroup');
  list.setAttribute('aria-labelledby', 'setupWhereHeading');
  whereOptions(v).forEach((o) => list.appendChild(choiceRow(o, o.id === picked)));
  box.appendChild(list);
  const hint = el('p', 'status-note guide__warn');
  hint.id = 'setupWhereHint';
  box.appendChild(hint);
  if (gameOk(v)) {
    const line = setupBox(`Fallout 4 ${v.runtime.label} ${v.runtime.gameVersion}: ${v.gameDir} `);
    if (!wiz.editGame) {
      line.appendChild(actionButton('CHANGE', () => {
        wiz.editGame = true;
        render();
      }));
    }
    box.appendChild(line);
  } else {
    box.appendChild(el('p', 'status-note guide__warn', gameProblem(v)));
  }
  if (!gameOk(v) || wiz.editGame) box.appendChild(gameField());
}

function pageCheck(box, v) {
  box.appendChild(el('h2', 'mods-sub', 'WHAT COMMONWEALTH ONLINE NEEDS'));
  const label = v.runtime ? v.runtime.label : '';
  const list = el('ol', 'checklist');
  const found = gameOk(v);
  requirement(list, !!v.gameDir, v.gameDir ? `Fallout 4 found: ${v.gameDir}` : 'Fallout 4 found', null);
  requirement(list, found, found ? `Supported version: ${label} ${v.runtime.gameVersion}` : `Supported version (${SUPPORTED})`, null);
  requirement(list, v.f4sePresent, v.f4sePresent ? 'F4SE (script extender)' : 'F4SE (script extender), needed to start the game', 'OPEN F4SE PAGE', () => send({ cmd: 'setup_open_address_page', page: 'f4se' }));
  let addressText = `Address Library${label ? ` for ${label}` : ''}`;
  if (v.addressStatus === 'present') addressText += ': found';
  else if (v.addressStatus === 'stored') addressText += ': using a saved copy from an earlier setup';
  else if (wiz.addressPath.trim()) addressText += ': using the file you chose';
  else addressText += ': not found';
  requirement(list, addressOk(v), addressText, 'OPEN ADDRESS LIBRARY PAGE', () => send({ cmd: 'setup_open_address_page', page: label === 'OG' ? 'og' : 'search' }));
  requirement(list, !v.gameRunning, 'Fallout 4 is not running', 'CHECK AGAIN', scanSetup);
  const pick = chosenManager(v);
  if (pick && pick.manager === 'mo2') requirement(list, !v.mo2Running, 'Mod Organizer 2 is not running', 'CHECK AGAIN', scanSetup);
  box.appendChild(list);
  const target = chosenOption(v);
  box.appendChild(setupBox(target ? `Installing into: ${target.id === 'custom' ? target.instance : target.label}` : 'Choose where to install. Go back and pick one.'));
  if (found && !v.f4sePresent) {
    box.appendChild(el('p', 'status-note guide__warn', 'F4SE is required before INSTALL turns on. Put it in the Fallout 4 folder, then press CHECK AGAIN.'));
    if (!v.gameRunning) box.appendChild(actionButton('CHECK AGAIN', scanSetup));
  }
  if (!found) box.appendChild(el('p', 'status-note guide__warn', gameProblem(v)));
  if (v.addressStatus === 'missing') {
    box.appendChild(setupBox(NEXUS_NOTE));
    box.appendChild(textField(`Address Library file (.zip, or ${v.addressFile || 'the version-*.bin file'})`, wiz.addressPath, (value) => { wiz.addressPath = value; }));
    box.appendChild(setupBox('7z archives are not supported: extract the archive, then choose the .zip or the .bin file.'));
  }
  const warn = el('div', 'notice notice--warn');
  warn.appendChild(el('p', 'notice__text', v.warning));
  box.appendChild(warn);
}

function pageProgress(box) {
  const list = el('ol', 'checklist');
  const at = SETUP_ORDER.indexOf(wiz.current);
  SETUP_ORDER.forEach((key, index) => {
    const seen = wiz.progress[key];
    let label = SETUP_STEP_LABEL[key];
    let className = 'check check--group';
    if (wiz.failed === key) {
      label += ' - FAILED';
      className = 'check guide__warn';
    } else if ((wiz.result && wiz.result.ok && seen) || (at >= 0 && index < at)) {
      className = 'check';
    } else if (key === wiz.current && wiz.running) {
      label += ' - working';
      className = 'check';
    }
    const row = el('li', className, label);
    if (seen && seen.message) row.appendChild(el('span', 'status-note', ` ${seen.message}`));
    list.appendChild(row);
  });
  box.appendChild(list);
  const download = wiz.progress.download;
  if (download && download.total > 0 && wiz.running) {
    const pct = Math.min(100, Math.round((download.done / download.total) * 100));
    box.appendChild(setupBox(`Downloading ${pct}%`));
  }
  if (wiz.result) {
    const notice = el('div', wiz.result.ok ? 'notice' : 'notice notice--error');
    notice.appendChild(el('p', 'notice__text', wiz.result.message));
    box.appendChild(notice);
  } else if (wiz.running) {
    box.appendChild(setupBox('Working... keep the launcher open.'));
  }
}

const SETUP_RENDER = [pageWhere, pageCheck];

function renderSteps() {
  const list = $('setupSteps');
  list.textContent = '';
  SETUP_PAGES.forEach((name, i) => {
    const item = el('li', i === wiz.step ? 'is-current' : i < wiz.step ? 'is-done' : '', `${i + 1}. ${name}`);
    list.appendChild(item);
  });
}

function renderSetup() {
  if (app.view !== 'setup') return;
  const box = $('setupBody');
  const v = wiz.view;
  box.textContent = '';
  $('setupTitle').textContent = `SETUP - ${SETUP_PAGES[wiz.step].toUpperCase()}`;
  renderSteps();
  if (wiz.step === STEP_RUN) {
    pageProgress(box);
  } else if (!v) {
    box.appendChild(setupBox('Looking for Fallout 4...'));
  } else {
    SETUP_RENDER[wiz.step](box, v);
  }
  $('setupBack').hidden = wiz.step !== STEP_CHECK;
  $('setupBack').disabled = wiz.running;
  $('setupNext').hidden = wiz.step !== STEP_WHERE;
  $('setupInstall').hidden = wiz.step !== STEP_CHECK;
  $('setupFinish').hidden = wiz.step !== STEP_RUN;
  $('setupFinish').disabled = wiz.running || app.busy;
  $('setupFinish').textContent = wiz.result && !wiz.result.ok ? 'TRY AGAIN' : 'PLAY';
  refreshNav();
}

function openSetup() {
  wiz.step = STEP_WHERE;
  wiz.editGame = false;
  wiz.result = null;
  wiz.failed = '';
  wiz.progress = {};
  wiz.current = '';
  wiz.running = false;
  wiz.playPending = false;
  showView('setup');
  scanSetup();
}

function setupAfterBusy() {
  if (wiz.pendingScan && !app.busy) {
    wiz.pendingScan = false;
    send({ cmd: 'setup_scan', game: wiz.game.trim() });
    return;
  }
  if (wiz.playPending && !app.busy) {
    wiz.playPending = false;
    if (enabled('play')) send({ cmd: 'play' });
  }
}

function setupStatusEvent(ev) {
  if (!ev.setupComplete && !wiz.opened) {
    wiz.opened = true;
    wiz.pendingScan = true;
    showView('setup');
  }
}

function setupEvent(ev) {
  wiz.view = ev;
  if (!wiz.game && ev.gameDir) wiz.game = ev.gameDir;
}

function setupProgressEvent(ev) {
  wiz.progress[ev.step] = ev;
  wiz.current = ev.step;
}

function setupResultEvent(ev) {
  wiz.running = false;
  wiz.result = ev;
  wiz.failed = ev.ok ? '' : wiz.current;
  wiz.current = '';
}

function setupAbort(message) {
  if (!wiz.running) return;
  wiz.running = false;
  wiz.result = { type: 'setup_result', ok: false, message };
  wiz.failed = wiz.current;
  wiz.current = '';
  render();
}

function startInstall() {
  const v = wiz.view;
  if (!installAllowed(v)) return;
  const { manager, instance } = chosenManager(v);
  const adopt = !!chosenOption(v).adopt;
  wiz.running = true;
  wiz.progress = {};
  wiz.current = '';
  wiz.failed = '';
  wiz.result = null;
  wiz.step = STEP_RUN;
  send({
    cmd: 'setup_run',
    manager,
    download_dir: (v.downloadDir || v.defaultDownloadDir || '').trim(),
    game: wiz.game.trim(),
    instance,
    address_library_path: wiz.addressPath.trim() || null,
    adopt_unknown: adopt,
    vortex_direct: false,
    acknowledge: true,
  });
  render();
}

function wireSetup() {
  $('setupNext').addEventListener('click', () => {
    if (wiz.step === STEP_WHERE && wiz.view) {
      wiz.step = STEP_CHECK;
      render();
    }
  });
  $('setupBack').addEventListener('click', () => {
    if (wiz.step === STEP_CHECK && !wiz.running) {
      wiz.step = STEP_WHERE;
      render();
    }
  });
  $('setupInstall').addEventListener('click', startInstall);
  $('setupFinish').addEventListener('click', () => {
    if (wiz.running || app.busy) return;
    if (wiz.result && !wiz.result.ok) {
      wiz.result = null;
      wiz.failed = '';
      wiz.step = STEP_CHECK;
      scanSetup();
      render();
      return;
    }
    wiz.playPending = true;
    showView('status');
    send({ cmd: 'init' });
  });
  $('setupAgain').addEventListener('click', () => {
    if (wiz.running) return;
    wiz.opened = true;
    openSetup();
  });
}

function setupMockView(scenario) {
  const dir = 'D:\\CO Modlist';
  const downloads = 'C:\\Users\\you\\AppData\\Local\\CommonwealthOnlineLauncher\\downloads';
  return {
    type: 'setup',
    setupComplete: scenario !== 'setup',
    ackExperimental: scenario !== 'setup',
    downloadDir: '',
    defaultDownloadDir: downloads,
    warning: 'Playing with mods is highly experimental and results may vary.',
    gameDir: 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4',
    gameError: null,
    runtime: { label: 'AE', gameVersion: '1.11.240' },
    unsupported: null,
    f4sePresent: true,
    savedManager: 'none',
    addressFile: 'version-1-11-240-0.bin',
    addressStatus: scenario === 'setup' ? 'missing' : 'present',
    addressPath: scenario === 'setup' ? null : 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4\\Data\\F4SE\\Plugins\\version-1-11-240-0.bin',
    addressKnown: scenario !== 'setup',
    managers: [
      { kind: 'mo2', name: 'CO Modlist', dir, exe: `${dir}\\ModOrganizer.exe`, managesThisGame: true, channel: 'standalone', activeProfile: 'Default' },
      { kind: 'mo2', name: 'Older Mod Organizer 2', dir: 'D:\\Modding\\MO2', exe: 'D:\\Modding\\MO2\\ModOrganizer.exe', managesThisGame: true, channel: 'unknown', activeProfile: 'Default' },
      { kind: 'mo2', name: 'Wabbajack Collection', dir: 'D:\\Wabbajack\\Fallout 4 Collection', exe: 'D:\\Wabbajack\\Fallout 4 Collection\\ModOrganizer.exe', managesThisGame: true, channel: 'wabbajack', activeProfile: 'Default' },
      { kind: 'vortex', name: 'Vortex', dir: `${downloads}\\..\\vortex`, exe: null, managesThisGame: false, channel: 'vortex', activeProfile: null },
    ],
    gameRunning: false,
    mo2Running: false,
  };
}

function setupMock(play, busy, scenario) {
  const reply = (ok, message) => ({ type: 'setup_result', ok, message });
  const progress = (step, extra) => ({ type: 'setup_progress', step, message: '', done: 0, total: 0, ...extra });
  return {
    setup_get: () => play([[0, setupMockView(scenario)]]),
    setup_scan: () => play([[0, setupMockView(scenario)]]),
    setup_reset: () => play([[0, setupMockView('setup')]]),
    setup_run: (cmd) => {
      if (!cmd.acknowledge) {
        play([[0, reply(false, 'Read the warning and confirm you understand it before setup runs.')]]);
        return;
      }
      play([
        [0, busy(true, 'setup')],
        [300, progress('detect', { message: 'Found Fallout 4 AE.' })],
        [300, progress('download', { done: 1, total: 4 })],
        [300, progress('download', { done: 4, total: 4 })],
        [300, progress('verify', { message: 'Checked against the signed manifest.' })],
        [300, progress('address_library', { message: 'version-1-11-240-0.bin' })],
        [300, progress('link', { message: 'Linking the files.' })],
        [300, reply(true, 'Commonwealth Online 0.0.0.11 (AE) is installed and turned on. Address Library was linked into that instance.')],
        [0, setupMockView('ok')],
        [0, busy(false, 'setup')],
      ]);
    },
  };
}
