create or replace function public.live_server_credentials_match(
  p_id text, p_secret_hash text, p_presented_id text, p_presented_secret text)
returns boolean language sql immutable set search_path to '' as $$
  select p_presented_id is not null
     and char_length(p_presented_id) between 1 and 64
     and p_presented_id = p_id
     and p_presented_secret is not null
     and char_length(p_presented_secret) between 1 and 256
     and p_secret_hash = encode(extensions.digest(p_presented_secret, 'sha256'), 'hex')
$$;

create or replace function public.register_live_server_owned(
  p_source text,
  p_body jsonb,
  p_allow_private boolean default false,
  p_previous_id text default null,
  p_previous_secret text default null)
returns jsonb language plpgsql security definer set search_path to '' as $$
declare
  source inet;
  source_group inet;
  compat jsonb;
  forbidden text;
  new_id text := 'live-' || encode(extensions.gen_random_bytes(16), 'hex');
  secret text := encode(extensions.gen_random_bytes(32), 'hex');
  v_port integer;
  v_max integer;
  v_players integer;
  v_passworded boolean;
  fingerprint text;
  existing public.live_servers;
begin
  if pg_catalog.current_setting('transaction_isolation') <> 'read committed' then
    return public.directory_error(503, 'unsupported_isolation', 'Registration requires read committed isolation.');
  end if;

  begin
    source := host(p_source::inet)::inet;
  exception when others then
    return public.directory_error(422, 'unsupported_address', 'The request source is not an IP address.');
  end;
  if p_body is null or jsonb_typeof(p_body) <> 'object' then
    return public.directory_error(400, 'invalid_json', 'A JSON object is required.');
  end if;
  if p_body ? 'passworded' and jsonb_typeof(p_body -> 'passworded') not in ('boolean', 'null') then
    return public.directory_error(422, 'invalid_metadata', 'passworded must be a boolean.');
  end if;
  if p_body ? 'listensOnIpv6' and jsonb_typeof(p_body -> 'listensOnIpv6') not in ('boolean', 'null') then
    return public.directory_error(422, 'invalid_metadata', 'listensOnIpv6 must be a boolean.');
  end if;
  if family(source) = 6 then
    if not p_allow_private and not public.is_public_ipv6(source) then
      return public.directory_error(422, 'non_public_address', 'Registration must come from the server''s public IP address.');
    end if;
    if (p_body -> 'listensOnIpv6') is distinct from 'true'::jsonb then
      return public.directory_error(422, 'ipv6_not_listening',
        'The registration arrived over IPv6, but the server does not listen on IPv6. Bind the server to :: or register over IPv4.');
    end if;
  elsif not p_allow_private and not public.is_public_ipv4(source) then
    return public.directory_error(422, 'non_public_address', 'Registration must come from the server''s public IPv4 address.');
  end if;
  foreach forbidden in array array['host', 'address', 'ip', 'publicAddress', 'endpoint'] loop
    if p_body ? forbidden then
      return public.directory_error(422, 'address_not_allowed', 'The public address is taken from the request source; do not supply ' || forbidden || '.');
    end if;
  end loop;
  v_passworded := coalesce((p_body ->> 'passworded')::boolean, false);
  source_group := public.live_server_address_group(source);

  lock table public.live_servers in share row exclusive mode;

  delete from public.live_servers where last_seen < now() - public.live_server_ttl();

  begin
    compat := coalesce(p_body -> 'compatibility', '{}'::jsonb);
    if jsonb_typeof(compat) <> 'object' then raise exception 'invalid_metadata:compatibility must be an object.'; end if;
    v_port := public.directory_integer(p_body, 'port', 1, 65535);
    v_max := public.directory_integer(p_body, 'maxPlayers', 1, 256);
    v_players := public.directory_integer(p_body, 'players', 0, 256, 0);
    if v_players > v_max then raise exception 'invalid_metadata:players cannot exceed maxPlayers.'; end if;
    fingerprint := lower(public.directory_text(compat, 'contentManifestFingerprint', 64));
    if fingerprint !~ '^([0-9a-f]{64})?$' then
      raise exception 'invalid_metadata:contentManifestFingerprint must be a 64-character SHA-256 hex string.';
    end if;
  exception when raise_exception then
    if sqlerrm like 'invalid_metadata:%' then
      return public.directory_error(422, 'invalid_metadata', substr(sqlerrm, 18));
    end if;
    raise;
  end;

  select * into existing from public.live_servers where host = source and port = v_port;
  if found then
    if not public.live_server_credentials_match(existing.id, existing.secret_hash, p_previous_id, p_previous_secret) then
      return public.directory_error(409, 'listing_conflict',
        'A listing already exists for this address and port and its credentials were not presented; register with its server id and secret to replace it.');
    end if;
    update public.live_servers set
      secret_hash = encode(extensions.digest(secret, 'sha256'), 'hex'),
      name = public.directory_text(p_body, 'name', 64, true),
      description = public.directory_text(p_body, 'description', 256),
      region = public.directory_text(p_body, 'region', 64),
      tags = public.directory_list(p_body, 'tags', 8, 32),
      players = v_players, max_players = v_max,
      client_version = public.directory_text(compat, 'clientVersion', 64),
      fallout_runtime = public.directory_text(compat, 'falloutRuntime', 64),
      content_manifest_fingerprint = fingerprint,
      capabilities = public.directory_list(compat, 'capabilities', 64, 64),
      passworded = v_passworded,
      registered_at = now(), last_seen = now()
    where id = existing.id;
    return jsonb_build_object('status', 200, 'serverId', existing.id, 'secret', secret, 'host', host(source),
      'port', v_port, 'rotated', true, 'heartbeatIntervalSeconds', 30,
      'ttlSeconds', extract(epoch from public.live_server_ttl())::integer);
  end if;

  begin
    if (select count(*) from public.live_servers) >= 512 then
      return public.directory_error(503, 'directory_full', 'The public directory is full.');
    end if;
    if (select count(*) from public.live_servers where host <<= source_group) >= 4 then
      return public.directory_error(429, 'too_many_servers', 'Too many servers are listed from this address.');
    end if;

    insert into public.live_servers (id, secret_hash, host, port, name, description, region, tags, players, max_players,
      client_version, fallout_runtime, content_manifest_fingerprint, capabilities, passworded, registered_at, last_seen)
    values (new_id, encode(extensions.digest(secret, 'sha256'), 'hex'), source, v_port,
      public.directory_text(p_body, 'name', 64, true), public.directory_text(p_body, 'description', 256),
      public.directory_text(p_body, 'region', 64), public.directory_list(p_body, 'tags', 8, 32),
      v_players, v_max,
      public.directory_text(compat, 'clientVersion', 64), public.directory_text(compat, 'falloutRuntime', 64),
      fingerprint, public.directory_list(compat, 'capabilities', 64, 64), v_passworded, now(), now())
    on conflict (host, port) do nothing;
  exception when raise_exception then
    if sqlerrm like 'invalid_metadata:%' then
      return public.directory_error(422, 'invalid_metadata', substr(sqlerrm, 18));
    end if;
    raise;
  end;

  if not found then
    select * into existing from public.live_servers where host = source and port = v_port;
    if not found then
      return public.directory_error(503, 'registration_race', 'The listing changed during registration; retry.');
    end if;
    if not public.live_server_credentials_match(existing.id, existing.secret_hash, p_previous_id, p_previous_secret) then
      return public.directory_error(409, 'listing_conflict',
        'A listing already exists for this address and port and its credentials were not presented; register with its server id and secret to replace it.');
    end if;
    update public.live_servers set
      secret_hash = encode(extensions.digest(secret, 'sha256'), 'hex'),
      name = public.directory_text(p_body, 'name', 64, true),
      description = public.directory_text(p_body, 'description', 256),
      region = public.directory_text(p_body, 'region', 64),
      tags = public.directory_list(p_body, 'tags', 8, 32),
      players = v_players, max_players = v_max,
      client_version = public.directory_text(compat, 'clientVersion', 64),
      fallout_runtime = public.directory_text(compat, 'falloutRuntime', 64),
      content_manifest_fingerprint = fingerprint,
      capabilities = public.directory_list(compat, 'capabilities', 64, 64),
      passworded = v_passworded,
      registered_at = now(), last_seen = now()
    where id = existing.id;
    return jsonb_build_object('status', 200, 'serverId', existing.id, 'secret', secret, 'host', host(source),
      'port', v_port, 'rotated', true, 'heartbeatIntervalSeconds', 30,
      'ttlSeconds', extract(epoch from public.live_server_ttl())::integer);
  end if;

  return jsonb_build_object('status', 201, 'serverId', new_id, 'secret', secret, 'host', host(source),
    'port', v_port, 'heartbeatIntervalSeconds', 30, 'ttlSeconds', extract(epoch from public.live_server_ttl())::integer);
end;
$$;

revoke all on function public.live_server_credentials_match(text, text, text, text) from public, anon, authenticated;
revoke all on function public.register_live_server_owned(text, jsonb, boolean, text, text) from public, anon, authenticated;
grant execute on function public.register_live_server_owned(text, jsonb, boolean, text, text) to service_role;
