'use strict';

const MODS_CAUTION = 'Adding or removing mods in Mod Organizer 2 may cause issues. Only add mods if you understand what you are doing.';

function openMods() {
  app.mods = null;
  showView('mods');
  app.modsWanted = app.busy;
  if (!app.busy) send({ cmd: 'mods' });
}

function modsPrimary(m) {
  return m.ours || m.managesThisGame;
}

function modsOrder(a, b) {
  return (b.ours - a.ours) || (b.managesThisGame - a.managesThisGame) || a.name.localeCompare(b.name);
}

function modsProfiles(m) {
  if (!m.profilesListed || !m.profiles.length) return 'No profiles found.';
  const active = m.profiles.find((p) => p.active) || m.profiles[0];
  const count = active.enabledMods === null ? '' : `, ${active.enabledMods} mods enabled`;
  const others = m.profiles.filter((p) => p !== active).map((p) => p.name);
  return `Profile in use: ${active.name}${count}.${others.length ? ` Also: ${others.join(', ')}.` : ''}`;
}

function modsHead(m) {
  const head = el('div', 'mods-card__head');
  head.append(el('span', 'mods-card__name', m.name), el('span', 'status-note', 'MOD ORGANIZER 2'));
  if (m.ours) head.appendChild(el('span', 'chip chip--ok', 'COMMONWEALTH ONLINE SETUP'));
  else if (m.managesThisGame) head.appendChild(el('span', 'chip chip--ok', 'USES YOUR GAME FOLDER'));
  return head;
}

let modsTab = 'mods';

function modsLine(name, on, separator) {
  const row = el('li', separator ? 'check check--group' : 'check');
  row.appendChild(el('span', 'check__name', name));
  if (!separator) row.appendChild(el('span', on ? 'chip chip--ok' : 'chip chip--warn', on ? 'ON' : 'OFF'));
  return row;
}

function modsTabs(counts) {
  const bar = el('div', 'mods-tabs');
  bar.setAttribute('role', 'tablist');
  [['mods', 'MODS'], ['plugins', 'PLUGINS']].forEach(([key, label]) => {
    const tab = el('button', 'btn mods-tab', `${label} (${counts[key]})`);
    tab.type = 'button';
    tab.setAttribute('role', 'tab');
    tab.setAttribute('aria-selected', String(modsTab === key));
    tab.addEventListener('click', () => {
      modsTab = key;
      renderMods();
    });
    bar.appendChild(tab);
  });
  return bar;
}

function modsDetail(d) {
  const mods = d.mods.filter((x) => !x.separator);
  const box = el('div', 'mods-detail');
  if (d.f4seNote) box.appendChild(el('p', 'status-note', d.f4seNote));
  box.appendChild(el('p', 'status-note', `Profile folder: ${d.dir}. Tracking: ${d.files.join(', ') || 'nothing'}.`));
  box.appendChild(modsTabs({ mods: `${mods.filter((x) => x.enabled).length} ON OF ${mods.length}`, plugins: `${d.plugins.filter((x) => x.enabled).length} ON OF ${d.plugins.length}` }));
  const list = el('ul', 'checklist');
  const rows = modsTab === 'mods' ? d.mods.map((x) => modsLine(x.name, x.enabled, x.separator)) : d.plugins.map((x) => modsLine(x.name, x.enabled, false));
  rows.forEach((r) => list.appendChild(r));
  if (!rows.length) box.appendChild(el('p', 'status-note', 'Nothing listed.'));
  box.appendChild(list);
  return box;
}

function managerCard(m) {
  const card = el('section', 'card mods-card');
  card.append(modsHead(m), el('p', 'status-note', m.dir), el('p', 'status-note', modsProfiles(m)));
  if (m.ours) {
    const open = el('button', 'btn', 'OPEN MOD ORGANIZER 2');
    open.type = 'button';
    open.disabled = !m.exe || app.busy;
    open.addEventListener('click', () => send({ cmd: 'open_mo2' }));
    card.append(el('p', 'status-note', MODS_CAUTION), open);
  }
  if (m.detail) card.appendChild(modsDetail(m.detail));
  return card;
}

function managerRow(m) {
  const row = el('div', 'mods-row');
  row.append(modsHead(m), el('p', 'status-note', `${m.dir}. ${modsProfiles(m)}`));
  return row;
}

function renderMods() {
  const m = app.mods;
  $('modsWarning').hidden = !m;
  $('modsWarningText').textContent = m ? m.warning : '';
  $('modsEmpty').hidden = !m || m.managers.length > 0;
  $('modsWaiting').hidden = !!m || app.busy;
  $('modsFailure').hidden = !app.failure;
  $('modsFailure').textContent = app.failure;
  $('modsRescan').disabled = app.busy;
  const list = $('modsList');
  list.textContent = '';
  if (!m) return;
  const sorted = m.managers.slice().sort(modsOrder);
  sorted.filter(modsPrimary).forEach((x) => list.appendChild(managerCard(x)));
  const rest = sorted.filter((x) => !modsPrimary(x));
  if (!rest.length) return;
  list.appendChild(el('h3', 'mods-sub', 'OTHER SETUPS ON THIS PC (NOT USED BY THIS INSTALL)'));
  rest.forEach((x) => list.appendChild(managerRow(x)));
}

function wireMods() {
  $('modsRescan').addEventListener('click', () => {
    if (!app.busy) openMods();
  });
  window.addEventListener('focus', () => {
    if (app.view === 'mods' && !app.busy) send({ cmd: 'mods' });
  });
  $('modsBack').addEventListener('click', () => showView('status'));
}

function modsMock(play, busy) {
  const warning = 'Modding besides our selection is not yet supported. Commonwealth Online is tested only with the files the launcher installs; other mods, load orders and profiles may break it.';
  const managers = [
    {
      name: 'CO Modlist', dir: 'D:\\CO Modlist', exe: 'D:\\CO Modlist\\ModOrganizer.exe',
      gameDir: 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4', managesThisGame: true, ours: true, profilesListed: true,
      detail: {
        f4seNote: 'Using F4SE AE 0.7.9 for your game.', profile: 'Default', dir: 'D:\\CO Modlist\\profiles\\Default', files: ['loadorder.txt', 'modlist.txt', 'plugins.txt'],
        mods: [
          { name: 'PrismaUI', enabled: true, separator: false },
          { name: 'F4SE AE 0.7.9', enabled: true, separator: false },
        ],
        plugins: [{ name: 'Fallout4.esm', enabled: true }, { name: 'DLCRobot.esm', enabled: true }],
      },
      profiles: [{ name: 'Default', enabledMods: 214, active: true }, { name: 'Vanilla plus', enabledMods: 31, active: false }],
    },
    {
      name: 'Fallout 4 (NG)', dir: 'D:\\Modding\\MO2-NG', exe: 'D:\\Modding\\MO2\\ModOrganizer.exe',
      gameDir: 'D:\\FO4-NG-1.10.984', managesThisGame: false, ours: false, profilesListed: true, detail: null,
      profiles: [{ name: 'Default', enabledMods: 0, active: true }],
    },
  ];
  return () => play([[0, busy(true, 'mods')], [300, { type: 'mods', managers, warning }], [0, busy(false, 'mods')]]);
}
