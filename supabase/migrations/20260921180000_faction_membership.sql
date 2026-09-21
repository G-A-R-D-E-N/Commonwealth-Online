begin;

alter table public.user_profile_details
  add column primary_faction_id uuid references public.factions(id) on delete set null;

create index user_profile_details_primary_faction_idx
  on public.user_profile_details (primary_faction_id);

create or replace function private.can_manage_faction_members(p_faction_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $function$
  select exists (
    select 1
    from public.faction_members m
    join public.faction_roles r
      on r.id = m.role_id
     and r.faction_id = m.faction_id
    where m.faction_id = p_faction_id
      and m.user_id = auth.uid()
      and m.status = 'active'
      and r.can_manage_members
  );
$function$;

create or replace function public.request_faction_membership(p_faction_id uuid)
returns text
language plpgsql
security definer
set search_path = ''
as $function$
declare
  requester_id uuid := auth.uid();
  recruitment_mode text;
  target_status text;
  default_role_id uuid;
begin
  if requester_id is null then
    raise exception 'authentication required';
  end if;

  select recruitment
  into recruitment_mode
  from public.factions
  where id = p_faction_id
    and status = 'active';

  if not found then
    raise exception 'faction not found';
  end if;

  if recruitment_mode in ('closed', 'invite_only') then
    raise exception 'faction is not accepting join requests';
  end if;

  if exists (
    select 1
    from public.faction_members
    where faction_id = p_faction_id
      and user_id = requester_id
      and status in ('active', 'pending', 'invited')
  ) then
    raise exception 'membership already exists';
  end if;

  target_status := case
    when recruitment_mode = 'open' then 'active'
    else 'pending'
  end;

  select id
  into default_role_id
  from public.faction_roles
  where faction_id = p_faction_id
    and name = 'Member'
  limit 1;

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
    requester_id,
    case when target_status = 'active' then default_role_id else null end,
    target_status,
    case when target_status = 'active' then now() else null end,
    now()
  )
  on conflict (faction_id, user_id)
  do update set
    role_id = excluded.role_id,
    status = excluded.status,
    joined_at = excluded.joined_at,
    updated_at = now();

  if target_status = 'active' then
    update public.user_profile_details
    set
      primary_faction_id = coalesce(primary_faction_id, p_faction_id),
      faction = null,
      updated_at = now()
    where user_id = requester_id;
  end if;

  return target_status;
end;
$function$;

create or replace function public.respond_faction_membership(
  p_faction_id uuid,
  p_user_id uuid,
  p_accept boolean
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  default_role_id uuid;
begin
  if not private.can_manage_faction_members(p_faction_id) then
    raise exception 'faction member management permission required';
  end if;

  if not exists (
    select 1
    from public.faction_members
    where faction_id = p_faction_id
      and user_id = p_user_id
      and status = 'pending'
  ) then
    raise exception 'pending membership request not found';
  end if;

  if p_accept then
    select id
    into default_role_id
    from public.faction_roles
    where faction_id = p_faction_id
      and name = 'Member'
    limit 1;

    update public.faction_members
    set
      role_id = default_role_id,
      status = 'active',
      joined_at = now(),
      updated_at = now()
    where faction_id = p_faction_id
      and user_id = p_user_id;

    update public.user_profile_details
    set
      primary_faction_id = coalesce(primary_faction_id, p_faction_id),
      faction = null,
      updated_at = now()
    where user_id = p_user_id;
  else
    update public.faction_members
    set
      role_id = null,
      status = 'removed',
      updated_at = now()
    where faction_id = p_faction_id
      and user_id = p_user_id;
  end if;
end;
$function$;

create or replace function public.invite_faction_member(
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

  if p_user_id = auth.uid() then
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
end;
$function$;

create or replace function public.respond_faction_invite(
  p_faction_id uuid,
  p_accept boolean
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  requester_id uuid := auth.uid();
  default_role_id uuid;
begin
  if requester_id is null then
    raise exception 'authentication required';
  end if;

  if not exists (
    select 1
    from public.faction_members
    where faction_id = p_faction_id
      and user_id = requester_id
      and status = 'invited'
  ) then
    raise exception 'faction invitation not found';
  end if;

  if p_accept then
    select id
    into default_role_id
    from public.faction_roles
    where faction_id = p_faction_id
      and name = 'Member'
    limit 1;

    update public.faction_members
    set
      role_id = default_role_id,
      status = 'active',
      joined_at = now(),
      updated_at = now()
    where faction_id = p_faction_id
      and user_id = requester_id;

    update public.user_profile_details
    set
      primary_faction_id = coalesce(primary_faction_id, p_faction_id),
      faction = null,
      updated_at = now()
    where user_id = requester_id;
  else
    update public.faction_members
    set
      role_id = null,
      status = 'removed',
      updated_at = now()
    where faction_id = p_faction_id
      and user_id = requester_id;
  end if;
end;
$function$;

create or replace function public.leave_faction(p_faction_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  requester_id uuid := auth.uid();
  current_role_id uuid;
  current_role_name text;
  replacement_faction_id uuid;
begin
  if requester_id is null then
    raise exception 'authentication required';
  end if;

  select m.role_id, r.name
  into current_role_id, current_role_name
  from public.faction_members m
  left join public.faction_roles r
    on r.id = m.role_id
   and r.faction_id = m.faction_id
  where m.faction_id = p_faction_id
    and m.user_id = requester_id
    and m.status = 'active';

  if not found then
    raise exception 'active membership not found';
  end if;

  if current_role_name = 'Leader' then
    raise exception 'faction leaders must transfer leadership before leaving';
  end if;

  update public.faction_members
  set
    role_id = null,
    status = 'left',
    updated_at = now()
  where faction_id = p_faction_id
    and user_id = requester_id;

  if exists (
    select 1
    from public.user_profile_details
    where user_id = requester_id
      and primary_faction_id = p_faction_id
  ) then
    select faction_id
    into replacement_faction_id
    from public.faction_members
    where user_id = requester_id
      and status = 'active'
      and faction_id <> p_faction_id
    order by joined_at asc nulls last
    limit 1;

    update public.user_profile_details
    set
      primary_faction_id = replacement_faction_id,
      updated_at = now()
    where user_id = requester_id;
  end if;
end;
$function$;

create or replace function public.set_primary_faction(p_faction_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if auth.uid() is null then
    raise exception 'authentication required';
  end if;

  if not exists (
    select 1
    from public.faction_members
    where faction_id = p_faction_id
      and user_id = auth.uid()
      and status = 'active'
  ) then
    raise exception 'active faction membership required';
  end if;

  update public.user_profile_details
  set
    primary_faction_id = p_faction_id,
    faction = null,
    updated_at = now()
  where user_id = auth.uid();
end;
$function$;

create or replace function public.get_public_member_profile(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text,
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

revoke all on function private.can_manage_faction_members(uuid) from public, anon, authenticated;

revoke all on function public.request_faction_membership(uuid) from public, anon;
revoke all on function public.respond_faction_membership(uuid, uuid, boolean) from public, anon;
revoke all on function public.invite_faction_member(uuid, uuid) from public, anon;
revoke all on function public.respond_faction_invite(uuid, boolean) from public, anon;
revoke all on function public.leave_faction(uuid) from public, anon;
revoke all on function public.set_primary_faction(uuid) from public, anon;
revoke all on function public.get_public_member_profile(uuid) from public;

grant execute on function public.request_faction_membership(uuid) to authenticated;
grant execute on function public.respond_faction_membership(uuid, uuid, boolean) to authenticated;
grant execute on function public.invite_faction_member(uuid, uuid) to authenticated;
grant execute on function public.respond_faction_invite(uuid, boolean) to authenticated;
grant execute on function public.leave_faction(uuid) to authenticated;
grant execute on function public.set_primary_faction(uuid) to authenticated;
grant execute on function public.get_public_member_profile(uuid) to anon, authenticated, service_role;

commit;
