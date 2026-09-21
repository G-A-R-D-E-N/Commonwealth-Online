begin;

create or replace function private.remove_faction_member(
  p_faction_id uuid,
  p_user_id uuid
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  target_role_name text;
begin
  if not private.can_manage_faction_members(p_faction_id) then
    raise exception 'faction member management permission required';
  end if;

  if p_user_id = (select auth.uid()) then
    raise exception 'use leave faction for your own membership';
  end if;

  select r.name
  into target_role_name
  from public.faction_members m
  left join public.faction_roles r
    on r.id = m.role_id
   and r.faction_id = m.faction_id
  where m.faction_id = p_faction_id
    and m.user_id = p_user_id
    and m.status = 'active';

  if not found then
    raise exception 'active faction member not found';
  end if;

  if target_role_name = 'Leader' then
    raise exception 'leader membership cannot be removed';
  end if;

  update public.faction_members
  set
    role_id = null,
    status = 'removed',
    updated_at = now()
  where faction_id = p_faction_id
    and user_id = p_user_id;

  update public.user_profile_details
  set
    primary_faction_id = null,
    updated_at = now()
  where user_id = p_user_id
    and primary_faction_id = p_faction_id;

  insert into public.user_notifications (
    user_id,
    actor_id,
    type,
    title,
    body,
    target_url
  )
  select
    p_user_id,
    (select auth.uid()),
    'system',
    'Faction membership removed',
    'Your membership in ' || f.name || ' was removed.',
    '/faction/?slug=' || f.slug
  from public.factions f
  where f.id = p_faction_id;
end;
$function$;

update public.user_notifications n
set target_url = '/faction/?slug=' || f.slug
from public.factions f
where n.target_url = '/faction/?id=' || f.id::text;

create or replace function private.invite_faction_member(
  p_faction_id uuid,
  p_user_id uuid
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if not private.can_manage_faction_members(p_faction_id) then
    raise exception 'faction member management permission required';
  end if;

  if p_user_id = (select auth.uid()) then
    raise exception 'cannot invite yourself';
  end if;

  if exists (
    select 1
    from public.faction_members
    where faction_id = p_faction_id
      and user_id = p_user_id
      and status in ('active', 'pending', 'invited')
  ) then
    raise exception 'membership already exists';
  end if;

  insert into public.faction_members (
    faction_id,
    user_id,
    role_id,
    status,
    joined_at,
    updated_at
  )
  values (
    p_faction_id,
    p_user_id,
    null,
    'invited',
    null,
    now()
  )
  on conflict (faction_id, user_id)
  do update set
    role_id = null,
    status = 'invited',
    joined_at = null,
    updated_at = now();

  insert into public.user_notifications (
    user_id,
    actor_id,
    type,
    title,
    body,
    target_url
  )
  select
    p_user_id,
    (select auth.uid()),
    'system',
    'Faction invitation',
    'You were invited to join ' || f.name || '.',
    '/faction/?slug=' || f.slug
  from public.factions f
  where f.id = p_faction_id;
end;
$function$;

commit;
