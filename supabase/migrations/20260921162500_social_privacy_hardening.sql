begin;

revoke select on public.profiles from anon, authenticated;
grant select (id, display_name, avatar_url, role)
  on public.profiles
  to anon, authenticated;

create or replace function public.get_public_member_profile(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text,
  bio text,
  faction text,
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
    d.bio,
    d.faction,
    d.playstyle,
    d.show_friends,
    case
      when d.show_joined_at then p.created_at
      else null
    end as joined_at,
    case
      when d.show_presence then pr.status
      else null
    end as presence_status,
    case
      when d.show_presence and pr.status = 'in_game' then pr.current_server
      else null
    end as current_server
  from public.profiles p
  join public.user_profile_details d
    on d.user_id = p.id
  left join public.user_presence pr
    on pr.user_id = p.id
  where p.id = p_user_id
    and d.is_public;
$function$;

create or replace function public.get_public_member_friends(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text
)
language sql
stable
security definer
set search_path = ''
as $function$
  select
    friend_profile.id,
    friend_profile.display_name,
    friend_profile.avatar_url
  from public.user_friendships f
  join public.user_profile_details owner_details
    on owner_details.user_id = p_user_id
   and owner_details.is_public
   and owner_details.show_friends
  join public.profiles friend_profile
    on friend_profile.id = case
      when f.requester_id = p_user_id then f.addressee_id
      else f.requester_id
    end
  join public.user_profile_details friend_details
    on friend_details.user_id = friend_profile.id
   and friend_details.is_public
  where f.status = 'accepted'
    and (f.requester_id = p_user_id or f.addressee_id = p_user_id)
  order by lower(friend_profile.display_name), friend_profile.id;
$function$;

revoke all on function public.get_public_member_profile(uuid) from public;
revoke all on function public.get_public_member_friends(uuid) from public;

grant execute on function public.get_public_member_profile(uuid)
  to anon, authenticated, service_role;
grant execute on function public.get_public_member_friends(uuid)
  to anon, authenticated, service_role;

commit;
