'use strict';

function openTroubleshoot() {
  app.fixes = {};
  showView('troubleshoot');
}

function copyCard(file) {
  const card = el('section', 'card mods-card');
  const when = file.modified ? new Date(file.modified * 1000).toLocaleString() : 'unknown';
  const grid = el('dl', 'card__grid');
  grid.append(...logRow('PATH', file.path), ...logRow('SIZE', `${file.size} bytes`), ...logRow('MODIFIED', when));
  card.append(grid);
  return card;
}

function renderTroubleshoot() {
  $('troubleshootFailure').hidden = !app.failure;
  $('troubleshootFailure').textContent = app.failure;
  document.querySelectorAll('#viewTroubleshoot [data-fix]').forEach((button) => {
    button.disabled = app.busy;
  });
  document.querySelectorAll('#viewTroubleshoot [data-result]').forEach((line) => {
    const done = app.fixes[line.dataset.result];
    line.hidden = !done;
    line.textContent = done ? done.message : '';
    line.classList.toggle('guide__warn', !!done && !done.ok);
  });
  const setup = app.fixes.check_setup;
  fillChecks($('troubleshootChecks'), setup && setup.checks);
  const box = $('troubleshootFiles');
  box.textContent = '';
  const found = app.fixes.check_server_files;
  if (found) found.list.forEach((file) => box.appendChild(copyCard(file)));
}

function wireTroubleshoot() {
  document.querySelectorAll('#viewTroubleshoot [data-fix]').forEach((button) => {
    button.addEventListener('click', () => {
      if (!app.busy) send({ cmd: button.dataset.fix });
    });
  });
  $('troubleshootBack').addEventListener('click', () => showView('status'));
}

function troubleshootMock(play, busy) {
  const run = (key, event) => () => play([[0, busy(true, key)], [400, event], [0, busy(false, key)]]);
  const fix = (what, ok, message, list = []) => ({ type: 'fix_result', what, ok, message, list });
  const copies = [
    { path: 'D:\\SteamLibrary\\steamapps\\common\\Fallout 4\\mods\\Commonwealth Online Server\\server\\CommonwealthOnline.Server.exe', size: 48123904, modified: 1761700000 },
    { path: 'D:\\MO2\\overwrite\\server\\CommonwealthOnline.Server.exe', size: 48123904, modified: 1761650000 },
  ];
  return {
    end_servers: run('end_servers', fix('end_servers', true, 'Ended 1 leftover server process(es); 0 still running.')),
    clear_links: run('clear_links', fix('clear_links', true, 'Removed 2 hosting link file(s).')),
    repair_server: run('repair_server', fix('repair_server', true, 'The server files were reinstalled.')),
    reset_folder: run('reset_folder', fix('reset_folder', true, 'Forgot the saved server folder.')),
    check_server_files: run('check_server_files', fix('check_server_files', true, `Found ${copies.length} server file(s).`, copies)),
    check_setup: run('check_setup', {
      type: 'setup_checks',
      checks: [
        { level: 'ok', text: 'Fallout 4 is closed.' },
        { level: 'ok', text: 'Steam is running.' },
        { level: 'warn', text: '2 leftover hosting link file(s) and no server running. Use CLEAR HOSTING LINK FILES.' },
        { level: 'fail', text: 'PrismaUI is turned off. Turn it on in Mod Organizer 2.' },
      ],
    }),
  };
}
