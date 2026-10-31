do $$
declare
  r jsonb;
  hb jsonb;
  v_id text;
  v_secret text;
  n integer;
  body jsonb := '{"name":"SQL Test","description":"d","port":7777,"players":1,"maxPlayers":8,"passworded":false,
    "compatibility":{"clientVersion":"0.0.0.2","falloutRuntime":"1.10.163","capabilities":["protocol-v2","protocol-v2"]}}';
begin
  r := public.register_live_server('8.8.4.4', body);
  assert r ->> 'status' = '201', 'registration: ' || r::text;
  assert r ->> 'host' = '8.8.4.4', 'observed source is the listed host';
  v_id := r ->> 'serverId';
  v_secret := r ->> 'secret';
  assert (select secret_hash from public.live_servers where live_servers.id = v_id) = encode(extensions.digest(v_secret, 'sha256'), 'hex'), 'secret stored hashed';
  assert not exists (select 1 from public.live_servers where secret_hash = v_secret), 'plaintext secret not stored';
  assert (select capabilities from public.live_servers where live_servers.id = v_id) = array['protocol-v2'], 'capabilities deduplicated';

  r := public.register_live_server('8.8.4.4', body || '{"host":"1.2.3.4"}');
  assert r #>> '{error,code}' = 'address_not_allowed', 'spoofed host rejected: ' || r::text;
  r := public.register_live_server('8.8.4.4', body || '{"ip":"1.2.3.4"}');
  assert r #>> '{error,code}' = 'address_not_allowed', 'spoofed ip rejected';
  r := public.register_live_server('192.168.0.5', body);
  assert r #>> '{error,code}' = 'non_public_address', 'private source rejected';
  r := public.register_live_server('100.64.1.1', body);
  assert r #>> '{error,code}' = 'non_public_address', 'CGNAT source rejected';
  r := public.register_live_server('::1', body || '{"listensOnIpv6":true}');
  assert r #>> '{error,code}' = 'non_public_address', 'loopback ipv6 rejected';
  r := public.register_live_server('2001:db8::1', body || '{"listensOnIpv6":true}');
  assert r #>> '{error,code}' = 'non_public_address', 'documentation ipv6 rejected';
  r := public.register_live_server('2001:0:4136:e378:8000:63bf:3fff:fdd2', body || '{"listensOnIpv6":true}');
  assert r #>> '{error,code}' = 'non_public_address', 'teredo rejected';
  r := public.register_live_server('not-an-address', body);
  assert r #>> '{error,code}' = 'unsupported_address', 'garbage source rejected';
  r := public.register_live_server('2606:4700:10::1', body);
  assert r #>> '{error,code}' = 'ipv6_not_listening', 'ipv6 source refused unless the server listens on ipv6: ' || r::text;
  r := public.register_live_server('2606:4700:10::1', body || '{"listensOnIpv6":"yes"}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'non-boolean listensOnIpv6 rejected';
  r := public.register_live_server('2606:4700:10::1', body || '{"listensOnIpv6":true}');
  assert r ->> 'status' = '201', 'public ipv6 listing accepted: ' || r::text;
  assert r ->> 'host' = '2606:4700:10::1', 'ipv6 host reported as text';
  assert (select host from public.list_live_servers() l where l.id = r ->> 'serverId') = '2606:4700:10::1', 'ipv6 host listed';
  for n in 2..4 loop
    r := public.register_live_server('2606:4700:10::' || n, body || '{"listensOnIpv6":true}');
    assert r ->> 'status' = '201', 'ipv6 listing ' || n;
  end loop;
  r := public.register_live_server('2606:4700:10::ffff:5', body || '{"listensOnIpv6":true}');
  assert r #>> '{error,code}' = 'too_many_servers', 'the per-address cap counts a whole ipv6 /64: ' || r::text;
  r := public.register_live_server('2606:4700:11::1', body || '{"listensOnIpv6":true}');
  assert r ->> 'status' = '201', 'another /64 is a different address group';
  r := public.register_live_server('2606:4700:10::1', body || '{"listensOnIpv6":true,"port":7777,"players":3}');
  assert r ->> 'status' = '201', 're-registering an existing ipv6 endpoint is not capped';
  r := public.register_live_server('127.0.0.1', body || '{"port":7790}', true);
  assert r ->> 'status' = '201', 'private allowed only in explicit local/test mode';

  r := public.register_live_server('8.8.4.4', body || '{"port":70000}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'bad port';
  r := public.register_live_server('8.8.4.4', body || '{"port":"7777"}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'string port';
  r := public.register_live_server('8.8.4.4', body || '{"players":9}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'players above max';
  r := public.register_live_server('8.8.4.4', body || '{"maxPlayers":999}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'max players bound';
  r := public.register_live_server('8.8.4.4', body || jsonb_build_object('name', repeat('x', 65)));
  assert r #>> '{error,code}' = 'invalid_metadata', 'long name';
  r := public.register_live_server('8.8.4.4', body || jsonb_build_object('name', E'bad\nname'));
  assert r #>> '{error,code}' = 'invalid_metadata', 'control chars';
  r := public.register_live_server('8.8.4.4', body || '{"compatibility":{"contentManifestFingerprint":"xyz"}}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'bad fingerprint';
  r := public.register_live_server('8.8.4.4', body || '{"compatibility":{"capabilities":[1]}}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'non-string capability';
  r := public.register_live_server('1.1.1.1', body || '{"passworded":true}');
  assert r ->> 'status' = '201', 'passworded listing accepted: ' || r::text;
  assert (select passworded from public.list_live_servers() l where l.id = r ->> 'serverId'), 'public list marks the listing passworded';
  assert not (select passworded from public.list_live_servers() l where l.id = v_id), 'an open listing stays unpassworded';
  r := public.register_live_server('1.1.1.1', body || '{"passworded":"yes"}');
  assert r #>> '{error,code}' = 'invalid_metadata', 'non-boolean passworded rejected';
  r := public.register_live_server('1.1.1.1', body || '{"passworded":false}');
  assert not (select passworded from public.list_live_servers() l where l.id = r ->> 'serverId'), 're-registering without a password clears the flag';
  assert not exists (select 1 from information_schema.columns where table_name = 'live_servers' and column_name ilike '%password%' and column_name <> 'passworded'),
    'the directory stores only the passworded flag, never a password';

  hb := public.heartbeat_live_server(v_id, 'wrong', '8.8.4.4', '{"players":2}');
  assert hb #>> '{error,code}' = 'unknown_server', 'wrong secret rejected';
  hb := public.heartbeat_live_server(v_id, v_secret, '9.9.9.9', '{"players":2}');
  assert hb #>> '{error,code}' = 'unknown_server', 'heartbeat from other source rejected';
  hb := public.heartbeat_live_server(v_id, v_secret, '8.8.4.4', '{"players":20}');
  assert hb #>> '{error,code}' = 'invalid_metadata', 'heartbeat bounds';
  hb := public.heartbeat_live_server(v_id, v_secret, '8.8.4.4', '{"players":5,"maxPlayers":8}');
  assert hb ->> 'status' = '200', 'heartbeat: ' || hb::text;
  assert (select players from public.list_live_servers() l where l.id = v_id) = 5, 'player count updated in public list';

  update public.live_servers set last_seen = now() - interval '91 seconds' where live_servers.id = v_id;
  assert not exists (select 1 from public.list_live_servers() l where l.id = v_id), 'expired listing hidden';
  hb := public.heartbeat_live_server(v_id, v_secret, '8.8.4.4', '{"players":5}');
  assert hb #>> '{error,code}' = 'unknown_server', 'expired listing must re-register';
  n := public.prune_live_servers();
  assert n >= 1 and not exists (select 1 from public.live_servers where live_servers.id = v_id), 'prune removes expired';

  r := public.register_live_server('8.8.4.4', body);
  hb := public.unregister_live_server(r ->> 'serverId', 'wrong', '8.8.4.4');
  assert hb #>> '{error,code}' = 'unknown_server', 'unregister needs secret';
  hb := public.unregister_live_server(r ->> 'serverId', r ->> 'secret', '8.8.4.4');
  assert hb ->> 'status' = '200' and not exists (select 1 from public.live_servers where live_servers.id = r ->> 'serverId'), 'unregister removes';

  for n in 1..4 loop
    r := public.register_live_server('8.8.4.4', body || jsonb_build_object('port', 8000 + n));
  end loop;
  r := public.register_live_server('8.8.4.4', body || '{"port":9000}');
  assert r #>> '{error,code}' = 'too_many_servers', 'per-ip cap';

  assert not has_table_privilege('anon', 'public.live_servers', 'select'), 'anon cannot read table';
  assert not has_function_privilege('anon', 'public.register_live_server(text, jsonb, boolean)', 'execute'), 'anon cannot register directly';
  assert not has_function_privilege('anon', 'public.heartbeat_live_server(text, text, text, jsonb)', 'execute'), 'anon cannot heartbeat directly';
  assert not has_function_privilege('anon', 'public.list_live_servers()', 'execute'), 'public list only through the rate-limited edge function';
  assert not exists (select 1 from information_schema.routines r2 join information_schema.parameters p using (specific_name)
    where r2.routine_name = 'list_live_servers' and p.parameter_name in ('secret_hash', 'secret')), 'list exposes no secret';

  raise exception 'LIVE_SERVERS_SQL_TESTS_PASSED';
end;
$$;
