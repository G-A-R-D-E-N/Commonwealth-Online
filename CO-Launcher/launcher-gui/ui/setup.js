'use strict';

const SETUP_PAGES = ['Welcome', 'Game and manager', 'Address Library and folder', 'Review and install', 'Progress and summary'];
const SETUP_ACK = 'I understand that playing with mods is highly experimental and results may vary.';
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
  picked: false,
  pendingScan: false,
  step: 0,
  manager: 'mo2',
  instance: '',
  game: '',
  addressPath: '',
  adoptUnknown: false,
  vortexDirect: false,
  ack: false,
  downloadDir: '',
  progress: {},
  current: '',
  failed: '',
  running: false,
  result: null,
};

function setupBox(text, className) {
  return el('p', className || 'status-note', text);
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
  const row = el('label', 'location-field', 'Fallout 4 folder (leave blank to use the detected one)');
  const input = document.createElement('input');
  input.type = 'text';
  input.autocomplete = 'off';
  input.value = wiz.game;
  input.placeholder = 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4';
  input.addEventListener('input', () => {
    wiz.game = input.value;
    refreshNav();
  });
  input.addEventListener('change', () => {
    wiz.game = input.value;
    if (app.busy) wiz.pendingScan = true;
    else send({ cmd: 'setup_scan', game: wiz.game.trim() });
  });
  row.appendChild(input);
  return row;
}

function checkField(label, checked, onChange) {
  const row = el('label', 'check');
  const box = document.createElement('input');
  box.type = 'checkbox';
  box.checked = !!checked;
  box.addEventListener('change', () => {
    onChange(box.checked);
    renderSetup();
  });
  row.append(box, document.createTextNode(` ${label}`));
  return row;
}

function radioField(name, label, checked, onPick) {
  const row = el('label', 'check');
  const box = document.createElement('input');
  box.type = 'radio';
  box.name = name;
  box.checked = !!checked;
  box.addEventListener('change', () => {
    if (box.checked) onPick();
    renderSetup();
  });
  row.append(box, document.createTextNode(` ${label}`));
  return row;
}

function gameOk(v) {
  return !!(v && v.gameDir && v.runtime);
}

function mo2Choice(v) {
  return (v.managers || []).find((m) => m.kind === 'mo2' && m.dir === wiz.instance) || null;
}

function managerOk(v) {
  if (wiz.manager === 'none' || wiz.manager === 'vortex') return true;
  if (!wiz.instance.trim()) return false;
  const chosen = mo2Choice(v);
  if (chosen && chosen.channel === 'wabbajack') return false;
  if (!chosen || chosen.channel === 'unknown') return wiz.adoptUnknown;
  return true;
}

function nextAllowed(v) {
  if (wiz.step === 1) return !!v && gameOk(v) && managerOk(v);
  return true;
}

function addressOk(v) {
  return !!v && (v.addressStatus !== 'missing' || !!wiz.addressPath.trim());
}

function installAllowed(v) {
  return !!v && wiz.step === 3 && wiz.ack && gameOk(v) && managerOk(v) && addressOk(v) && !wiz.running && !app.busy;
}

function refreshNav() {
  const v = wiz.view;
  $('setupNext').disabled = !nextAllowed(v);
  $('setupInstall').disabled = !installAllowed(v);
}

function managerLabel() {
  if (wiz.manager === 'mo2') return `Mod Organizer 2: ${wiz.instance || '(no folder chosen)'}`;
  if (wiz.manager === 'vortex') return 'Vortex (zip files for Install From File)';
  return 'No mod manager (files are copied into the game Data folder)';
}

function pageWelcome(box, v) {
  box.appendChild(el('h2', 'mods-sub', 'SET UP COMMONWEALTH ONLINE'));
  box.appendChild(setupBox('This wizard downloads Commonwealth Online for your Fallout 4 version, links the Address Library it needs, and sets up Mod Organizer 2 or Vortex.'));
  const warn = el('div', 'notice notice--warn');
  warn.appendChild(el('p', 'notice__text', v.warning));
  box.appendChild(warn);
  box.appendChild(setupBox(v.prereleaseNote));
  box.appendChild(setupBox('Nothing is changed until you press INSTALL on the review page.'));
}

function pageGame(box, v) {
  const card = el('section', 'card');
  card.appendChild(el('h2', 'mods-sub', 'FALLOUT 4'));
  if (v.gameDir) card.appendChild(setupBox(`Game folder: ${v.gameDir}`));
  if (v.runtime) card.appendChild(setupBox(`Version: ${v.runtime.label} ${v.runtime.gameVersion}`));
  if (v.unsupported) card.appendChild(el('p', 'status-note guide__warn', `Fallout 4 ${v.unsupported} is not supported. Supported: 1.10.163 (OG), 1.10.984 (NG), 1.11.240 (AE).`));
  if (v.gameError) card.appendChild(el('p', 'status-note guide__warn', v.gameError));
  card.appendChild(gameField());
  (v.dependencyNotes || []).forEach((note) => card.appendChild(el('p', 'status-note guide__warn', note)));
  if (v.f4seVersion) card.appendChild(setupBox(`F4SE version: ${v.f4seVersion}`));
  card.appendChild(setupBox(v.checksNote));
  if ((v.findings || []).length) {
    card.appendChild(setupBox(`Files to review in your game folder (${v.findings.length}). The launcher does not remove or overwrite any of them.`));
    const list = el('ol', 'checklist');
    v.findings.forEach((f) => list.appendChild(el('li', 'check', `${f.path}: ${f.reason}`)));
    card.appendChild(list);
  }
  card.appendChild(setupBox(v.verifyNote));
  if (!v.gogInstall) {
    const steam = el('button', 'btn', 'CHECK GAME FILES IN STEAM');
    steam.type = 'button';
    steam.addEventListener('click', () => send({ cmd: 'setup_open_address_page', page: 'steam_verify' }));
    card.appendChild(steam);
  }
  if (!v.f4sePresent) {
    const f4se = el('button', 'btn', 'OPEN F4SE DOWNLOAD');
    f4se.type = 'button';
    f4se.addEventListener('click', () => send({ cmd: 'setup_open_address_page', page: 'f4se' }));
    card.appendChild(f4se);
  }
  box.appendChild(card);

  box.appendChild(el('h2', 'mods-sub', 'WHERE SHOULD IT GO?'));
  if (v.savedManager && v.savedManager !== 'none') box.appendChild(setupBox(`Last setup used: ${v.savedManager}.`));
  if (!(v.managers || []).some((m) => m.kind === 'mo2')) {
    box.appendChild(setupBox('To let the launcher install and enable the mod for you, install Mod Organizer 2 and create a Fallout 4 instance.'));
    const mo2 = el('button', 'btn', 'OPEN MOD ORGANIZER 2');
    mo2.type = 'button';
    mo2.addEventListener('click', () => send({ cmd: 'setup_open_address_page', page: 'mo2' }));
    box.appendChild(mo2);
  }
  (v.managers || []).filter((m) => m.kind === 'mo2').forEach((m) => {
    const note = m.channel === 'wabbajack' ? ' (Wabbajack: read only)' : m.channel === 'standalone' ? ' (already set up by the launcher)' : ' (not made by the launcher)';
    box.appendChild(radioField('manager', `Mod Organizer 2: ${m.name}, ${m.dir}${note}`, wiz.manager === 'mo2' && wiz.instance === m.dir, () => {
      wiz.manager = 'mo2';
      wiz.instance = m.dir;
    }));
  });
  box.appendChild(radioField('manager', 'Mod Organizer 2, another folder (type it below)', wiz.manager === 'mo2' && !(v.managers || []).some((m) => m.kind === 'mo2' && m.dir === wiz.instance), () => {
    wiz.manager = 'mo2';
    wiz.instance = '';
  }));
  if (wiz.manager === 'mo2') {
    box.appendChild(textField('Mod Organizer 2 instance folder', wiz.instance, (value) => { wiz.instance = value; }, 'D:\\CO Modlist'));
    const chosen = mo2Choice(v);
    if (chosen && chosen.channel === 'wabbajack') {
      box.appendChild(el('p', 'status-note guide__warn', 'This instance is managed by Wabbajack. The launcher will not change it; update the modlist in Wabbajack instead.'));
    } else if (!chosen || chosen.channel === 'unknown') {
      box.appendChild(checkField('Let the launcher manage this instance (it was not made by the launcher).', wiz.adoptUnknown, (on) => { wiz.adoptUnknown = on; }));
    }
  }
  if ((v.managers || []).some((m) => m.kind === 'vortex')) {
    box.appendChild(radioField('manager', 'Vortex', wiz.manager === 'vortex', () => { wiz.manager = 'vortex'; }));
  }
  if (wiz.manager === 'vortex') {
    box.appendChild(setupBox('Vortex: the launcher writes ready-to-install zips into your download folder. It never writes into Vortex\'s own state. The Vortex list in the Mods tab is read-only and may be stale.'));
    box.appendChild(checkField('Also copy the Address Library into the Fallout 4 Data folder now (Vortex will report that as an outside change).', wiz.vortexDirect, (on) => { wiz.vortexDirect = on; }));
    if (v.vortexRunning) box.appendChild(el('p', 'status-note guide__warn', 'Vortex is running. Close it before you deploy the new zips.'));
  }
  box.appendChild(radioField('manager', 'I do not use a mod manager', wiz.manager === 'none', () => { wiz.manager = 'none'; }));
  if (v.gameRunning) box.appendChild(el('p', 'status-note guide__warn', 'Fallout 4 is running. Close it before you install.'));
  if (wiz.manager === 'mo2' && v.mo2Running) box.appendChild(el('p', 'status-note guide__warn', 'Mod Organizer 2 is running. Close it before you install.'));
}

function pageAddress(box, v) {
  const label = v.runtime ? v.runtime.label : '';
  box.appendChild(el('h2', 'mods-sub', `ADDRESS LIBRARY${label ? ` FOR FALLOUT 4 ${label}` : ''}`));
  if (v.addressStatus === 'present') {
    box.appendChild(setupBox(`Found: ${v.addressPath}${v.addressKnown ? '' : ' (not on the known list, so it is used as it is)'}`));
  } else if (v.addressStatus === 'stored') {
    box.appendChild(setupBox('A copy from an earlier setup is stored in your download folder and will be used.'));
  } else {
    const warn = el('div', 'notice notice--warn');
    warn.appendChild(el('p', 'notice__text', `Not found. Download ${v.addressFile || 'the Address Library'} yourself, then choose it below.`));
    box.appendChild(warn);
    box.appendChild(setupBox(NEXUS_NOTE));
    const open = el('button', 'btn', 'OPEN THE ADDRESS LIBRARY PAGE');
    open.type = 'button';
    open.addEventListener('click', () => send({ cmd: 'setup_open_address_page', page: label === 'OG' ? 'og' : 'search' }));
    box.appendChild(open);
  }
  box.appendChild(textField(`Address Library file, optional (.zip, or ${v.addressFile || 'the version-*.bin file'}); used when no copy is found above`, wiz.addressPath, (value) => { wiz.addressPath = value; }));
  box.appendChild(setupBox('7z archives are not supported: extract the archive, then choose the .zip or the .bin file.'));
  box.appendChild(el('h2', 'mods-sub', 'DOWNLOAD FOLDER'));
  box.appendChild(textField('Download folder (full path)', wiz.downloadDir, (value) => { wiz.downloadDir = value; }));
  box.appendChild(setupBox('The release is saved in a folder named after its version inside this folder. The Address Library copy is kept here too.'));
  box.appendChild(setupBox(v.prereleaseNote));
}

function pageReview(box, v) {
  box.appendChild(el('h2', 'mods-sub', 'REVIEW'));
  const card = el('section', 'card');
  card.appendChild(setupBox(`Fallout 4: ${v.gameDir || '(not found)'}`));
  card.appendChild(setupBox(`Version: ${v.runtime ? `${v.runtime.label} ${v.runtime.gameVersion}` : '(unknown)'}`));
  card.appendChild(setupBox(`Manager: ${managerLabel()}`));
  card.appendChild(setupBox(`Address Library: ${v.addressStatus === 'missing' ? 'not found yet' : v.addressStatus}`));
  card.appendChild(setupBox(`Download folder: ${wiz.downloadDir || '(not set)'}`));
  box.appendChild(card);
  const warn = el('div', 'notice notice--warn');
  warn.appendChild(el('p', 'notice__text', v.warning));
  box.appendChild(warn);
  if (wiz.result && !wiz.result.ok) {
    const failure = el('div', 'notice notice--error');
    failure.appendChild(el('p', 'notice__text', wiz.result.message));
    box.appendChild(failure);
  }
  box.appendChild(checkField(SETUP_ACK, wiz.ack, (on) => { wiz.ack = on; }));
  if (!addressOk(v)) box.appendChild(el('p', 'status-note guide__warn', 'Choose the Address Library file on the previous page first.'));
  if (!gameOk(v)) box.appendChild(el('p', 'status-note guide__warn', 'Go back and choose a Fallout 4 folder with a supported version.'));
  else if (!managerOk(v)) box.appendChild(el('p', 'status-note guide__warn', 'Go back and check the manager choice.'));
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

const SETUP_RENDER = [pageWelcome, pageGame, pageAddress, pageReview, pageProgress];

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
  if (!v) {
    box.appendChild(setupBox('Looking at your Fallout 4 and Mod Organizer 2 setup...'));
  } else if (wiz.step === 4) {
    pageProgress(box);
  } else {
    SETUP_RENDER[wiz.step](box, v);
  }
  $('setupBack').hidden = wiz.step === 0 || wiz.step === 4;
  $('setupBack').disabled = wiz.running;
  $('setupNext').hidden = wiz.step >= 3;
  $('setupInstall').hidden = wiz.step !== 3;
  $('setupFinish').hidden = wiz.step !== 4;
  $('setupFinish').disabled = wiz.running;
  $('setupFinish').textContent = wiz.result && !wiz.result.ok ? 'TRY AGAIN' : 'FINISH';
  refreshNav();
}

function openSetup() {
  wiz.step = 0;
  wiz.result = null;
  wiz.failed = '';
  wiz.progress = {};
  wiz.current = '';
  wiz.running = false;
  wiz.ack = false;
  showView('setup');
  if (app.busy) wiz.pendingScan = true;
  else send({ cmd: 'setup_scan', game: wiz.game.trim() });
}

function setupAfterBusy() {
  if (wiz.pendingScan && !app.busy) {
    wiz.pendingScan = false;
    send({ cmd: 'setup_scan', game: wiz.game.trim() });
  }
}

function setupStatusEvent(ev) {
  if (!ev.setupComplete && !wiz.opened) {
    wiz.opened = true;
    wiz.pendingScan = true;
    showView('setup');
  }
}

function pickDefaults(v) {
  const mo2 = (v.managers || []).filter((m) => m.kind === 'mo2');
  const best = mo2.find((m) => m.managesThisGame) || mo2[0];
  if (best) {
    wiz.manager = 'mo2';
    wiz.instance = best.dir;
  } else if ((v.managers || []).some((m) => m.kind === 'vortex')) {
    wiz.manager = 'vortex';
  } else {
    wiz.manager = 'none';
  }
  wiz.picked = true;
}

function setupEvent(ev) {
  wiz.view = ev;
  if (!wiz.downloadDir) wiz.downloadDir = ev.downloadDir || ev.defaultDownloadDir || '';
  if (!wiz.picked) pickDefaults(ev);
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
  wiz.step = 3;
  render();
}

function startInstall() {
  const v = wiz.view;
  if (!installAllowed(v)) return;
  wiz.running = true;
  wiz.progress = {};
  wiz.current = '';
  wiz.failed = '';
  wiz.result = null;
  wiz.step = 4;
  send({
    cmd: 'setup_run',
    manager: wiz.manager,
    download_dir: wiz.downloadDir.trim(),
    game: wiz.game.trim(),
    instance: wiz.manager === 'mo2' ? wiz.instance.trim() : '',
    address_library_path: wiz.addressPath.trim() || null,
    adopt_unknown: wiz.adoptUnknown,
    vortex_direct: wiz.vortexDirect,
    acknowledge: wiz.ack,
  });
  render();
}

function wireSetup() {
  $('setupNext').addEventListener('click', () => {
    if (wiz.step < 3 && nextAllowed(wiz.view)) {
      wiz.step += 1;
      render();
    }
  });
  $('setupBack').addEventListener('click', () => {
    if (wiz.step > 0 && wiz.step < 4 && !wiz.running) {
      wiz.step -= 1;
      render();
    }
  });
  $('setupInstall').addEventListener('click', startInstall);
  $('setupFinish').addEventListener('click', () => {
    if (wiz.running) return;
    if (wiz.result && !wiz.result.ok) {
      wiz.result = null;
      wiz.failed = '';
      wiz.step = 3;
      render();
      return;
    }
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
    prereleaseNote: 'Setup turns on pre-release builds so you get the newest unified Commonwealth Online build.',
    gameDir: 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4',
    gameError: null,
    runtime: { label: 'AE', gameVersion: '1.11.240' },
    unsupported: null,
    f4sePresent: false,
    f4seVersion: null,
    findings: [{ path: 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4\\Data\\custom.esp', reason: 'A plugin file loose in the Data folder. The launcher did not install it, so it may conflict with other plugins.' }],
    dependencyNotes: ['The script extender F4SE is not in the game folder. Commonwealth Online needs it to start.'],
    checksNote: 'The launcher lists file names in the game folder to find common mod files. It does not check what they contain, so an empty list does not prove there are no problems.',
    gogInstall: false,
    verifyNote: "Steam can check Fallout 4's game files and replace any that are missing or changed. Use it if the game will not start. Replacing game files can undo mod changes to them.",
    savedManager: 'none',
    addressFile: 'version-1-11-240-0.bin',
    addressStatus: 'missing',
    addressPath: null,
    addressKnown: false,
    managers: [
      { kind: 'mo2', name: 'CO Modlist', dir, exe: `${dir}\\ModOrganizer.exe`, managesThisGame: true, channel: 'unknown', activeProfile: 'Default' },
      { kind: 'vortex', name: 'Vortex', dir: `${downloads}\\..\\vortex`, exe: null, managesThisGame: false, channel: 'vortex', activeProfile: null },
    ],
    gameRunning: false,
    mo2Running: false,
    vortexRunning: false,
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
        play([[0, reply(false, 'Read the warning and tick the box to confirm you understand it before setup runs.')]]);
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
