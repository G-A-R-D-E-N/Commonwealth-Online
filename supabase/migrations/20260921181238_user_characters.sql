begin;

alter table public.user_profile_details
  add column if not exists show_characters boolean not null default false;

create table if not exists public.user_characters (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references public.profiles(id) on delete cascade,
  server_id text not null check (char_length(server_id) between 1 and 120),
  server_name text not null check (char_length(server_name) between 1 and 120),
  character_name text not null check (char_length(character_name) between 1 and 80),
  level integer not null default 1 check (level between 1 and 65535),
  faction text check (faction is null or char_length(faction) <= 80),
  last_played_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (user_id, server_id, character_name)
);

create index if not exists user_characters_user_recent_idx
  on public.user_characters (user_id, last_played_at desc nulls last, updated_at desc);

alter table public.user_characters enable row level security;

revoke all on public.user_characters from public, anon, authenticated;
grant select on public.user_characters to authenticated;
grant all on public.user_characters to service_role;

create policy "users read own characters"
on public.user_characters
for select
to authenticated
using (auth.uid() = user_id);

create or replace function public.upsert_user_character(
  p_user_id uuid,
  p_server_id text,
  p_server_name text,
  p_character_name text,
  p_level integer,
  p_faction text,
  p_last_played_at timestamptz
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $function$
declare
  character_id uuid;
begin
  if p_user_id is null
     or char_length(btrim(p_server_id)) < 1
     or char_length(btrim(p_server_name)) < 1
     or char_length(btrim(p_character_name)) < 1
     or p_level < 1
     or p_level > 65535 then
    raise exception 'invalid character payload';
  end if;

  insert into public.user_characters (
    user_id,
    server_id,
    server_name,
    character_name,
    level,
    faction,
    last_played_at,
    updated_at
  )
  values (
    p_user_id,
    btrim(p_server_id),
    btrim(p_server_name),
    btrim(p_character_name),
    p_level,
    nullif(btrim(p_faction), ''),
    p_last_played_at,
    now()
  )
  on conflict (user_id, server_id, character_name) do update
  set
    server_name = excluded.server_name,
    level = excluded.level,
    faction = excluded.faction,
    last_played_at = excluded.last_played_at,
    updated_at = now()
  returning id into character_id;

  return character_id;
end;
$function$;

revoke all on function public.upsert_user_character(uuid, text, text, text, integer, text, timestamptz)
  from public, anon, authenticated;
grant execute on function public.upsert_user_character(uuid, text, text, text, integer, text, timestamptz)
  to service_role;

create or replace function public.get_public_user_characters(p_user_id uuid)
returns table (
  id uuid,
  server_id text,
  server_name text,
  character_name text,
  level integer,
  faction text,
  last_played_at timestamptz
)
language sql
stable
security definer
set search_path = ''
as $function$
  select
    c.id,
    c.server_id,
    c.server_name,
    c.character_name,
    c.level,
    c.faction,
    c.last_played_at
  from public.user_characters c
  join public.user_profile_details d
    on d.user_id = c.user_id
   and d.is_public
   and d.show_characters
  where c.user_id = p_user_id
    and not private.users_blocked(c.user_id, auth.uid())
  order by c.last_played_at desc nulls last, c.updated_at desc
  limit 10;
$function$;

revoke all on function public.get_public_user_characters(uuid) from public;
grant execute on function public.get_public_user_characters(uuid)
  to anon, authenticated, service_role;

commit;
