'use strict';

function serverMock(play, busy, name) {
  const world = {
    installed: name !== 'noserver',
    running: name === 'running',
    players: [
      { id: 3, address: '203.0.113.7:51012', steamId: '76561198000000001' },
      { id: 4, address: '198.51.100.9:40211', steamId: null },
    ],
    bans: [{ kind: 'ip', id: '192.0.2.44', reason: 'griefing' }],
    settings: {
      name: 'Commonwealth Online Server',
      description: '',
      maxPlayers: 16,
      password: '',
      port: 7777,
      publicListing: true,
      worldMode: 'coop-story',
      vats: 'disabled',
      doorsShared: true,
      friendlyFire: true,
      factionFriendlyFire: false,
      chat: true,
      factions: true,
      persistence: true,
      steamFriendsOnly: true,
      downedSeconds: 60,
      reviveSeconds: 3,
    },
  };
  const listingView = () => {
    if (!world.running) return null;
    if (!world.settings.publicListing) {
      return { state: 'disabled', publicAddress: '', error: '', text: 'Not listed' };
    }
    const address = `203.0.113.7:${world.settings.port}`;
    return { state: 'registered', publicAddress: address, error: '', text: `Registered as ${address}` };
  };
  const overview = () => ({
    type: 'server',
    folder: 'D:\\CO Modlist\\mods\\Commonwealth Online Server\\server',
    installed: world.installed,
    bundled: true,
    note: '',
    problem: null,
    running: world.running,
    status: world.running
      ? { name: world.settings.name, players: world.players.length, maxPlayers: world.settings.maxPlayers, worldMode: world.settings.worldMode, port: world.settings.port }
      : null,
    listing: listingView(),
    settings: world.installed ? { ...world.settings } : null,
    players: world.running ? world.players : [],
    bans: world.running ? world.bans : [],
  });
  const reply = (ok, text, data) => ({ type: 'server_reply', ok, text, data: data === undefined ? null : data });
  const listingChecks = () => {
    if (!world.settings.publicListing) {
      return [{ level: 'info', text: 'This server is not in the public server list, so players can only join it when they know its address.' }];
    }
    return [
      { level: 'warn', text: "This server is in the public server list, so strangers can find it. Turn 'List this server publicly' off in SETTINGS if you do not want that." },
      { level: 'warn', text: `Players from the internet reach the listed server only if UDP port ${world.settings.port} is forwarded to this PC on your router. The launcher never changes your router or firewall.` },
    ];
  };
  const checks = (mode) => ({
    type: 'server_network',
    checks: [
      { level: 'ok', text: 'The server files are installed.' },
      { level: world.running ? 'ok' : 'info', text: world.running ? 'The server is running.' : 'The server is switched off right now.' },
      { level: 'ok', text: 'Nothing else is using the game port (7777), so the server can start.' },
      { level: 'ok', text: 'The admin port (7779) only answers on this PC. It is not open to your network or the internet.' },
      ...(mode === 'p2p'
        ? [
          { level: 'ok', text: 'Steam is running and you are signed in.' },
          { level: 'info', text: 'Steam P2P needs no router changes. The launcher opens no ports.' },
          { level: 'info', text: 'The people you host for can see your internet address (IP). Only host for people you trust.' },
        ]
        : [
          { level: 'ok', text: 'Players on your home network can join 192.168.1.20 port 7777.' },
          { level: 'warn', text: 'For players outside your home you would have to forward port 7777 on your router. The launcher never does this for you, because it opens the port to the whole internet.' },
          { level: 'info', text: 'To check, look at the port forwarding page of your router and make sure 7777 is not forwarded unless you meant to.' },
          { level: 'info', text: 'Windows Firewall: only allow the server on Private networks, never on Public ones.' },
          { level: 'warn', text: 'No password is set. Anyone who reaches the server could join. Set one in SETTINGS.' },
          ...listingChecks(),
        ]),
    ],
  });
  const again = (what, events) => play([[0, busy(true, what)], ...events.map((e) => [400, e]), [0, busy(false, what)]]);
  const statusData = () => ({
    is_running: world.running,
    server_name: world.settings.name,
    max_players: world.settings.maxPlayers,
    connected_clients: world.players.length,
    port: world.settings.port,
    admin_port: 7779,
    world_mode: world.settings.worldMode,
  });
  const answer = (command, args) => {
    switch (command) {
      case 'status': return reply(true, 'Server status.', statusData());
      case 'ping': return reply(true, 'pong');
      case 'clients': return reply(true, `${world.players.length} players connected.`, {
        total_clients: world.players.length,
        clients: world.players.map((p) => ({ player_id: p.id, address: p.address.split(':')[0], port: Number(p.address.split(':')[1]), steam_id: p.steamId })),
      });
      case 'bans': return reply(true, `${world.bans.length} bans.`, { bans: world.bans });
      case 'factions': return reply(true, '1 faction.', { enabled: true, factions: [{ factionId: 'f-01', name: 'Minutemen', tag: 'MM', memberCount: 2 }] });
      case 'shutdown':
        world.running = false;
        return reply(true, 'The server is stopping.');
      case 'kick':
      case 'kill':
        world.players = world.players.filter((p) => p.id !== args.playerId);
        return reply(true, command === 'kick' ? `Player ${args.playerId} was kicked.` : `Player ${args.playerId} was killed.`);
      case 'ban':
      case 'steam_ban':
        world.bans.push({ kind: command === 'ban' ? 'ip' : 'steam', id: args.ip || args.steamId || '203.0.113.7', reason: args.reason || '' });
        world.players = world.players.filter((p) => p.id !== args.playerId);
        return reply(true, 'Banned.');
      case 'unban':
      case 'steam_unban':
        world.bans = world.bans.filter((b) => b.id !== (args.ip || args.steamId));
        return reply(true, 'The ban was removed.');
      default: return reply(true, 'Done.');
    }
  };
  const settingsError = (s) => {
    if (!s.name.trim()) return 'The server name must be 1 to 64 characters long.';
    if (s.port === 7779 || s.port === 7778) return `Port ${s.port} is already used by the server itself. Pick a different game port.`;
    return '';
  };
  return {
    server: () => again('server', [overview()]),
    server_network: (cmd) => again('network', [checks(cmd.mode)]),
    server_start: () => {
      world.running = true;
      again('start', [reply(true, 'The server started.'), overview()]);
    },
    server_admin: (cmd) => again('admin', [answer(cmd.command, cmd.args), overview()]),
    server_save: (cmd) => {
      const error = settingsError(cmd.settings);
      if (!error) world.settings = { ...cmd.settings };
      again('save', [reply(!error, error || 'Saved. The changes apply the next time the server starts.'), overview()]);
    },
    server_restart: () => again('restart', [reply(true, 'The server restarted with your saved settings.'), overview()]),
  };
}
