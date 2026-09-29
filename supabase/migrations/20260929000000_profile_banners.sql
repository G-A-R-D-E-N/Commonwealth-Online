begin;

-- Profile banners mirror the profile picture pattern: a curated allow-list of
-- local banner artwork shipped in Website/assets/profile-banners. Storing NULL
-- (or the empty string sent by the picker) means "no banner".

alter table public.profiles
  add column if not exists banner_url text;

alter table public.profiles
  drop constraint if exists profiles_banner_url_allowed;

alter table public.profiles
  add constraint profiles_banner_url_allowed
  check (
    banner_url is null
    or banner_url in (
      '/assets/profile-banners/AE-Art.webp',
      '/assets/profile-banners/Automatron.webp',
      '/assets/profile-banners/MuralBanner1.webp',
      '/assets/profile-banners/MuralBanner2.webp',
      '/assets/profile-banners/MuralBanner3.webp',
      '/assets/profile-banners/NukaGirl.webp',
      '/assets/profile-banners/Vertibird.webp'
    )
  );

-- Let owners change their banner just like their profile picture, and expose
-- the selected banner with the same column-level grant avatar_url uses.
grant select (id, display_name, avatar_url, banner_url, role)
  on public.profiles
  to anon, authenticated;

grant update (banner_url) on public.profiles to authenticated;

-- Public profile reads go through a SECURITY DEFINER RPC. The return type must
-- change to carry banner_url, and PostgreSQL cannot alter a function's return
-- type in place, so the private implementation and its public wrapper are
-- dropped and recreated with the same hardening and privilege posture.
drop function if exists private.get_public_member_profile(uuid);

create or replace function private.get_public_member_profile(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text,
  banner_url text,
  bio text,
  faction_id uuid,
  faction_name text,
  faction_tag text,
  playstyle text,
  show_friends boolean,
  joined_at timestamptz,
  presence_status text,
  current_server text
)
language sql
stable
security definer
set search_path = ''
as $function$
  select
    p.id,
    p.display_name,
    p.avatar_url,
    p.banner_url,
    d.bio,
    f.id as faction_id,
    f.name as faction_name,
    f.tag as faction_tag,
    d.playstyle,
    d.show_friends,
    case
      when d.show_joined_at then p.created_at
      else null
    end as joined_at,
    case
      when d.show_presence
        and not private.users_blocked(p.id, auth.uid())
      then pr.status
      else null
    end as presence_status,
    case
      when d.show_presence
        and pr.status = 'in_game'
        and not private.users_blocked(p.id, auth.uid())
      then pr.current_server
      else null
    end as current_server
  from public.profiles p
  join public.user_profile_details d
    on d.user_id = p.id
  left join public.factions f
    on f.id = d.primary_faction_id
   and f.status = 'active'
  left join public.user_presence pr
    on pr.user_id = p.id
  where p.id = p_user_id
    and d.is_public;
$function$;

revoke all on function private.get_public_member_profile(uuid) from public;
grant execute on function private.get_public_member_profile(uuid) to anon, authenticated, service_role;

drop function if exists public.get_public_member_profile(uuid);

create or replace function public.get_public_member_profile(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text,
  banner_url text,
  bio text,
  faction_id uuid,
  faction_name text,
  faction_tag text,
  playstyle text,
  show_friends boolean,
  joined_at timestamptz,
  presence_status text,
  current_server text
)
language sql
stable
security invoker
set search_path = ''
as $function$
  select * from private.get_public_member_profile(p_user_id);
$function$;

revoke all on function public.get_public_member_profile(uuid) from public;
grant execute on function public.get_public_member_profile(uuid) to anon, authenticated, service_role;

commit;