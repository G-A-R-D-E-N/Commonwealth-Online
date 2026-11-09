alter table public.live_servers add column if not exists passworded boolean not null default false;

create or replace function public.register_live_server(p_source text, p_body jsonb, p_allow_private boolean default false)
returns jsonb language plpgsql security definer set search_path to '' as $$
declare
  source inet;
  compat jsonb;
  forbidden text;
  new_id text := 'live-' || encode(extensions.gen_random_bytes(16), 'hex');
  secret text := encode(extensions.gen_random_bytes(32), 'hex');
  v_port integer;
  v_max integer;
  v_players integer;
  v_passworded boolean;
  fingerprint text;
begin
  if pg_catalog.current_setting('transaction_isolation') <> 'read committed' then
    return public.directory_error(503, 'unsupported_isolation', 'Registration requires read committed isolation.');
  end if;

  begin
    source := host(p_source::inet)::inet;
  exception when others then
    return public.directory_error(422, 'unsupported_address', 'Public listings currently require an IPv4 source address.');
  end;
  if family(source) <> 4 then
    return public.directory_error(422, 'unsupported_address', 'Public listings currently require an IPv4 source address.');
  end if;
  if not p_allow_private and not public.is_public_ipv4(source) then
    return public.directory_error(422, 'non_public_address', 'Registration must come from the server''s public IPv4 address.');
  end if;
  if p_body is null or jsonb_typeof(p_body) <> 'object' then
    return public.directory_error(400, 'invalid_json', 'A JSON object is required.');
  end if;
  foreach forbidden in array array['host', 'address', 'ip', 'publicAddress', 'endpoint'] loop
    if p_body ? forbidden then
      return public.directory_error(422, 'address_not_allowed', 'The public address is taken from the request source; do not supply ' || forbidden || '.');
    end if;
  end loop;
  if p_body ? 'passworded' and jsonb_typeof(p_body -> 'passworded') not in ('boolean', 'null') then
    return public.directory_error(422, 'invalid_metadata', 'passworded must be a boolean.');
  end if;
  v_passworded := coalesce((p_body ->> 'passworded')::boolean, false);

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

    if not exists (select 1 from public.live_servers where host = source and port = v_port) then
      if (select count(*) from public.live_servers) >= 512 then
        return public.directory_error(503, 'directory_full', 'The public directory is full.');
      end if;
      if (select count(*) from public.live_servers where host = source) >= 4 then
        return public.directory_error(429, 'too_many_servers', 'Too many servers are listed from this address.');
      end if;
    end if;

    insert into public.live_servers (id, secret_hash, host, port, name, description, region, tags, players, max_players,
      client_version, fallout_runtime, content_manifest_fingerprint, capabilities, passworded, registered_at, last_seen)
    values (new_id, encode(extensions.digest(secret, 'sha256'), 'hex'), source, v_port,
      public.directory_text(p_body, 'name', 64, true), public.directory_text(p_body, 'description', 256),
      public.directory_text(p_body, 'region', 64), public.directory_list(p_body, 'tags', 8, 32),
      v_players, v_max,
      public.directory_text(compat, 'clientVersion', 64), public.directory_text(compat, 'falloutRuntime', 64),
      fingerprint, public.directory_list(compat, 'capabilities', 64, 64), v_passworded, now(), now())
    on conflict (host, port) do update set
      id = excluded.id, secret_hash = excluded.secret_hash, name = excluded.name, description = excluded.description,
      region = excluded.region, tags = excluded.tags, players = excluded.players, max_players = excluded.max_players,
      client_version = excluded.client_version, fallout_runtime = excluded.fallout_runtime,
      content_manifest_fingerprint = excluded.content_manifest_fingerprint, capabilities = excluded.capabilities,
      passworded = excluded.passworded, registered_at = now(), last_seen = now();
  exception when raise_exception then
    if sqlerrm like 'invalid_metadata:%' then
      return public.directory_error(422, 'invalid_metadata', substr(sqlerrm, 18));
    end if;
    raise;
  end;

  return jsonb_build_object('status', 201, 'serverId', new_id, 'secret', secret, 'host', host(source),
    'port', v_port, 'heartbeatIntervalSeconds', 30, 'ttlSeconds', extract(epoch from public.live_server_ttl())::integer);
end;
$$;

drop function if exists public.list_live_servers();

create function public.list_live_servers()
returns table (id text, name text, description text, region text, tags text[], host text, port integer,
  players integer, max_players integer, client_version text, fallout_runtime text,
  content_manifest_fingerprint text, capabilities text[], passworded boolean, last_seen timestamptz)
language sql stable security definer set search_path to '' as $$
  select s.id, s.name, s.description, s.region, s.tags, host(s.host), s.port, s.players, s.max_players,
    s.client_version, s.fallout_runtime, s.content_manifest_fingerprint, s.capabilities, s.passworded, s.last_seen
  from public.live_servers s
  where s.last_seen >= now() - public.live_server_ttl()
  order by s.players desc, s.name
  limit 512
$$;

revoke all on function public.register_live_server(text, jsonb, boolean) from public, anon, authenticated;
grant execute on function public.register_live_server(text, jsonb, boolean) to service_role;
revoke all on function public.list_live_servers() from public, anon, authenticated;
grant execute on function public.list_live_servers() to service_role;
