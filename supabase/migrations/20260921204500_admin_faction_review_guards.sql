begin;

drop policy if exists "users read own faction applications"
on public.faction_applications;

create policy "users and admins read faction applications"
on public.faction_applications
for select
to authenticated
using (
  applicant_id = (select auth.uid())
  or exists (
    select 1
    from public.profiles p
    where p.id = (select auth.uid())
      and p.role = 'admin'
  )
);

create or replace function private.review_faction_application(
  p_application_id uuid,
  p_decision text,
  p_review_note text default null
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $function$
declare
  application_row public.faction_applications%rowtype;
  faction_id uuid;
  leader_role_id uuid;
  faction_slug text;
begin
  if not exists (
    select 1
    from public.profiles
    where id = (select auth.uid())
      and role = 'admin'
  ) then
    raise exception 'admin access required';
  end if;

  if p_decision not in ('reviewing', 'changes_requested', 'approved', 'rejected') then
    raise exception 'invalid faction application decision';
  end if;

  if p_decision in ('changes_requested', 'rejected')
     and nullif(btrim(coalesce(p_review_note, '')), '') is null then
    raise exception 'review note required';
  end if;

  select *
  into application_row
  from public.faction_applications
  where id = p_application_id
  for update;

  if not found then
    raise exception 'faction application not found';
  end if;

  if application_row.status not in ('submitted', 'reviewing') then
    raise exception 'faction application is not reviewable in its current state';
  end if;

  if application_row.status = 'reviewing' and p_decision = 'reviewing' then
    raise exception 'faction application is already under review';
  end if;

  if p_decision <> 'approved' then
    update public.faction_applications
    set
      status = p_decision,
      review_note = nullif(btrim(p_review_note), ''),
      reviewed_by = (select auth.uid()),
      reviewed_at = now(),
      updated_at = now()
    where id = p_application_id;

    return null;
  end if;

  faction_slug :=
    coalesce(
      nullif(trim(both '-' from regexp_replace(lower(application_row.proposed_name), '[^a-z0-9]+', '-', 'g')), ''),
      'faction'
    )
    || '-'
    || left(replace(application_row.id::text, '-', ''), 8);

  insert into public.factions (
    slug,
    name,
    tag,
    summary,
    lore,
    focus,
    recruitment,
    created_by
  )
  values (
    faction_slug,
    application_row.proposed_name,
    upper(application_row.proposed_tag),
    application_row.summary,
    application_row.lore,
    application_row.focus,
    application_row.recruitment,
    application_row.applicant_id
  )
  returning id into faction_id;

  insert into public.faction_roles (
    faction_id,
    name,
    priority,
    can_manage_members,
    can_manage_roles,
    can_edit_faction,
    can_review_members
  )
  values (
    faction_id,
    'Leader',
    0,
    true,
    true,
    true,
    true
  )
  returning id into leader_role_id;

  insert into public.faction_roles (
    faction_id,
    name,
    priority
  )
  values (
    faction_id,
    'Member',
    100
  );

  insert into public.faction_members (
    faction_id,
    user_id,
    role_id,
    status,
    joined_at
  )
  values (
    faction_id,
    application_row.applicant_id,
    leader_role_id,
    'active',
    now()
  );

  update public.faction_applications
  set
    status = 'approved',
    review_note = nullif(btrim(p_review_note), ''),
    reviewed_by = (select auth.uid()),
    reviewed_at = now(),
    updated_at = now()
  where id = p_application_id;

  return faction_id;
end;
$function$;

commit;
