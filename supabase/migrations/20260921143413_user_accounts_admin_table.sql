create table if not exists public.user_accounts (
  id uuid primary key references auth.users(id) on delete cascade,
  email text,
  username text not null default 'Member',
  avatar_url text not null default '/assets/profile-icons/armorer.png',
  role text not null default 'member',
  providers text[] not null default '{}'::text[],
  email_confirmed_at timestamptz,
  last_sign_in_at timestamptz,
  created_at timestamptz not null,
  updated_at timestamptz not null
);

alter table public.user_accounts enable row level security;

revoke all on table public.user_accounts from anon, authenticated;
grant all on table public.user_accounts to service_role;

create or replace function public.sync_user_account_from_auth()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  profile_row public.profiles%rowtype;
begin
  select *
  into profile_row
  from public.profiles
  where id = new.id;

  insert into public.user_accounts (
    id,
    email,
    username,
    avatar_url,
    role,
    providers,
    email_confirmed_at,
    last_sign_in_at,
    created_at,
    updated_at
  )
  values (
    new.id,
    new.email,
    coalesce(
      nullif(profile_row.display_name, ''),
      nullif(btrim(new.raw_user_meta_data ->> 'display_name'), ''),
      nullif(btrim(new.raw_user_meta_data ->> 'global_name'), ''),
      nullif(btrim(new.raw_user_meta_data ->> 'full_name'), ''),
      nullif(btrim(new.raw_user_meta_data ->> 'name'), ''),
      'Member'
    ),
    coalesce(profile_row.avatar_url, '/assets/profile-icons/armorer.png'),
    coalesce(profile_row.role, 'member'),
    coalesce(
      array(select jsonb_array_elements_text(coalesce(new.raw_app_meta_data -> 'providers', '[]'::jsonb))),
      '{}'::text[]
    ),
    new.email_confirmed_at,
    new.last_sign_in_at,
    new.created_at,
    new.updated_at
  )
  on conflict (id) do update
  set
    email = excluded.email,
    providers = excluded.providers,
    email_confirmed_at = excluded.email_confirmed_at,
    last_sign_in_at = excluded.last_sign_in_at,
    updated_at = greatest(public.user_accounts.updated_at, excluded.updated_at);

  return new;
end;
$function$;

create or replace function public.sync_user_account_from_profile()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  insert into public.user_accounts (
    id,
    email,
    username,
    avatar_url,
    role,
    providers,
    email_confirmed_at,
    last_sign_in_at,
    created_at,
    updated_at
  )
  select
    u.id,
    u.email,
    new.display_name,
    new.avatar_url,
    new.role,
    coalesce(
      array(select jsonb_array_elements_text(coalesce(u.raw_app_meta_data -> 'providers', '[]'::jsonb))),
      '{}'::text[]
    ),
    u.email_confirmed_at,
    u.last_sign_in_at,
    u.created_at,
    greatest(u.updated_at, new.updated_at)
  from auth.users u
  where u.id = new.id
  on conflict (id) do update
  set
    username = excluded.username,
    avatar_url = excluded.avatar_url,
    role = excluded.role,
    updated_at = greatest(public.user_accounts.updated_at, excluded.updated_at);

  return new;
end;
$function$;

drop trigger if exists sync_user_account_auth on auth.users;
create trigger sync_user_account_auth
after insert or update of email, email_confirmed_at, last_sign_in_at, raw_app_meta_data, raw_user_meta_data, updated_at
on auth.users
for each row
execute function public.sync_user_account_from_auth();

drop trigger if exists sync_user_account_profile on public.profiles;
create trigger sync_user_account_profile
after insert or update of display_name, avatar_url, role, updated_at
on public.profiles
for each row
execute function public.sync_user_account_from_profile();

insert into public.user_accounts (
  id,
  email,
  username,
  avatar_url,
  role,
  providers,
  email_confirmed_at,
  last_sign_in_at,
  created_at,
  updated_at
)
select
  u.id,
  u.email,
  coalesce(
    nullif(p.display_name, ''),
    nullif(btrim(u.raw_user_meta_data ->> 'display_name'), ''),
    nullif(btrim(u.raw_user_meta_data ->> 'global_name'), ''),
    nullif(btrim(u.raw_user_meta_data ->> 'full_name'), ''),
    nullif(btrim(u.raw_user_meta_data ->> 'name'), ''),
    'Member'
  ),
  coalesce(p.avatar_url, '/assets/profile-icons/armorer.png'),
  coalesce(p.role, 'member'),
  coalesce(
    array(select jsonb_array_elements_text(coalesce(u.raw_app_meta_data -> 'providers', '[]'::jsonb))),
    '{}'::text[]
  ),
  u.email_confirmed_at,
  u.last_sign_in_at,
  u.created_at,
  greatest(u.updated_at, coalesce(p.updated_at, u.updated_at))
from auth.users u
left join public.profiles p on p.id = u.id
on conflict (id) do update
set
  email = excluded.email,
  username = excluded.username,
  avatar_url = excluded.avatar_url,
  role = excluded.role,
  providers = excluded.providers,
  email_confirmed_at = excluded.email_confirmed_at,
  last_sign_in_at = excluded.last_sign_in_at,
  created_at = excluded.created_at,
  updated_at = excluded.updated_at;

revoke all on function public.sync_user_account_from_auth() from public;
revoke all on function public.sync_user_account_from_profile() from public;
grant execute on function public.sync_user_account_from_auth() to service_role;
grant execute on function public.sync_user_account_from_profile() to service_role;
