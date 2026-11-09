'use strict';

function appdataRow(entry) {
  const card = el('section', 'card mods-card');
  const when = entry.modifiedUnix ? new Date(entry.modifiedUnix * 1000).toLocaleString() : 'unknown';
  const grid = el('dl', 'card__grid');
  grid.append(...logRow('WHAT', entry.description), ...logRow('PATH', entry.path), ...logRow('SIZE', `${entry.sizeBytes} bytes`), ...logRow('MODIFIED', when));
  const show = el('button', 'btn', 'SHOW IN FOLDER');
  show.type = 'button';
  show.addEventListener('click', () => send({ cmd: 'appdata_show', path: entry.path }));
  card.append(grid, show);
  return card;
}

function renderAppdata() {
  const box = $('appdataList');
  box.textContent = '';
  const entries = app.appdata;
  if (!entries) return;
  if (!entries.length) {
    box.appendChild(el('p', 'status-note', 'No launcher or Commonwealth Online files were found in your AppData or Documents folders.'));
    return;
  }
  entries.forEach((entry) => box.appendChild(appdataRow(entry)));
}

function wireAppdata() {
  $('appdataScan').addEventListener('click', () => send({ cmd: 'appdata_scan' }));
}

function appdataMock(play) {
  const entries = [
    { kind: 'setup', path: 'C:\\Users\\you\\AppData\\Local\\CommonwealthOnlineLauncher\\launcher-setup.json', sizeBytes: 812, modifiedUnix: 1761700000, description: 'Launcher setup choices and progress.' },
    { kind: 'game', path: 'C:\\Users\\you\\Documents\\My Games\\Fallout4\\CommonwealthOnline\\host-server-folder.json', sizeBytes: 96, modifiedUnix: 1761650000, description: 'Server folder you saved for hosting.' },
  ];
  return { appdata_scan: () => play([[0, { type: 'appdata', entries }]]) };
}
