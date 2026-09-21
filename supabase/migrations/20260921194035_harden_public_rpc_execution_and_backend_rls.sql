drop policy if exists "backend only" on public.application_rate_limits;
create policy "backend only"
on public.application_rate_limits
for all
to anon, authenticated
using (false)
with check (false);

drop policy if exists "backend only" on public.applications;
create policy "backend only"
on public.applications
for all
to anon, authenticated
using (false)
with check (false);

drop policy if exists "backend only" on public.signup_rate_limits;
create policy "backend only"
on public.signup_rate_limits
for all
to anon, authenticated
using (false)
with check (false);

drop policy if exists "backend only" on public.user_accounts;
create policy "backend only"
on public.user_accounts
for all
to anon, authenticated
using (false)
with check (false);

alter function public.get_public_member_friends(uuid) set schema private;
alter function public.get_public_member_profile(uuid) set schema private;
alter function public.get_public_recent_servers(uuid) set schema private;
alter function public.get_public_user_characters(uuid) set schema private;

alter function public.cancel_faction_membership_request(uuid) set schema private;
alter function public.invite_faction_member(uuid, uuid) set schema private;
alter function public.is_forum_moderator() set schema private;
alter function public.leave_faction(uuid) set schema private;
alter function public.remove_faction_member(uuid, uuid) set schema private;
alter function public.request_faction_membership(uuid) set schema private;
alter function public.respond_faction_invite(uuid, boolean) set schema private;
alter function public.respond_faction_membership(uuid, uuid, boolean) set schema private;
alter function public.review_faction_application(uuid, text, text) set schema private;
alter function public.set_primary_faction(uuid) set schema private;

create or replace function private.get_public_username_history(p_user_id uuid)
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

revoke all on function private.get_public_username_history(uuid) from public, anon, authenticated;
grant execute on function private.get_public_username_history(uuid) to anon, authenticated, service_role;

create or replace function public.get_public_member_friends(p_user_id uuid)
returns table (
  id uuid,
  display_name text,
  avatar_url text
)
language sql
stable
security invoker
set search_path = ''
as $function$
  select * from private.get_public_member_friends(p_user_id);
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
security invoker
set search_path = ''
as $function$
  select * from private.get_public_member_profile(p_user_id);
$function$;

create or replace function public.get_public_recent_servers(p_user_id uuid)
returns table (
  server_id text,
  server_name text,
  last_joined_at timestamptz,
  session_count integer,
  total_seconds bigint
)
language sql
stable
security invoker
set search_path = ''
as $function$
  select * from private.get_public_recent_servers(p_user_id);
$function$;

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
security invoker
set search_path = ''
as $function$
  select * from private.get_public_user_characters(p_user_id);
$function$;

create or replace function public.get_public_username_history(p_user_id uuid)
returns table (
  username text,
  changed_at timestamptz
)
language sql
stable
security invoker
set search_path = ''
as $function$
  select * from private.get_public_username_history(p_user_id);
$function$;

revoke all on function public.get_public_member_friends(uuid) from public;
revoke all on function public.get_public_member_profile(uuid) from public;
revoke all on function public.get_public_recent_servers(uuid) from public;
revoke all on function public.get_public_user_characters(uuid) from public;
revoke all on function public.get_public_username_history(uuid) from public;

grant execute on function public.get_public_member_friends(uuid) to anon, authenticated, service_role;
grant execute on function public.get_public_member_profile(uuid) to anon, authenticated, service_role;
grant execute on function public.get_public_recent_servers(uuid) to anon, authenticated, service_role;
grant execute on function public.get_public_user_characters(uuid) to anon, authenticated, service_role;
grant execute on function public.get_public_username_history(uuid) to anon, authenticated, service_role;

create or replace function public.cancel_faction_membership_request(p_faction_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.cancel_faction_membership_request(p_faction_id);
$function$;

create or replace function public.invite_faction_member(p_faction_id uuid, p_user_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.invite_faction_member(p_faction_id, p_user_id);
$function$;

create or replace function public.is_forum_moderator()
returns boolean
language sql
stable
security invoker
set search_path = ''
as $function$
  select private.is_forum_moderator();
$function$;

create or replace function public.leave_faction(p_faction_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.leave_faction(p_faction_id);
$function$;

create or replace function public.remove_faction_member(p_faction_id uuid, p_user_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.remove_faction_member(p_faction_id, p_user_id);
$function$;

create or replace function public.request_faction_membership(p_faction_id uuid)
returns text
language sql
security invoker
set search_path = ''
as $function$
  select private.request_faction_membership(p_faction_id);
$function$;

create or replace function public.respond_faction_invite(p_faction_id uuid, p_accept boolean)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.respond_faction_invite(p_faction_id, p_accept);
$function$;

create or replace function public.respond_faction_membership(
  p_faction_id uuid,
  p_user_id uuid,
  p_accept boolean
)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.respond_faction_membership(p_faction_id, p_user_id, p_accept);
$function$;

create or replace function public.review_faction_application(
  p_application_id uuid,
  p_decision text,
  p_review_note text default null
)
returns uuid
language sql
security invoker
set search_path = ''
as $function$
  select private.review_faction_application(p_application_id, p_decision, p_review_note);
$function$;

create or replace function public.set_primary_faction(p_faction_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.set_primary_faction(p_faction_id);
$function$;

revoke all on function public.cancel_faction_membership_request(uuid) from public, anon;
revoke all on function public.invite_faction_member(uuid, uuid) from public, anon;
revoke all on function public.is_forum_moderator() from public, anon;
revoke all on function public.leave_faction(uuid) from public, anon;
revoke all on function public.remove_faction_member(uuid, uuid) from public, anon;
revoke all on function public.request_faction_membership(uuid) from public, anon;
revoke all on function public.respond_faction_invite(uuid, boolean) from public, anon;
revoke all on function public.respond_faction_membership(uuid, uuid, boolean) from public, anon;
revoke all on function public.review_faction_application(uuid, text, text) from public, anon;
revoke all on function public.set_primary_faction(uuid) from public, anon;

grant execute on function public.cancel_faction_membership_request(uuid) to authenticated, service_role;
grant execute on function public.invite_faction_member(uuid, uuid) to authenticated, service_role;
grant execute on function public.is_forum_moderator() to authenticated, service_role;
grant execute on function public.leave_faction(uuid) to authenticated, service_role;
grant execute on function public.remove_faction_member(uuid, uuid) to authenticated, service_role;
grant execute on function public.request_faction_membership(uuid) to authenticated, service_role;
grant execute on function public.respond_faction_invite(uuid, boolean) to authenticated, service_role;
grant execute on function public.respond_faction_membership(uuid, uuid, boolean) to authenticated, service_role;
grant execute on function public.review_faction_application(uuid, text, text) to authenticated, service_role;
grant execute on function public.set_primary_faction(uuid) to authenticated, service_role;
