'use strict';

const CULPRIT = {
  commonwealth_online: ['COMMONWEALTH ONLINE', 'chip--error'],
  prismaui: ['PRISMAUI', 'chip--warn'],
  f4se: ['F4SE', 'chip--warn'],
  game: ['GAME OR OTHER MODS', 'chip--warn'],
  mod: ['ANOTHER MOD', 'chip--warn'],
  unknown: ['UNKNOWN', 'chip--warn'],
};

function openLogs() {
  app.crashes = null;
  app.shared = null;
  showView('logs');
  send({ cmd: 'crashes' });
}

const logRow = (label, value) => [el('dt', undefined, label), el('dd', undefined, value)];

function crashCard(c) {
  const [label, tone] = CULPRIT[c.culprit] || CULPRIT.unknown;
  const card = el('section', 'card mods-card');
  const head = el('div', 'mods-card__head');
  head.append(el('span', 'mods-card__name', c.time || c.file), el('span', `chip ${tone}`, label));
  const grid = el('dl', 'card__grid');
  grid.append(...logRow('WHAT', c.exception || '-'), ...logRow('WHERE', c.fault || '-'), ...logRow('LOG', c.file));
  card.append(head, el('p', 'status-note', c.verdict), grid);
  return card;
}

function renderLogs() {
  const list = app.crashes;
  const done = app.shared;
  $('logsEmpty').hidden = !list || list.length > 0;
  $('logsWaiting').hidden = !!list || app.busy;
  $('logsFailure').hidden = !app.failure;
  $('logsFailure').textContent = app.failure;
  $('logsDone').hidden = !done;
  $('logsDoneText').textContent = done ? `Saved ${done.files} log files to ${done.path}` : '';
  $('logsRescan').disabled = app.busy;
  $('logsShare').disabled = app.busy;
  const box = $('logsList');
  box.textContent = '';
  if (list) list.forEach((c) => box.appendChild(crashCard(c)));
}

function wireLogs() {
  $('logsRescan').addEventListener('click', () => {
    if (!app.busy) openLogs();
  });
  $('logsShare').addEventListener('click', () => {
    if (app.busy) return;
    app.shared = null;
    send({ cmd: 'share_logs' });
  });
  $('logsReveal').addEventListener('click', () => send({ cmd: 'reveal_logs' }));
  $('logsBack').addEventListener('click', () => showView('status'));
}

function logsMock(play, busy, none) {
  const crashes = [
    {
      file: 'CommonwealthOnline.log', time: '2026-10-29 02:23:22.166', exception: 'access violation (0xC0000005)',
      fault: 'CommonwealthOnlineUI.dll+0x1D09A0', culprit: 'commonwealth_online',
      verdict: 'Commonwealth Online crashed in CommonwealthOnlineUI.dll. Press SHARE LOGS and send the zip to the team.',
    },
    {
      file: 'crash-2026-09-23-22-41-20.log', time: '2026-09-23 22:41:20', exception: 'EXCEPTION_ACCESS_VIOLATION',
      fault: 'Fallout4.exe+0141358', culprit: 'game',
      verdict: "The game itself crashed. That is usually another mod's data or a mod conflict.",
    },
    {
      file: 'crash-2026-09-15-15-07-45.log', time: '2026-09-15 15:07:46', exception: 'EXCEPTION_ACCESS_VIOLATION',
      fault: 'SomeMod.dll+0x20', culprit: 'mod',
      verdict: 'SomeMod.dll crashed. It is not part of Commonwealth Online; try disabling it.',
    },
  ];
  return {
    crashes: () => play([[0, busy(true, 'crashes')], [300, { type: 'crashes', list: none ? [] : crashes }], [0, busy(false, 'crashes')]]),
    share_logs: () => play(none
      ? [[0, busy(true, 'share')], [300, { type: 'error', message: 'no Fallout 4 log files were found; start the game once first' }], [0, busy(false, 'share')]]
      : [[0, busy(true, 'share')], [500, { type: 'logs_shared', path: 'C:\\Users\\player\\Desktop\\CommonwealthOnline-logs-20261029-224630.zip', files: 21 }], [0, busy(false, 'share')]]),
    reveal_logs: () => {},
  };
}
