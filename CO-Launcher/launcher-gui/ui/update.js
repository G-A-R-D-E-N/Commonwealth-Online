'use strict';

const WABBAJACK_TEXT = 'A new Commonwealth Online version is available. Update the modlist in Wabbajack.';
const CHANNEL_NAME = {
  wabbajack: 'Installed with Wabbajack',
  standalone: 'Installed by the launcher',
  unknown: 'Not known to the launcher',
};
const ADOPT_LINES = [
  'The launcher installs this Commonwealth Online release into the Mod Organizer 2 instance and manages its updates from now on.',
  'The other Commonwealth Online builds for this game version are switched off in the active profile. Their folders are kept.',
];
const ROLLBACK_LINES = [
  'The previous launcher-installed build is enabled and the current one is switched off in the active profile.',
];

function modPending(o) {
  const m = app.upd.mod || {};
  return !!o.modVersion && (!!m.installedVersion || modAction() === 'adopt');
}

function clearOffer() {
  app.upd.offer = null;
  app.upd.autoPending = false;
  render();
}

function modAction() {
  const m = app.upd.mod || {};
  if (m.channel === 'standalone' && m.canInstallMod) return 'standalone';
  if (m.channel === 'unknown' && m.needsAdoptConfirm) return 'adopt';
  return null;
}

function modSummary() {
  const m = app.upd.mod || {};
  if (m.channel === 'wabbajack') return WABBAJACK_TEXT;
  if (m.channel === 'standalone' && m.canInstallMod) return 'The mod update is ready to install.';
  if (m.channel === 'unknown' && m.needsAdoptConfirm) {
    return 'A new Commonwealth Online version is available. This install was not made by the launcher.';
  }
  return 'A new Commonwealth Online version is available. Update the mod yourself.';
}

function updateSummary(o) {
  const parts = [];
  if (o.launcherVersion) parts.push('The launcher can update itself now.');
  if (modPending(o)) parts.push(modSummary());
  if (!parts.length) parts.push('A new Commonwealth Online release is out.');
  return parts.join(' ');
}

function openSettings() {
  showView('settings');
  send({ cmd: 'locations_get' });
}

function renderChannel() {
  const m = app.upd.mod;
  $('updChannel').hidden = !m;
  $('updReasons').hidden = !m;
  $('updRollback').hidden = !(m && m.channel === 'standalone' && m.installedVersion);
  $('updRollback').disabled = app.busy;
  $('updSettingsAdopt').hidden = modAction() !== 'adopt';
  $('updSettingsAdopt').disabled = app.busy || !adoptVersion();
  $('updSettingsAdopt').title = adoptVersion() ? '' : 'Check for updates first.';
  if (!m) return;
  const version = m.installedVersion ? `, version ${m.installedVersion}` : '';
  $('updChannel').textContent = `${CHANNEL_NAME[m.channel] || 'Unknown'}${version}.`;
  const list = $('updReasonList');
  list.textContent = '';
  (m.channelReasons || []).forEach((reason) => list.appendChild(el('li', undefined, reason)));
}

function renderUpdate() {
  const u = app.upd;
  const o = u.offer;
  const show = !!o && o.version !== u.skipped && (!!o.launcherVersion || modPending(o));
  const msg = u.message;
  const err = msg && !msg.ok ? msg.text : '';
  const note = msg && msg.ok ? msg.text : '';
  $('update').hidden = !show && !note;
  $('updateOffer').hidden = !show;
  if (show) {
    $('updateTitle').textContent = `Update ${o.version} available`;
    $('updateText').textContent = updateSummary(o);
    $('updateNow').hidden = !o.launcherVersion;
    $('updateNow').disabled = app.busy;
    $('updateSkip').disabled = app.busy;
    $('updateDetails').hidden = !o.notes;
    const details = u.details && !!o.notes;
    $('updateNotes').hidden = !details;
    $('updateNotes').textContent = details ? o.notes : '';
    const action = modPending(o) ? modAction() : null;
    $('updateModActions').hidden = !action;
    $('updateMod').hidden = action !== 'standalone';
    $('updateMod').textContent = `Update mod to ${o.modVersion}`;
    $('updateMod').disabled = app.busy;
    $('updateAdopt').hidden = action !== 'adopt';
    $('updateAdopt').disabled = app.busy;
    const p = u.progress;
    $('updateTrack').hidden = !p;
    $('updateProgressText').hidden = !p;
    if (p) {
      const pct = p.total > 0 ? Math.min(100, Math.round((p.done / p.total) * 100)) : 0;
      $('updateBar').style.width = `${pct}%`;
      $('updateProgressText').textContent = p.total > 0 ? `Downloading ${pct}%` : 'Preparing the download...';
    }
  }
  $('updateError').hidden = !err;
  $('updateError').textContent = err;
  $('updateNote').hidden = !note;
  $('updateNote').textContent = note;
  $('updSettingsMsg').hidden = !msg;
  $('updSettingsMsg').textContent = msg ? msg.text : '';
  $('updSettingsMsg').className = msg && !msg.ok ? 'status-note guide__warn' : 'status-note';
  $('updCheck').disabled = app.busy;
  $('updPre').disabled = app.busy;
  $('updNotify').disabled = app.busy;
  $('updAuto').disabled = app.busy || $('updNotify').checked;
  renderChannel();
}

function updateEvent(ev) {
  const u = app.upd;
  switch (ev.type) {
    case 'update_prefs':
      u.prefs = ev;
      u.mod = ev;
      $('updPre').checked = ev.includePrereleases;
      $('updAuto').checked = ev.autoInstall;
      $('updNotify').checked = ev.notifyOnly;
      $('updVersion').textContent = `Launcher ${ev.launcherVersion}`;
      $('launcherVersion').textContent = `LAUNCHER ${ev.launcherVersion}`;
      break;
    case 'update_available':
      u.offer = ev;
      u.mod = ev;
      u.progress = null;
      u.message = null;
      u.details = false;
      u.autoPending = !!(
        ev.launcherVersion &&
        $('updAuto').checked &&
        !$('updNotify').checked &&
        ev.signingConfigured &&
        ev.version !== u.skipped
      );
      break;
    case 'update_progress':
      u.progress = ev;
      break;
    case 'update_result':
      u.progress = null;
      u.mod = ev;
      if (ev.ok && u.offer && ev.installedVersion === u.offer.modVersion) u.offer.modVersion = null;
      u.message = { ok: ev.ok, text: ev.message };
      break;
  }
}

// Ordering constraint: update_available arrives before busy goes off.
function updateIdle() {
  const u = app.upd;
  if (!u.autoPending || app.busy || !u.offer || u.progress) return;
  u.autoPending = false;
  u.progress = { done: 0, total: 0 };
  send({ cmd: 'update_install_launcher', version: u.offer.launcherVersion });
}

function adoptVersion() {
  return (app.upd.offer && app.upd.offer.modVersion) || (app.upd.mod && app.upd.mod.adoptVersion) || null;
}

function adoptMod() {
  const version = adoptVersion();
  if (app.busy || !version || modAction() !== 'adopt') return;
  askConfirm(
    'LET THE LAUNCHER MANAGE THIS MOD?',
    () => {
      app.upd.message = null;
      send({ cmd: 'update_install_mod', adopt_unknown: true, version });
    },
    ADOPT_LINES,
  );
}

function wireUpdate() {
  $('updateNow').addEventListener('click', () => {
    if (app.busy || !app.upd.offer || !app.upd.offer.launcherVersion) return;
    app.upd.message = null;
    app.upd.progress = { done: 0, total: 0 };
    send({ cmd: 'update_install_launcher', version: app.upd.offer.launcherVersion });
  });
  $('updateSkip').addEventListener('click', () => {
    const o = app.upd.offer;
    if (app.busy || !o) return;
    app.upd.skipped = o.version;
    app.upd.message = null;
    send({ cmd: 'update_skip', version: o.version });
    render();
  });
  $('updateDetails').addEventListener('click', () => {
    app.upd.details = !app.upd.details;
    render();
  });
  $('updateMod').addEventListener('click', () => {
    if (app.busy || !app.upd.offer || modAction() !== 'standalone') return;
    app.upd.message = null;
    send({ cmd: 'update_install_mod', adopt_unknown: false, version: app.upd.offer.modVersion });
  });
  $('updateAdopt').addEventListener('click', adoptMod);
  $('updSettingsAdopt').addEventListener('click', adoptMod);
  $('updRollback').addEventListener('click', () => {
    if (app.busy || !app.upd.mod) return;
    askConfirm(
      'ROLL BACK THE MOD?',
      () => {
        app.upd.message = null;
        send({ cmd: 'update_rollback_mod' });
      },
      ROLLBACK_LINES,
    );
  });
  $('updCheck').addEventListener('click', () => {
    if (app.busy) return;
    app.upd.message = null;
    clearOffer();
    send({ cmd: 'update_check' });
  });
  const saveSettings = () => {
    if (app.busy) return;
    if (!app.upd.prefs || $('updPre').checked !== app.upd.prefs.includePrereleases) clearOffer();
    send({
      cmd: 'update_settings',
      include_prereleases: $('updPre').checked,
      auto_install: $('updAuto').checked,
      notify_only: $('updNotify').checked,
    });
  };
  $('updPre').addEventListener('change', saveSettings);
  $('updAuto').addEventListener('change', saveSettings);
  $('updNotify').addEventListener('change', () => {
    saveSettings();
    render();
  });
  $('settingsBack').addEventListener('click', () => showView('status'));
  $('locationsSave').addEventListener('click', () => {
    if (app.busy) return;
    const locations = {
      game: $('locationGame').value.trim(), mo2: $('locationMo2').value.trim(), vortex: $('locationVortex').value.trim(),
    };
    const shown = app.upd.locations;
    const changed = !shown || Object.keys(locations).some((key) => locations[key] !== shown[key]);
    send({ cmd: 'locations_save', locations });
    if (changed) {
      clearOffer();
      send({ cmd: 'update_check' });
    }
  });
  $('locationVortexOpen').addEventListener('click', () => send({ cmd: 'locations_reveal_vortex' }));
}
