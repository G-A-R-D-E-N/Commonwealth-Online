begin;

create table if not exists public.user_badges (
  id bigint generated always as identity primary key,
  slug text not null unique check (slug ~ '^[a-z0-9]+(?:-[a-z0-9]+)*$'),
  name text not null check (char_length(name) between 1 and 50),
  description text not null default '' check (char_length(description) <= 160),
  sort_order integer not null default 0
);

create table if not exists public.user_badge_assignments (
  user_id uuid not null references public.profiles(id) on delete cascade,
  badge_id bigint not null references public.user_badges(id) on delete cascade,
  assigned_by uuid references public.profiles(id) on delete set null,
  is_displayed boolean not null default false,
  display_order smallint not null default 0 check (display_order between 0 and 10),
  assigned_at timestamptz not null default now(),
  primary key (user_id, badge_id)
);

create index if not exists user_badge_assignments_public_idx
  on public.user_badge_assignments (user_id, is_displayed, display_order);

alter table public.user_badges enable row level security;
alter table public.user_badge_assignments enable row level security;

revoke all on public.user_badges from anon, authenticated;
revoke all on public.user_badge_assignments from anon, authenticated;

grant select on public.user_badges to anon, authenticated;
grant select (user_id, badge_id, is_displayed, display_order)
  on public.user_badge_assignments
  to anon, authenticated;
grant update (is_displayed, display_order)
  on public.user_badge_assignments
  to authenticated;

grant all on public.user_badges to service_role;
grant all on public.user_badge_assignments to service_role;
grant usage, select on sequence public.user_badges_id_seq to service_role;

create policy "badge catalog is public"
on public.user_badges
for select
using (true);

create policy "badge assignments follow profile privacy"
on public.user_badge_assignments
for select
using (
  auth.uid() = user_id
  or exists (
    select 1
    from public.profiles viewer
    where viewer.id = auth.uid()
      and viewer.role = 'admin'
  )
  or (
    is_displayed
    and exists (
      select 1
      from public.user_profile_details details
      where details.user_id = user_badge_assignments.user_id
        and details.is_public
    )
  )
);

create policy "users choose displayed earned badges"
on public.user_badge_assignments
for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create or replace function private.guard_badge_display_limit()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  displayed_count integer;
begin
  if new.is_displayed
     and (tg_op = 'INSERT' or old.is_displayed is distinct from true) then
    select count(*)
    into displayed_count
    from public.user_badge_assignments a
    where a.user_id = new.user_id
      and a.is_displayed
      and a.badge_id <> new.badge_id;

    if displayed_count >= 3 then
      raise exception 'a user may display at most three badges';
    end if;
  end if;

  return new;
end;
$function$;

drop trigger if exists guard_badge_display_limit on public.user_badge_assignments;
create trigger guard_badge_display_limit
before insert or update of is_displayed on public.user_badge_assignments
for each row
execute function private.guard_badge_display_limit();

revoke all on function private.guard_badge_display_limit() from public, anon, authenticated;
grant execute on function private.guard_badge_display_limit() to service_role;

insert into public.user_badges (slug, name, description, sort_order)
values
  ('beta-tester', 'Beta Tester', 'Participates in Commonwealth Online testing.', 10),
  ('contributor', 'Contributor', 'Has contributed work to Commonwealth Online.', 20),
  ('mod-author', 'Mod Author', 'Creates mods or integration content for the community.', 30),
  ('server-host', 'Server Host', 'Operates a Commonwealth Online server.', 40),
  ('moderator', 'Moderator', 'Helps moderate Commonwealth Online community spaces.', 50),
  ('developer', 'Developer', 'Contributes directly to Commonwealth Online development.', 60),
  ('founder', 'Founder', 'Founding project or community member.', 70)
on conflict (slug) do update
set
  name = excluded.name,
  description = excluded.description,
  sort_order = excluded.sort_order;

commit;
