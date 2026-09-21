begin;

alter table public.user_profile_details
  add column if not exists show_recent_servers boolean not null default false;

create table if not exists public.user_server_favorites (
  user_id uuid not null references public.profiles(id) on delete cascade,
  server_id text not null check (char_length(server_id) between 1 and 120),
  server_name text not null check (char_length(server_name) between 1 and 120),
  created_at timestamptz not null default now(),
  primary key (user_id, server_id)
);

create table if not exists public.user_server_history (
  user_id uuid not null references public.profiles(id) on delete cascade,
  server_id text not null check (char_length(server_id) between 1 and 120),
  server_name text not null check (char_length(server_name) between 1 and 120),
  first_joined_at timestamptz not null,
  last_joined_at timestamptz not null,
  last_left_at timestamptz,
  session_count integer not null default 1 check (session_count >= 1),
  total_seconds bigint not null default 0 check (total_seconds >= 0),
  updated_at timestamptz not null default now(),
  primary key (user_id, server_id)
);

create index if not exists user_server_history_recent_idx
  on public.user_server_history (user_id, last_joined_at desc);

alter table public.user_server_favorites enable row level security;
alter table public.user_server_history enable row level security;

revoke all on public.user_server_favorites from public, anon, authenticated;
revoke all on public.user_server_history from public, anon, authenticated;

grant select, insert, delete on public.user_server_favorites to authenticated;
grant select on public.user_server_history to authenticated;
grant all on public.user_server_favorites to service_role;
grant all on public.user_server_history to service_role;

create policy "users manage own favorite servers"
on public.user_server_favorites
for all
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create policy "users read own server history"
on public.user_server_history
for select
to authenticated
using (auth.uid() = user_id);

create or replace function public.record_user_server_session(
  p_user_id uuid,
  p_server_id text,
  p_server_name text,
  p_joined_at timestamptz,
  p_left_at timestamptz
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  duration_seconds bigint;
begin
  if p_user_id is null
     or char_length(btrim(p_server_id)) < 1
     or char_length(btrim(p_server_name)) < 1
     or p_joined_at is null
     or p_left_at is null
     or p_left_at < p_joined_at then
    raise exception 'invalid server session';
  end if;

  duration_seconds := floor(extract(epoch from (p_left_at - p_joined_at)))::bigint;

  if duration_seconds > 604800 then
    raise exception 'server session exceeds seven days';
  end if;

  insert into public.user_server_history (
    user_id,
    server_id,
    server_name,
    first_joined_at,
    last_joined_at,
    last_left_at,
    session_count,
    total_seconds,
    updated_at
  )
  values (
    p_user_id,
    btrim(p_server_id),
    btrim(p_server_name),
    p_joined_at,
    p_joined_at,
    p_left_at,
    1,
    duration_seconds,
    now()
  )
  on conflict (user_id, server_id) do update
  set
    server_name = excluded.server_name,
    first_joined_at = least(public.user_server_history.first_joined_at, excluded.first_joined_at),
    last_joined_at = greatest(public.user_server_history.last_joined_at, excluded.last_joined_at),
    last_left_at = greatest(public.user_server_history.last_left_at, excluded.last_left_at),
    session_count = public.user_server_history.session_count + 1,
    total_seconds = public.user_server_history.total_seconds + excluded.total_seconds,
    updated_at = now();
end;
$function$;

revoke all on function public.record_user_server_session(uuid, text, text, timestamptz, timestamptz)
  from public, anon, authenticated;
grant execute on function public.record_user_server_session(uuid, text, text, timestamptz, timestamptz)
  to service_role;

create or replace function public.get_public_recent_servers(p_user_id uuid)
returns table (
  server_id text,
  server_name text,
  last_joined_at timestamptz,
  session_count integer,
  total_seconds bigint
)
language sql
stable
security definer
set search_path = ''
as $function$
  select
    h.server_id,
    h.server_name,
    h.last_joined_at,
    h.session_count,
    h.total_seconds
  from public.user_server_history h
  join public.user_profile_details d
    on d.user_id = h.user_id
   and d.is_public
   and d.show_recent_servers
  where h.user_id = p_user_id
    and not private.users_blocked(h.user_id, auth.uid())
  order by h.last_joined_at desc
  limit 5;
$function$;

revoke all on function public.get_public_recent_servers(uuid) from public;
grant execute on function public.get_public_recent_servers(uuid)
  to anon, authenticated, service_role;

commit;
