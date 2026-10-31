'use strict';

const ARG = {
  player: { k: 'playerId', t: 'int', min: 1, label: 'Player number', hint: 'player number', req: true },
  reason: { k: 'reason', t: 'text', maxLength: 120, label: 'Reason', hint: 'reason', rest: true },
  ip: { k: 'ip', t: 'text', maxLength: 45, label: 'IP address', hint: 'ip address', req: true, test: /^[0-9a-fA-F.:]{3,45}$/, bad: 'Type a valid IP address, for example 203.0.113.7.' },
  steam: { k: 'steamId', t: 'text', maxLength: 17, label: 'Steam ID', hint: 'steam id', req: true, test: /^\d{17}$/, bad: 'A Steam ID has exactly 17 digits.' },
  profile: { k: 'profileId', t: 'text', maxLength: 64, label: 'Profile ID', hint: 'profile id', req: true },
  faction: { k: 'factionId', t: 'text', maxLength: 64, label: 'Faction ID', hint: 'faction id', req: true },
};
const QUEST_MODES = [['coop-story', 'Co-op story'], ['faction-war', 'Faction war'], ['instanced-story', 'Instanced story'], ['free-roam', 'Free roam']];

function cmd(group, name, command, help, args, extra) {
  return { group, name, cmd: command, help, args: args || [], ...extra };
}

function lines(v, pad) {
  if (v === null || typeof v !== 'object') return [pad + String(v)];
  if (!Object.keys(v).length) return [`${pad}(none)`];
  if (Array.isArray(v)) {
    return v.flatMap((x, i) => (x && typeof x === 'object' ? [`${pad}#${i + 1}`, ...lines(x, `${pad}  `)] : lines(x, `${pad}- `)));
  }
  return Object.entries(v).flatMap(([k, x]) => {
    const name = k.replace(/_/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2').toLowerCase();
    return x && typeof x === 'object' ? [`${pad}${name}:`, ...lines(x, `${pad}  `)] : [`${pad}${name}: ${x}`];
  });
}

const showClients = (d) => {
  const list = (d && d.clients) || [];
  return list.length
    ? list.map((c) => `#${c.player_id}  ${c.address}:${c.port}${c.steam_id ? `  STEAM ${c.steam_id}` : ''}`).join('\n')
    : 'Nobody is connected.';
};
const showFactions = (d) => {
  if (d && d.enabled === false) return 'Factions are turned off on this server.';
  const list = (d && d.factions) || [];
  return list.length ? list.map((f) => `${f.name} [${f.tag}]  ${f.memberCount} members  id ${f.factionId}`).join('\n') : 'There are no factions yet.';
};

const COMMANDS = [
  cmd('Server', 'status', 'status', 'Show the server name, players, game mode and settings.'),
  cmd('Server', 'ping', 'ping', 'Check that the server answers.'),
  cmd('Server', 'players', 'clients', 'List the players who are connected.', [], { show: showClients }),
  cmd('Server', 'bans', 'bans', 'List everything that is banned.'),
  cmd('Server', 'stop', 'shutdown', 'Stop the server. Everyone is disconnected.', [], { danger: true }),
  cmd('Players', 'kick', 'kick', 'Remove a player. They can join again.', [ARG.player, ARG.reason]),
  cmd('Players', 'revive', 'revive', 'Bring a downed player back up.', [ARG.player]),
  cmd('Players', 'kill', 'kill', 'Kill a player.', [ARG.player], { danger: true }),
  cmd('Players', 'mute', 'voice_mute', 'Stop a player from using voice chat. Leave the seconds empty to mute until you unmute.', [ARG.player, { k: 'seconds', t: 'int', min: 1, label: 'Seconds', hint: 'seconds' }]),
  cmd('Players', 'unmute', 'voice_unmute', 'Let a muted player use voice chat again.', [ARG.player]),
  cmd('Bans', 'ban', 'ban', 'Ban the address of a player who is online.', [ARG.player, ARG.reason], { danger: true }),
  cmd('Bans', 'banip', 'ban', 'Ban an IP address.', [ARG.ip, ARG.reason], { danger: true }),
  cmd('Bans', 'unban', 'unban', 'Remove an IP ban.', [ARG.ip]),
  cmd('Bans', 'steamban', 'steam_ban', 'Ban the Steam account of a player who is online.', [ARG.player, ARG.reason], { danger: true }),
  cmd('Bans', 'steamunban', 'steam_unban', 'Remove a Steam account ban.', [ARG.steam]),
  cmd('Bans', 'profileunban', 'profile_unban', 'Remove a ban on a player profile.', [ARG.profile]),
  cmd('Identity and Steam', 'steamlookup', 'steam_lookup', 'Look up what the server knows about a Steam ID.', [ARG.steam]),
  cmd('Identity and Steam', 'steamflags', 'steam_flags', 'List Steam accounts that were flagged by the server.'),
  cmd('Identity and Steam', 'clearflag', 'steam_flag_clear', 'Clear a flag on a Steam account.', [ARG.profile, ARG.steam]),
  cmd('Identity and Steam', 'resetidentity', 'identity_reset', 'Forget the saved sign-in of a profile, so it must be set up again the next time it joins.', [ARG.profile, { k: 'ban', t: 'bool', label: 'Also ban it', hint: 'ban yes/no' }, ARG.reason], { danger: true }),
  cmd('World', 'time', 'world_time', 'Set the time of day for everyone. Four digits, for example 1430 for 2:30 pm.', [{ k: 'hhmm', t: 'text', maxLength: 4, label: 'Time (HHMM)', hint: 'hhmm', req: true, test: /^([01]\d|2[0-3])[0-5]\d$/, bad: 'Type the time as four digits, for example 0830 or 1430.' }]),
  cmd('World', 'weather', 'world_weather', 'Change the weather for everyone. Needs the 8-digit weather form ID.', [{ k: 'weather', t: 'text', maxLength: 8, label: 'Weather ID', hint: 'weather id', req: true, test: /^[0-9a-fA-F]{8}$/, bad: 'A weather ID has exactly 8 letters or digits (0-9, A-F).' }]),
  cmd('World', 'worldmode', 'world_mode', 'Show or change the game mode. Leave it empty to see the current one.', [{ k: 'mode', t: 'select', label: 'Mode', hint: 'mode', options: [['coop-story', 'Co-op story'], ['free-roam', 'Free roam']], blank: '(show current)' }]),
  cmd('World', 'worldpolicy', 'world_policy', 'Show the world rules the server sends to players.'),
  cmd('World', 'policyreload', 'world_policy_reload', 'Reload the world rules file without restarting.'),
  cmd('Factions', 'factions', 'factions', 'List the player factions.', [], { show: showFactions }),
  cmd('Factions', 'factiondisband', 'faction_disband', 'Delete a faction.', [ARG.faction], { danger: true }),
  cmd('Factions', 'factionpromote', 'faction_promote', 'Make a member the leader of a faction.', [ARG.faction, ARG.profile]),
  cmd('Factions', 'factionkick', 'faction_kick', 'Remove a member from a faction.', [ARG.faction, ARG.profile], { danger: true }),
  cmd('Quests', 'questmode', 'quest_mode', 'Show or change how quests work. Leave it empty to see the current mode.', [{ k: 'mode', t: 'select', label: 'Mode', hint: 'mode', options: QUEST_MODES, blank: '(show current)' }]),
  cmd('Quests', 'questgate', 'quest_gate', 'Hold everyone at a quest stage until you release it. Quest looks like 000F12:Fallout4.esm.', [
    { k: 'quest', t: 'text', maxLength: 80, label: 'Quest', hint: 'quest', req: true, test: /^[0-9a-fA-F]{6}:.+\.(esm|esp|esl)$/i, bad: 'A quest looks like 000F12:Fallout4.esm (6 digits, a colon, the plugin file).' },
    { k: 'stage', t: 'int', min: 0, max: 65535, label: 'Stage', hint: 'stage', req: true },
    { k: 'release', t: 'bool', label: 'Release the hold', hint: 'release yes/no' },
  ]),
  cmd('Quests', 'questsolo', 'quest_solo', 'Let a player do a quest on their own, away from the shared quest.', [
    ARG.player,
    { k: 'quest', t: 'text', maxLength: 80, label: 'Quest', hint: 'quest', req: true, test: /^[0-9a-fA-F]{6}:.+\.(esm|esp|esl)$/i, bad: 'A quest looks like 000F12:Fallout4.esm (6 digits, a colon, the plugin file).' },
    { k: 'solo', t: 'bool', def: true, label: 'Play it solo', hint: 'solo yes/no' },
  ]),
];
const BY_NAME = Object.fromEntries(COMMANDS.map((c) => [c.name, c]));
const YES = /^(1|y|yes|true|on)$/i;

function usage(def) {
  return `/${def.name} ${def.args.map((a) => (a.req ? `<${a.hint}>` : `[${a.hint}]`)).join(' ')}`.trim();
}

function buildArgs(def, values) {
  const args = {};
  for (let i = 0; i < def.args.length; i += 1) {
    const a = def.args[i];
    const raw = (values[i] || '').trim();
    if (a.t === 'bool') {
      const on = raw ? YES.test(raw) : !!a.def;
      if (on || a.def) args[a.k] = on;
    } else if (!raw) {
      if (a.req) return { error: `Missing the ${a.hint}. Usage: ${usage(def)}` };
    } else if (a.t === 'int') {
      const n = Number(raw);
      if (!/^\d+$/.test(raw) || n < (a.min || 0) || (a.max !== undefined && n > a.max)) {
        return { error: `${a.label}: type a whole number${a.max !== undefined ? ` from ${a.min} to ${a.max}` : ''}.` };
      }
      args[a.k] = n;
    } else if (a.t === 'select') {
      const hit = a.options.find(([v]) => v === raw.toLowerCase());
      if (!hit) return { error: `${a.label}: pick one of ${a.options.map(([v]) => v).join(', ')}.` };
      args[a.k] = hit[0];
    } else if (a.test && !a.test.test(raw)) {
      return { error: a.bad };
    } else {
      args[a.k] = raw;
    }
  }
  return { args };
}

function pushLog(label, ok, text, data) {
  const def = BY_NAME[label.split(' ')[0].slice(1)];
  let detail = '';
  if (typeof data === 'string') detail = data;
  else if (data !== null && data !== undefined) detail = def && def.show ? def.show(data) : lines(data, '').join('\n');
  app.log.unshift({ label, ok, text, detail });
  app.log.length = Math.min(app.log.length, 60);
  renderLog();
}

function runCommand(def, values) {
  if (app.busy) return false;
  const built = buildArgs(def, values);
  const label = ['/' + def.name, ...values.filter(Boolean)].join(' ');
  if (built.error) {
    pushLog(label, false, built.error);
    return false;
  }
  const go = () => adminCall(def.cmd, built.args, label, 'admin');
  if (def.danger) askConfirm(`RUN ${label}?`.slice(0, 60), go);
  else go();
  return true;
}

function runSlash() {
  const input = $('serverSlash');
  const line = input.value.trim();
  if (!line) return;
  const tokens = line.replace(/^\//, '').split(/\s+/);
  const name = tokens.shift().toLowerCase();
  if (!name) {
    showSuggest();
    return;
  }
  if (name === 'help' || name === '?') {
    input.value = '';
    pushLog('/help', true, 'Every command you can run:', COMMANDS.map((c) => `${usage(c)}\n    ${c.help}`).join('\n'));
    hideSuggest();
    return;
  }
  const def = BY_NAME[name];
  if (!def) {
    pushLog(`/${name}`, false, 'That command does not exist. Type / to see every command, or /help for the list.');
    return;
  }
  const values = def.args.map((a, i) => (a.rest ? tokens.slice(i).join(' ') : tokens[i] || ''));
  app.refocusSlash = true;
  if (!runCommand(def, values)) {
    app.refocusSlash = false;
    return;
  }
  input.value = '';
  hideSuggest();
}

function renderLog() {
  const box = $('serverLog');
  box.textContent = '';
  if (!app.log.length) box.appendChild(el('p', 'status-note', 'Nothing yet. Run a command and its answer appears here.'));
  app.log.forEach((entry) => {
    const item = el('div', `srv-log__entry${entry.ok ? '' : ' srv-log__entry--bad'}`);
    const head = el('div', 'srv-log__head');
    head.append(el('span', 'srv-log__cmd', entry.label), el('span', `chip ${entry.ok ? 'chip--ok' : 'chip--error'}`, entry.ok ? 'DONE' : 'FAILED'));
    item.append(head, el('p', 'srv-log__text', entry.text));
    if (entry.detail) item.appendChild(el('pre', 'srv-log__data', entry.detail));
    box.appendChild(item);
  });
  $('serverLogClear').disabled = !app.log.length;
}

const suggest = { items: [], at: 0 };

function hideSuggest() {
  suggest.items = [];
  $('serverSuggest').hidden = true;
}

function showSuggest() {
  const text = $('serverSlash').value;
  const box = $('serverSuggest');
  box.textContent = '';
  if (!text.startsWith('/')) {
    hideSuggest();
    return;
  }
  const [first, ...rest] = text.slice(1).split(/\s+/);
  const exact = BY_NAME[first.toLowerCase()];
  if (rest.length && exact) {
    suggest.items = [];
    const hint = el('li', 'srv-suggest__item srv-suggest__item--hint');
    hint.append(el('span', 'srv-suggest__usage', usage(exact)), el('span', 'srv-suggest__help', exact.help));
    box.appendChild(hint);
    box.hidden = false;
    return;
  }
  suggest.items = COMMANDS.filter((c) => c.name.startsWith(first.toLowerCase())).slice(0, 8);
  suggest.at = Math.min(suggest.at, Math.max(0, suggest.items.length - 1));
  suggest.items.forEach((c, i) => {
    const item = el('li', `srv-suggest__item${i === suggest.at ? ' is-active' : ''}`);
    item.setAttribute('role', 'option');
    item.append(el('span', 'srv-suggest__usage', usage(c)), el('span', 'srv-suggest__help', c.help));
    item.addEventListener('mousedown', (e) => {
      e.preventDefault();
      completeSlash(c);
    });
    box.appendChild(item);
  });
  box.hidden = !suggest.items.length;
}

function completeSlash(def) {
  const input = $('serverSlash');
  input.value = `/${def.name}${def.args.length ? ' ' : ''}`;
  input.focus();
  showSuggest();
}

function slashKey(e) {
  const input = $('serverSlash');
  const open = suggest.items.length > 0 && !$('serverSuggest').hidden;
  if (e.key === 'Escape' && !$('serverSuggest').hidden) {
    e.stopPropagation();
    hideSuggest();
  } else if (open && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
    e.preventDefault();
    suggest.at = (suggest.at + (e.key === 'ArrowDown' ? 1 : -1) + suggest.items.length) % suggest.items.length;
    showSuggest();
  } else if (open && e.key === 'Tab') {
    e.preventDefault();
    completeSlash(suggest.items[suggest.at]);
  } else if (e.key === 'Enter') {
    const typed = input.value.slice(1).trim().toLowerCase();
    if (open && !BY_NAME[typed] && typed !== 'help') completeSlash(suggest.items[suggest.at]);
    else runSlash();
  }
}

function commandForm(def) {
  const row = el('div', 'srv-cmd');
  const head = el('div', 'srv-cmd__head');
  head.append(el('span', 'srv-cmd__name', `/${def.name}`), el('span', 'srv-cmd__help', def.help));
  row.appendChild(head);
  const fields = el('div', 'server-row');
  const controls = def.args.map((a) => {
    const c = makeControl(a);
    c.node.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') runCommand(def, controls.map((x) => x.read()));
    });
    if (a.t === 'bool') {
      const label = el('label', 'srv-check');
      label.append(c.node, el('span', undefined, a.label));
      fields.appendChild(label);
    } else {
      fields.appendChild(c.node);
    }
    return c;
  });
  const run = el('button', 'btn', def.danger ? 'RUN...' : 'RUN');
  run.type = 'button';
  run.dataset.run = '1';
  run.addEventListener('click', () => runCommand(def, controls.map((c) => c.read())));
  fields.appendChild(run);
  row.appendChild(fields);
  return row;
}

function buildCommands() {
  const box = $('serverCommands');
  box.textContent = '';
  [...new Set(COMMANDS.map((c) => c.group))].forEach((group) => {
    const details = el('details', 'srv-group');
    details.appendChild(el('summary', 'srv-group__title', group.toUpperCase()));
    COMMANDS.filter((c) => c.group === group).forEach((c) => details.appendChild(commandForm(c)));
    box.appendChild(details);
  });
}

function renderServerAdmin(running) {
  $('serverSlash').disabled = app.busy || !running;
  $('serverSlashSend').disabled = app.busy || !running;
  $('serverCommands').querySelectorAll('button, input, select').forEach((n) => { n.disabled = app.busy || !running; });
  if (app.refocusSlash && !$('serverSlash').disabled) {
    app.refocusSlash = false;
    if (app.serverTab === 'admin') $('serverSlash').focus();
  }
}

function wireServerAdmin() {
  buildCommands();
  renderLog();
  $('serverSlash').addEventListener('input', () => {
    suggest.at = 0;
    showSuggest();
  });
  $('serverSlash').addEventListener('focus', showSuggest);
  $('serverSlash').addEventListener('blur', hideSuggest);
  $('serverSlash').addEventListener('keydown', slashKey);
  $('serverSlashSend').addEventListener('click', runSlash);
  $('serverLogClear').addEventListener('click', () => {
    app.log = [];
    renderLog();
  });
}
