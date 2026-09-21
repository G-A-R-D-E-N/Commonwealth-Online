begin;

create table public.factions (
  id uuid primary key default gen_random_uuid(),
  slug text not null unique
    check (slug = lower(slug) and slug ~ '^[a-z0-9][a-z0-9-]{1,79}$'),
  name text not null
    check (char_length(name) between 3 and 80),
  tag text not null
    check (char_length(tag) between 2 and 10),
  summary text not null
    check (char_length(summary) between 20 and 500),
  lore text not null
    check (char_length(lore) between 20 and 6000),
  focus text not null default 'mixed'
    check (focus in ('pve', 'pvp', 'roleplay', 'mixed')),
  recruitment text not null default 'open'
    check (recruitment in ('open', 'application', 'invite_only', 'closed')),
  emblem_url text
    check (emblem_url is null or char_length(emblem_url) <= 500),
  status text not null default 'active'
    check (status in ('active', 'suspended', 'archived')),
  created_by uuid not null references public.profiles(id) on delete restrict,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.faction_roles (
  id uuid primary key default gen_random_uuid(),
  faction_id uuid not null references public.factions(id) on delete cascade,
  name text not null check (char_length(name) between 2 and 40),
  priority integer not null default 100 check (priority between 0 and 1000),
  can_manage_members boolean not null default false,
  can_manage_roles boolean not null default false,
  can_edit_faction boolean not null default false,
  can_review_members boolean not null default false,
  created_at timestamptz not null default now(),
  unique (faction_id, name),
  unique (id, faction_id)
);

create table public.faction_members (
  faction_id uuid not null references public.factions(id) on delete cascade,
  user_id uuid not null references public.profiles(id) on delete cascade,
  role_id uuid,
  status text not null default 'active'
    check (status in ('pending', 'invited', 'active', 'left', 'removed')),
  joined_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  primary key (faction_id, user_id),
  foreign key (role_id, faction_id)
    references public.faction_roles(id, faction_id)
    on delete restrict
);

create table public.faction_applications (
  id uuid primary key default gen_random_uuid(),
  applicant_id uuid not null default auth.uid() references public.profiles(id) on delete cascade,
  proposed_name text not null check (char_length(proposed_name) between 3 and 80),
  proposed_tag text not null check (char_length(proposed_tag) between 2 and 10),
  summary text not null check (char_length(summary) between 20 and 500),
  lore text not null check (char_length(lore) between 20 and 6000),
  goals text not null check (char_length(goals) between 20 and 3000),
  focus text not null default 'mixed'
    check (focus in ('pve', 'pvp', 'roleplay', 'mixed')),
  recruitment text not null default 'open'
    check (recruitment in ('open', 'application', 'invite_only', 'closed')),
  status text not null default 'submitted'
    check (status in ('draft', 'submitted', 'reviewing', 'changes_requested', 'approved', 'rejected')),
  review_note text check (review_note is null or char_length(review_note) <= 2000),
  reviewed_by uuid references public.profiles(id) on delete set null,
  reviewed_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create unique index factions_name_unique_ci
  on public.factions (lower(name));

create unique index factions_tag_unique_ci
  on public.factions (upper(tag));

create index factions_status_created_idx
  on public.factions (status, created_at desc);

create index faction_members_user_status_idx
  on public.faction_members (user_id, status);

create index faction_members_faction_status_idx
  on public.faction_members (faction_id, status);

create index faction_applications_applicant_created_idx
  on public.faction_applications (applicant_id, created_at desc);

create index faction_applications_status_created_idx
  on public.faction_applications (status, created_at desc);

create unique index faction_applications_open_per_user
  on public.faction_applications (applicant_id)
  where status in ('draft', 'submitted', 'reviewing', 'changes_requested');

alter table public.factions enable row level security;
alter table public.faction_roles enable row level security;
alter table public.faction_members enable row level security;
alter table public.faction_applications enable row level security;

create policy "active factions are public"
on public.factions
for select
using (status = 'active');

create policy "staff and founders read nonpublic factions"
on public.factions
for select
to authenticated
using (created_by = auth.uid() or public.is_forum_moderator());

create policy "active faction roles are public"
on public.faction_roles
for select
using (
  exists (
    select 1
    from public.factions f
    where f.id = faction_roles.faction_id
      and f.status = 'active'
  )
);

create policy "staff read nonpublic faction roles"
on public.faction_roles
for select
to authenticated
using (public.is_forum_moderator());

create policy "active faction members are public"
on public.faction_members
for select
using (
  status = 'active'
  and exists (
    select 1
    from public.factions f
    where f.id = faction_members.faction_id
      and f.status = 'active'
  )
);

create policy "members and staff read own faction memberships"
on public.faction_members
for select
to authenticated
using (user_id = auth.uid() or public.is_forum_moderator());

create policy "users submit faction applications"
on public.faction_applications
for insert
to authenticated
with check (
  applicant_id = auth.uid()
  and status in ('draft', 'submitted')
);

create policy "users read own faction applications"
on public.faction_applications
for select
to authenticated
using (
  applicant_id = auth.uid()
  or public.is_forum_moderator()
);

create policy "users revise editable faction applications"
on public.faction_applications
for update
to authenticated
using (
  applicant_id = auth.uid()
  and status in ('draft', 'changes_requested')
)
with check (
  applicant_id = auth.uid()
  and status in ('draft', 'submitted')
  and reviewed_by is null
  and reviewed_at is null
);

create or replace function public.review_faction_application(
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
    where id = auth.uid()
      and role in ('moderator', 'admin')
  ) then
    raise exception 'staff access required';
  end if;

  if p_decision not in ('reviewing', 'changes_requested', 'approved', 'rejected') then
    raise exception 'invalid faction application decision';
  end if;

  select *
  into application_row
  from public.faction_applications
  where id = p_application_id
  for update;

  if not found then
    raise exception 'faction application not found';
  end if;

  if application_row.status in ('approved', 'rejected') then
    raise exception 'faction application is already closed';
  end if;

  if p_decision <> 'approved' then
    update public.faction_applications
    set
      status = p_decision,
      review_note = nullif(trim(p_review_note), ''),
      reviewed_by = auth.uid(),
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
    review_note = nullif(trim(p_review_note), ''),
    reviewed_by = auth.uid(),
    reviewed_at = now(),
    updated_at = now()
  where id = p_application_id;

  return faction_id;
end;
$function$;

revoke all on public.factions from anon, authenticated;
revoke all on public.faction_roles from anon, authenticated;
revoke all on public.faction_members from anon, authenticated;
revoke all on public.faction_applications from anon, authenticated;

grant select on public.factions to anon, authenticated;
grant select on public.faction_roles to anon, authenticated;
grant select on public.faction_members to anon, authenticated;
grant select, insert, update on public.faction_applications to authenticated;

revoke all on function public.review_faction_application(uuid, text, text) from public, anon;
grant execute on function public.review_faction_application(uuid, text, text) to authenticated, service_role;

commit;
