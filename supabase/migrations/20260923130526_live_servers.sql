create table if not exists public.live_servers (
  id text primary key check (id ~ '^live-[0-9a-f]{32}$'),
  secret_hash text not null check (secret_hash ~ '^[0-9a-f]{64}$'),
  host inet not null,
  port integer not null check (port between 1 and 65535),
  name text not null check (char_length(name) between 1 and 64 and name !~ '[[:cntrl:]]'),
  description text not null default '' check (char_length(description) <= 256 and description !~ '[[:cntrl:]]'),
  region text not null default '' check (char_length(region) <= 64 and region !~ '[[:cntrl:]]'),
  tags text[] not null default '{}' check (cardinality(tags) <= 8),
  players integer not null default 0 check (players >= 0),
  max_players integer not null check (max_players between 1 and 256),
  client_version text not null default '' check (char_length(client_version) <= 64 and client_version !~ '[[:cntrl:]]'),
  fallout_runtime text not null default '' check (char_length(fallout_runtime) <= 64 and fallout_runtime !~ '[[:cntrl:]]'),
  content_manifest_fingerprint text not null default '' check (content_manifest_fingerprint ~ '^([0-9a-f]{64})?$'),
  capabilities text[] not null default '{}' check (cardinality(capabilities) <= 64),
  registered_at timestamptz not null default now(),
  last_seen timestamptz not null default now(),
  constraint live_servers_players_bounded check (players <= max_players),
  constraint live_servers_endpoint_unique unique (host, port)
);

create index if not exists live_servers_last_seen_idx on public.live_servers (last_seen);
create index if not exists live_servers_host_idx on public.live_servers (host);

alter table public.live_servers enable row level security;
revoke all on public.live_servers from anon, authenticated;

create table if not exists public.server_directory_rate_limits (
  bucket text primary key,
  window_started_at timestamptz not null default now(),
  request_count integer not null default 0 check (request_count >= 0),
  updated_at timestamptz not null default now()
);
alter table public.server_directory_rate_limits enable row level security;
revoke all on public.server_directory_rate_limits from anon, authenticated;

create or replace function public.live_server_ttl() returns interval
language sql immutable set search_path to '' as $$ select interval '90 seconds' $$;

create or replace function public.consume_server_directory_rate_limit(p_bucket text, p_limit integer, p_window_seconds integer)
returns boolean language plpgsql security definer set search_path to '' as $$
declare
  allowed boolean;
begin
  if p_limit < 1 or p_window_seconds < 1 or char_length(p_bucket) < 1 then
    raise exception 'invalid rate limit arguments';
  end if;
  insert into public.server_directory_rate_limits (bucket, window_started_at, request_count, updated_at)
  values (p_bucket, now(), 1, now())
  on conflict (bucket) do update set
    window_started_at = case when now() >= public.server_directory_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then now() else public.server_directory_rate_limits.window_started_at end,
    request_count = case when now() >= public.server_directory_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then 1 else public.server_directory_rate_limits.request_count + 1 end,
    updated_at = now()
  returning request_count <= p_limit into allowed;
  return allowed;
end;
$$;

create or replace function public.is_public_ipv4(p_address inet) returns boolean
language sql immutable set search_path to '' as $$
  select family(p_address) = 4 and masklen(p_address) = 32 and not (
    p_address << '0.0.0.0/8' or p_address << '10.0.0.0/8' or p_address << '100.64.0.0/10' or
    p_address << '127.0.0.0/8' or p_address << '169.254.0.0/16' or p_address << '172.16.0.0/12' or
    p_address << '192.0.0.0/24' or p_address << '192.0.2.0/24' or p_address << '192.168.0.0/16' or
    p_address << '198.18.0.0/15' or p_address << '198.51.100.0/24' or p_address << '203.0.113.0/24' or
    p_address << '224.0.0.0/3')
$$;

create or replace function public.directory_error(p_status integer, p_code text, p_message text) returns jsonb
language sql immutable set search_path to '' as $$
  select jsonb_build_object('status', p_status, 'error', jsonb_build_object('code', p_code, 'message', p_message))
$$;

create or replace function public.directory_text(p_body jsonb, p_key text, p_max integer, p_required boolean default false)
returns text language plpgsql immutable set search_path to '' as $$
declare
  value text;
begin
  if p_body is null or not (p_body ? p_key) or jsonb_typeof(p_body -> p_key) = 'null' then
    if p_required then raise exception 'invalid_metadata:% is required.', p_key; end if;
    return '';
  end if;
  if jsonb_typeof(p_body -> p_key) <> 'string' then
    raise exception 'invalid_metadata:% must be a string.', p_key;
  end if;
  value := btrim(p_body ->> p_key);
  if (p_required and value = '') or octet_length(value) > p_max or value ~ '[[:cntrl:]]' then
    raise exception 'invalid_metadata:% must be 1-% printable UTF-8 bytes.', p_key, p_max;
  end if;
  return value;
end;
$$;

create or replace function public.directory_integer(p_body jsonb, p_key text, p_min integer, p_max integer, p_default integer default null)
returns integer language plpgsql immutable set search_path to '' as $$
begin
  if p_body is null or not (p_body ? p_key) or jsonb_typeof(p_body -> p_key) = 'null' then
    if p_default is null then raise exception 'invalid_metadata:% is required.', p_key; end if;
    return p_default;
  end if;
  if jsonb_typeof(p_body -> p_key) <> 'number' or (p_body ->> p_key) !~ '^-?[0-9]+$'
     or (p_body ->> p_key)::numeric not between p_min and p_max then
    raise exception 'invalid_metadata:% must be an integer %-%.', p_key, p_min, p_max;
  end if;
  return (p_body ->> p_key)::integer;
end;
$$;

create or replace function public.directory_list(p_body jsonb, p_key text, p_items integer, p_max integer)
returns text[] language plpgsql immutable set search_path to '' as $$
declare
  item jsonb;
  result text[] := '{}';
begin
  if p_body is null or not (p_body ? p_key) or jsonb_typeof(p_body -> p_key) = 'null' then return result; end if;
  if jsonb_typeof(p_body -> p_key) <> 'array' or jsonb_array_length(p_body -> p_key) > p_items then
    raise exception 'invalid_metadata:% must be an array of at most % strings.', p_key, p_items;
  end if;
  for item in select * from jsonb_array_elements(p_body -> p_key) loop
    if jsonb_typeof(item) <> 'string' or btrim(item #>> '{}') = '' or octet_length(btrim(item #>> '{}')) > p_max
       or (item #>> '{}') ~ '[[:cntrl:]]' then
      raise exception 'invalid_metadata:% entries must be 1-% printable UTF-8 bytes.', p_key, p_max;
    end if;
    if not btrim(item #>> '{}') = any(result) then result := result || btrim(item #>> '{}'); end if;
  end loop;
  return result;
end;
$$;

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
  fingerprint text;
begin
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
  if (p_body -> 'passworded') = 'true'::jsonb then
    return public.directory_error(422, 'password_listing_unsupported', 'Passworded servers cannot be listed publicly yet.');
  end if;

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
      client_version, fallout_runtime, content_manifest_fingerprint, capabilities, registered_at, last_seen)
    values (new_id, encode(extensions.digest(secret, 'sha256'), 'hex'), source, v_port,
      public.directory_text(p_body, 'name', 64, true), public.directory_text(p_body, 'description', 256),
      public.directory_text(p_body, 'region', 64), public.directory_list(p_body, 'tags', 8, 32),
      v_players, v_max,
      public.directory_text(compat, 'clientVersion', 64), public.directory_text(compat, 'falloutRuntime', 64),
      fingerprint, public.directory_list(compat, 'capabilities', 64, 64), now(), now())
    on conflict (host, port) do update set
      id = excluded.id, secret_hash = excluded.secret_hash, name = excluded.name, description = excluded.description,
      region = excluded.region, tags = excluded.tags, players = excluded.players, max_players = excluded.max_players,
      client_version = excluded.client_version, fallout_runtime = excluded.fallout_runtime,
      content_manifest_fingerprint = excluded.content_manifest_fingerprint, capabilities = excluded.capabilities,
      registered_at = now(), last_seen = now();
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

create or replace function public.authorize_live_server(p_id text, p_secret text, p_source text)
returns public.live_servers language plpgsql security definer set search_path to '' as $$
declare
  srv public.live_servers;
begin
  select * into srv from public.live_servers where id = p_id and last_seen >= now() - public.live_server_ttl();
  if not found or p_secret is null or char_length(p_secret) > 256
     or srv.secret_hash <> encode(extensions.digest(p_secret, 'sha256'), 'hex') then
    return null;
  end if;
  begin
    if host(p_source::inet)::inet <> srv.host then return null; end if;
  exception when others then
    return null;
  end;
  return srv;
end;
$$;

create or replace function public.heartbeat_live_server(p_id text, p_secret text, p_source text, p_body jsonb)
returns jsonb language plpgsql security definer set search_path to '' as $$
declare
  srv public.live_servers;
  v_max integer;
  v_players integer;
begin
  srv := public.authorize_live_server(p_id, p_secret, p_source);
  if srv.id is null then
    return public.directory_error(404, 'unknown_server', 'Listing expired or credentials are invalid; register again.');
  end if;
  begin
    v_max := public.directory_integer(p_body, 'maxPlayers', 1, 256, srv.max_players);
    v_players := public.directory_integer(p_body, 'players', 0, 256, srv.players);
    if v_players > v_max then raise exception 'invalid_metadata:players cannot exceed maxPlayers.'; end if;
  exception when raise_exception then
    return public.directory_error(422, 'invalid_metadata', substr(sqlerrm, 18));
  end;
  update public.live_servers set players = v_players, max_players = v_max, last_seen = now() where id = srv.id;
  return jsonb_build_object('status', 200, 'serverId', srv.id, 'players', v_players, 'maxPlayers', v_max,
    'heartbeatIntervalSeconds', 30, 'ttlSeconds', extract(epoch from public.live_server_ttl())::integer);
end;
$$;

create or replace function public.unregister_live_server(p_id text, p_secret text, p_source text)
returns jsonb language plpgsql security definer set search_path to '' as $$
declare
  srv public.live_servers;
begin
  srv := public.authorize_live_server(p_id, p_secret, p_source);
  if srv.id is null then
    return public.directory_error(404, 'unknown_server', 'Listing expired or credentials are invalid.');
  end if;
  delete from public.live_servers where id = srv.id;
  return jsonb_build_object('status', 200, 'removed', true);
end;
$$;

create or replace function public.list_live_servers()
returns table (id text, name text, description text, region text, tags text[], host text, port integer,
  players integer, max_players integer, client_version text, fallout_runtime text,
  content_manifest_fingerprint text, capabilities text[], last_seen timestamptz)
language sql stable security definer set search_path to '' as $$
  select s.id, s.name, s.description, s.region, s.tags, host(s.host), s.port, s.players, s.max_players,
    s.client_version, s.fallout_runtime, s.content_manifest_fingerprint, s.capabilities, s.last_seen
  from public.live_servers s
  where s.last_seen >= now() - public.live_server_ttl()
  order by s.players desc, s.name
  limit 512
$$;

create or replace function public.prune_live_servers() returns integer
language plpgsql security definer set search_path to '' as $$
declare
  removed integer;
begin
  delete from public.live_servers where last_seen < now() - public.live_server_ttl();
  get diagnostics removed = row_count;
  delete from public.server_directory_rate_limits where updated_at < now() - interval '1 day';
  return removed;
end;
$$;

revoke all on function public.consume_server_directory_rate_limit(text, integer, integer) from public, anon, authenticated;
revoke all on function public.register_live_server(text, jsonb, boolean) from public, anon, authenticated;
revoke all on function public.authorize_live_server(text, text, text) from public, anon, authenticated;
revoke all on function public.heartbeat_live_server(text, text, text, jsonb) from public, anon, authenticated;
revoke all on function public.unregister_live_server(text, text, text) from public, anon, authenticated;
revoke all on function public.prune_live_servers() from public, anon, authenticated;
revoke all on function public.directory_text(jsonb, text, integer, boolean) from public, anon, authenticated;
revoke all on function public.directory_integer(jsonb, text, integer, integer, integer) from public, anon, authenticated;
revoke all on function public.directory_list(jsonb, text, integer, integer) from public, anon, authenticated;
revoke all on function public.directory_error(integer, text, text) from public, anon, authenticated;
grant execute on function public.consume_server_directory_rate_limit(text, integer, integer) to service_role;
grant execute on function public.register_live_server(text, jsonb, boolean) to service_role;
grant execute on function public.heartbeat_live_server(text, text, text, jsonb) to service_role;
grant execute on function public.unregister_live_server(text, text, text) to service_role;
grant execute on function public.prune_live_servers() to service_role;
revoke all on function public.list_live_servers() from public;
grant execute on function public.list_live_servers() to anon, authenticated, service_role;
