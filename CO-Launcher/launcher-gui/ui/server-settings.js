'use strict';

const SETTING_GROUPS = [
  {
    title: 'YOUR SERVER',
    items: [
      { k: 'name', t: 'text', label: 'Server name', maxLength: 64, help: 'The name players see for your server (1 to 64 characters; accented letters count as more than one). Only used by a DEDICATED server.' },
      { k: 'description', t: 'text', label: 'Description', maxLength: 256, help: 'A short message about your server. Optional.' },
      { k: 'maxPlayers', t: 'int', label: 'Most players at once', min: 1, max: 16, help: 'How many players can be on the server together (1 to 16). Only used by a DEDICATED server.' },
      { k: 'password', t: 'password', label: 'Password', maxLength: 64, help: 'Players must type this to join. Leave it empty for no password. Use 4 to 64 letters, numbers or symbols, with no accents.' },
      { k: 'port', t: 'int', label: 'Game port', min: 1, max: 65535, help: 'The network port (UDP) that players connect to. Leave it at 7777 unless another program uses it. It cannot be the admin port or the LAN discovery port (7778 unless you changed it). Only used by a DEDICATED server.' },
      { k: 'publicListing', t: 'bool', modes: ['dedicated'], label: 'List this server publicly', help: 'Puts the server in the public server list, so anyone can find and join it. Turn it off to keep it private. A server behind CGNAT, or behind a home router without a public address, cannot be listed.' },
    ],
  },
  {
    title: 'HOW THE GAME PLAYS',
    items: [
      { k: 'worldMode', t: 'select', label: 'Game mode', options: [['coop-story', 'Co-op story'], ['free-roam', 'Free roam']], help: 'Co-op story: everyone plays the normal story together. Free roam: the main story and companions are switched off, so everyone just explores.' },
      { k: 'vats', t: 'select', label: 'VATS targeting', options: [['disabled', 'Off'], ['real-time', 'On, without slow motion'], ['vanilla', 'On, with slow motion']], help: 'Off is the safe choice. Slow motion can put players out of sync, so it is not recommended.' },
      { k: 'doorsShared', t: 'bool', label: 'Shared doors', help: 'When one player opens or closes a door, it changes for everybody.' },
      { k: 'friendlyFire', t: 'bool', label: 'Players can hurt each other', help: 'Turn this off to stop players from damaging one another. Monsters and the environment still hurt.' },
      { k: 'factionFriendlyFire', t: 'bool', label: 'Faction members can hurt each other', help: 'Only matters when players can hurt each other. Off keeps people in the same faction from damaging one another.' },
    ],
  },
  {
    title: 'FEATURES',
    items: [
      { k: 'chat', t: 'bool', label: 'Text chat', help: 'Players can send text messages to everybody.' },
      { k: 'factions', t: 'bool', label: 'Player factions', help: 'Players can create and join factions. Factions are only remembered between restarts when saving progress is on.' },
      { k: 'persistence', t: 'bool', label: 'Save progress', help: 'The server remembers players and factions between restarts.' },
      { k: 'steamFriendsOnly', t: 'bool', label: 'Steam friends only', help: 'When a player hosts through Steam, only their Steam friends can join. Turn this off together with a password to let others join.' },
    ],
  },
  {
    title: 'DOWNED PLAYERS',
    items: [
      { k: 'downedSeconds', t: 'int', label: 'Seconds before a downed player dies', min: 5, max: 600, help: 'How long a downed player can wait for help (5 to 600 seconds).' },
      { k: 'reviveSeconds', t: 'int', label: 'Seconds to revive a friend', min: 0, max: 15, help: 'How long a teammate must stay with a downed player to bring them back (0 to 15 seconds).' },
    ],
  },
];
const SETTING_ITEMS = SETTING_GROUPS.flatMap((g) => g.items);
const SETTING_NUMBERS = SETTING_ITEMS.filter((i) => i.t === 'int');

const form = { controls: {}, rows: {}, key: '' };

function settingVisible(item) {
  return !item.modes || item.modes.includes(app.hostMode);
}

function buildSettingRow(item) {
  const control = makeControl(item);
  form.controls[item.k] = control;
  const row = el('div', 'srv-set');
  form.rows[item.k] = row;
  const text = el('div', 'srv-set__text');
  text.append(el('span', 'srv-set__name', item.label.toUpperCase()), el('p', 'srv-set__help', item.help));
  const side = el('div', 'srv-set__control');
  side.appendChild(control.node);
  if (item.k === 'password') {
    const show = el('button', 'btn', 'SHOW');
    show.type = 'button';
    show.addEventListener('click', () => {
      const hidden = control.node.type === 'password';
      control.node.type = hidden ? 'text' : 'password';
      show.textContent = hidden ? 'HIDE' : 'SHOW';
    });
    side.appendChild(show);
  }
  row.append(text, side);
  control.node.addEventListener('input', markEdited);
  control.node.addEventListener('change', markEdited);
  return row;
}

function fillSettings(settings) {
  SETTING_ITEMS.forEach((i) => {
    const c = form.controls[i.k];
    if (i.t === 'bool') c.node.checked = !!settings[i.k];
    else c.node.value = settings[i.k] === undefined || settings[i.k] === null ? '' : String(settings[i.k]);
  });
}

function buildSettings() {
  const box = $('serverForm');
  box.textContent = '';
  form.controls = {};
  form.rows = {};
  SETTING_GROUPS.forEach((g) => {
    box.appendChild(el('h2', 'mods-sub', g.title));
    g.items.forEach((item) => box.appendChild(buildSettingRow(item)));
  });
}

function readSettings() {
  const out = {};
  SETTING_ITEMS.forEach((i) => {
    const c = form.controls[i.k];
    out[i.k] = i.t === 'bool' ? c.node.checked : i.t === 'int' ? (c.node.value.trim() === '' ? NaN : Number(c.node.value)) : i.t === 'password' ? c.node.value : c.node.value.trim();
  });
  return out;
}

function settingsProblem(settings) {
  const bad = SETTING_NUMBERS.find((i) => !Number.isInteger(settings[i.k]) || settings[i.k] < i.min || settings[i.k] > i.max);
  if (bad) return `${bad.label}: type a whole number from ${bad.min} to ${bad.max}.`;
  return settings.name ? '' : 'Give your server a name.';
}

function markEdited() {
  form.edited = true;
  $('serverFormNote').textContent = 'You have unsaved changes. Press SAVE to keep them.';
  $('serverReset').disabled = app.busy;
}

function settingsNote(running) {
  if (app.hostMode !== 'dedicated') {
    return 'When you host through the game (STEAM P2P), the game sets the server name, game port and number of players itself, so those three only apply to a DEDICATED server. Everything else is read when the server starts. A server started by the game can only be restarted from the game.';
  }
  return running
    ? 'The server is running. Saved changes take effect the next time it starts. SAVE AND RESTART applies them now and disconnects everyone.'
    : 'Changes are saved to the server and take effect the next time it starts.';
}

function renderServerSettings(s, running) {
  const settings = s && s.settings;
  if (!settings) return;
  const key = JSON.stringify(settings);
  if (!form.built) {
    buildSettings();
    form.built = true;
  }
  if (key !== form.key) {
    fillSettings(settings);
    form.key = key;
    form.edited = false;
  }
  if (!form.edited) {
    $('serverFormNote').textContent = settingsNote(running);
  }
  SETTING_ITEMS.forEach((i) => {
    if (form.rows[i.k]) form.rows[i.k].hidden = !settingVisible(i);
  });
  Object.values(form.controls).forEach((c) => { c.node.disabled = app.busy; });
  $('serverSave').disabled = app.busy;
  $('serverReset').disabled = app.busy || !form.edited;
  $('serverSaveRestart').hidden = !(running && app.hostMode === 'dedicated');
  $('serverSaveRestart').disabled = app.busy;
}

function saveSettings(restart) {
  if (app.busy) return;
  const settings = readSettings();
  const problem = settingsProblem(settings);
  if (problem) {
    app.reply = { ok: false, text: problem };
    app.replyTo = 'settings';
    renderServer();
    return;
  }
  const go = () => {
    app.restartAfterSave = restart;
    app.sent = { label: 'SAVE SETTINGS', to: 'settings' };
    send({ cmd: 'server_save', settings, restart: !!restart, mode: app.hostMode });
  };
  if (restart) askConfirm('SAVE AND RESTART? EVERYONE IS DISCONNECTED.', go);
  else go();
}

function settingsReplied(ev) {
  app.restartNext = !!app.restartAfterSave && ev.ok;
  if (app.restartNext) ev.text = 'Settings saved. Restarting the server now.';
  app.restartAfterSave = false;
  if (ev.ok) form.edited = false;
}

function wireServerSettings() {
  $('serverSave').addEventListener('click', () => saveSettings(false));
  $('serverSaveRestart').addEventListener('click', () => saveSettings(true));
  $('serverReset').addEventListener('click', () => {
    const settings = app.server && app.server.settings;
    if (!settings) return;
    fillSettings(settings);
    form.edited = false;
    renderServer();
  });
}
