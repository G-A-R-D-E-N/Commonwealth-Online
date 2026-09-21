begin;

create table public.user_profile_details (
  user_id uuid primary key references public.profiles(id) on delete cascade,
  bio text not null default '' check (char_length(bio) <= 500),
  faction text check (faction is null or char_length(faction) <= 40),
  playstyle text check (playstyle is null or char_length(playstyle) <= 80),
  is_public boolean not null default false,
  show_presence boolean not null default true,
  show_friends boolean not null default true,
  show_joined_at boolean not null default true,
  updated_at timestamptz not null default now()
);

create table public.user_friendships (
  id bigint generated always as identity primary key,
  requester_id uuid not null references public.profiles(id) on delete cascade,
  addressee_id uuid not null references public.profiles(id) on delete cascade,
  status text not null default 'pending'
    check (status in ('pending', 'accepted', 'declined')),
  created_at timestamptz not null default now(),
  responded_at timestamptz,
  check (requester_id <> addressee_id)
);

create unique index user_friendships_pair_unique
  on public.user_friendships (
    least(requester_id, addressee_id),
    greatest(requester_id, addressee_id)
  );

create index user_friendships_requester_idx
  on public.user_friendships (requester_id, status);

create index user_friendships_addressee_idx
  on public.user_friendships (addressee_id, status);

create table public.user_blocks (
  blocker_id uuid not null references public.profiles(id) on delete cascade,
  blocked_id uuid not null references public.profiles(id) on delete cascade,
  created_at timestamptz not null default now(),
  primary key (blocker_id, blocked_id),
  check (blocker_id <> blocked_id)
);

create index user_blocks_blocked_idx
  on public.user_blocks (blocked_id);

create table public.user_presence (
  user_id uuid primary key references public.profiles(id) on delete cascade,
  status text not null default 'offline'
    check (status in ('online', 'away', 'in_game', 'offline')),
  current_server text check (current_server is null or char_length(current_server) <= 120),
  last_seen_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.user_notifications (
  id bigint generated always as identity primary key,
  user_id uuid not null references public.profiles(id) on delete cascade,
  actor_id uuid references public.profiles(id) on delete set null,
  type text not null check (type in ('friend_request', 'friend_accepted', 'system')),
  title text not null check (char_length(title) between 1 and 120),
  body text check (body is null or char_length(body) <= 500),
  target_url text check (target_url is null or char_length(target_url) <= 500),
  read_at timestamptz,
  created_at timestamptz not null default now()
);

create index user_notifications_user_created_idx
  on public.user_notifications (user_id, created_at desc);

alter table public.user_profile_details enable row level security;
alter table public.user_friendships enable row level security;
alter table public.user_blocks enable row level security;
alter table public.user_presence enable row level security;
alter table public.user_notifications enable row level security;

create or replace function private.users_blocked(p_left uuid, p_right uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $function$
  select exists (
    select 1
    from public.user_blocks b
    where (b.blocker_id = p_left and b.blocked_id = p_right)
       or (b.blocker_id = p_right and b.blocked_id = p_left)
  );
$function$;

revoke all on function private.users_blocked(uuid, uuid) from public;
grant usage on schema private to anon, authenticated, service_role;
grant execute on function private.users_blocked(uuid, uuid) to anon, authenticated, service_role;

grant select on public.user_profile_details to anon, authenticated;
grant insert, update, delete on public.user_profile_details to authenticated;

grant select, insert, delete on public.user_friendships to authenticated;
grant update (status) on public.user_friendships to authenticated;

grant select, insert, delete on public.user_blocks to authenticated;

grant select on public.user_presence to anon, authenticated;
grant insert, update, delete on public.user_presence to authenticated;

grant select, delete on public.user_notifications to authenticated;
grant update (read_at) on public.user_notifications to authenticated;

create policy "public profile details are visible when public"
on public.user_profile_details
for select
using (is_public or auth.uid() = user_id);

create policy "users create own profile details"
on public.user_profile_details
for insert
to authenticated
with check (auth.uid() = user_id);

create policy "users update own profile details"
on public.user_profile_details
for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create policy "users delete own profile details"
on public.user_profile_details
for delete
to authenticated
using (auth.uid() = user_id);

create policy "friendships visible to participants"
on public.user_friendships
for select
to authenticated
using (auth.uid() = requester_id or auth.uid() = addressee_id);

create policy "users request friendships"
on public.user_friendships
for insert
to authenticated
with check (
  auth.uid() = requester_id
  and requester_id <> addressee_id
  and not private.users_blocked(requester_id, addressee_id)
);

create policy "addressee responds to friendship"
on public.user_friendships
for update
to authenticated
using (auth.uid() = addressee_id and status = 'pending')
with check (auth.uid() = addressee_id and status in ('accepted', 'declined'));

create policy "participants remove friendships"
on public.user_friendships
for delete
to authenticated
using (auth.uid() = requester_id or auth.uid() = addressee_id);

create policy "users see own blocks"
on public.user_blocks
for select
to authenticated
using (auth.uid() = blocker_id);

create policy "users create own blocks"
on public.user_blocks
for insert
to authenticated
with check (auth.uid() = blocker_id);

create policy "users remove own blocks"
on public.user_blocks
for delete
to authenticated
using (auth.uid() = blocker_id);

create policy "presence visible by privacy settings"
on public.user_presence
for select
using (
  auth.uid() = user_id
  or (
    exists (
      select 1
      from public.user_profile_details d
      where d.user_id = user_presence.user_id
        and d.is_public
        and d.show_presence
    )
    and not private.users_blocked(user_presence.user_id, auth.uid())
  )
);

create policy "users create own presence"
on public.user_presence
for insert
to authenticated
with check (auth.uid() = user_id);

create policy "users update own presence"
on public.user_presence
for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create policy "users delete own presence"
on public.user_presence
for delete
to authenticated
using (auth.uid() = user_id);

create policy "users see own notifications"
on public.user_notifications
for select
to authenticated
using (auth.uid() = user_id);

create policy "users mark own notifications read"
on public.user_notifications
for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create policy "users delete own notifications"
on public.user_notifications
for delete
to authenticated
using (auth.uid() = user_id);

create or replace function private.handle_new_social_profile()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  insert into public.user_profile_details (user_id)
  values (new.id)
  on conflict (user_id) do nothing;

  insert into public.user_presence (user_id, status, last_seen_at, updated_at)
  values (new.id, 'offline', now(), now())
  on conflict (user_id) do nothing;

  return new;
end;
$function$;

create or replace function private.guard_friendship_update()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if new.requester_id <> old.requester_id or new.addressee_id <> old.addressee_id then
    raise exception 'friendship participants are immutable';
  end if;

  if old.status <> 'pending' or new.status not in ('accepted', 'declined') then
    raise exception 'invalid friendship transition';
  end if;

  new.responded_at := now();
  return new;
end;
$function$;

create or replace function private.handle_friendship_notification()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  actor_name text;
begin
  if tg_op = 'INSERT' and new.status = 'pending' then
    select display_name into actor_name from public.profiles where id = new.requester_id;

    insert into public.user_notifications (
      user_id, actor_id, type, title, body, target_url
    )
    values (
      new.addressee_id,
      new.requester_id,
      'friend_request',
      'New friend request',
      coalesce(actor_name, 'A Commonwealth Online member') || ' sent you a friend request.',
      '/profile/'
    );
  elsif tg_op = 'UPDATE' and old.status = 'pending' and new.status = 'accepted' then
    select display_name into actor_name from public.profiles where id = new.addressee_id;

    insert into public.user_notifications (
      user_id, actor_id, type, title, body, target_url
    )
    values (
      new.requester_id,
      new.addressee_id,
      'friend_accepted',
      'Friend request accepted',
      coalesce(actor_name, 'A Commonwealth Online member') || ' accepted your friend request.',
      '/profile/'
    );
  end if;

  return new;
end;
$function$;

create or replace function private.handle_user_block()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  delete from public.user_friendships
  where (requester_id = new.blocker_id and addressee_id = new.blocked_id)
     or (requester_id = new.blocked_id and addressee_id = new.blocker_id);

  return new;
end;
$function$;

create trigger create_social_profile_rows
after insert on public.profiles
for each row
execute function private.handle_new_social_profile();

create trigger guard_friendship_update
before update on public.user_friendships
for each row
execute function private.guard_friendship_update();

create trigger notify_friendship_change
after insert or update on public.user_friendships
for each row
execute function private.handle_friendship_notification();

create trigger remove_friendship_on_block
after insert on public.user_blocks
for each row
execute function private.handle_user_block();

revoke all on function private.handle_new_social_profile() from public, anon, authenticated;
revoke all on function private.guard_friendship_update() from public, anon, authenticated;
revoke all on function private.handle_friendship_notification() from public, anon, authenticated;
revoke all on function private.handle_user_block() from public, anon, authenticated;
grant execute on function private.handle_new_social_profile() to service_role;
grant execute on function private.guard_friendship_update() to service_role;
grant execute on function private.handle_friendship_notification() to service_role;
grant execute on function private.handle_user_block() to service_role;

insert into public.user_profile_details (user_id)
select id from public.profiles
on conflict (user_id) do nothing;

insert into public.user_presence (user_id, status, last_seen_at, updated_at)
select id, 'offline', now(), now()
from public.profiles
on conflict (user_id) do nothing;

commit;
