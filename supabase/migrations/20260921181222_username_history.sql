begin;

alter table public.user_profile_details
  add column if not exists show_username_history boolean not null default false;

create table if not exists public.user_username_history (
  id bigint generated always as identity primary key,
  user_id uuid not null references public.profiles(id) on delete cascade,
  username text not null check (char_length(username) between 1 and 80),
  changed_at timestamptz not null default now()
);

create index if not exists user_username_history_user_changed_idx
  on public.user_username_history (user_id, changed_at desc);

alter table public.user_username_history enable row level security;

revoke all on public.user_username_history from anon, authenticated;
grant select on public.user_username_history to authenticated;
grant all on public.user_username_history to service_role;
grant usage, select on sequence public.user_username_history_id_seq to service_role;

create policy "users and admins read username history"
on public.user_username_history
for select
to authenticated
using (
  auth.uid() = user_id
  or exists (
    select 1
    from public.profiles viewer
    where viewer.id = auth.uid()
      and viewer.role = 'admin'
  )
);

create or replace function private.capture_username_change()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if new.display_name is distinct from old.display_name then
    insert into public.user_username_history (user_id, username, changed_at)
    values (old.id, old.display_name, now());
  end if;

  return new;
end;
$function$;

drop trigger if exists capture_username_change on public.profiles;
create trigger capture_username_change
after update of display_name on public.profiles
for each row
when (old.display_name is distinct from new.display_name)
execute function private.capture_username_change();

revoke all on function private.capture_username_change() from public, anon, authenticated;
grant execute on function private.capture_username_change() to service_role;

create or replace function public.get_public_username_history(p_user_id uuid)
returns table (
  username text,
  changed_at timestamptz
)
language sql
stable
security definer
set search_path = ''
as $function$
  select h.username, h.changed_at
  from public.user_username_history h
  join public.user_profile_details d
    on d.user_id = h.user_id
   and d.is_public
   and d.show_username_history
  where h.user_id = p_user_id
  order by h.changed_at desc
  limit 10;
$function$;

revoke all on function public.get_public_username_history(uuid) from public;
grant execute on function public.get_public_username_history(uuid)
  to anon, authenticated, service_role;

commit;
