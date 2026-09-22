begin;

update public.user_notifications n
set target_url = '/member/?username=' || p.display_name
from public.profiles p
where n.target_url = '/member/?id=' || p.id::text;

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
    select display_name
    into actor_name
    from public.profiles
    where id = new.requester_id;

    insert into public.user_notifications (
      user_id,
      actor_id,
      type,
      title,
      body,
      target_url
    )
    values (
      new.addressee_id,
      new.requester_id,
      'friend_request',
      'New friend request',
      coalesce(actor_name, 'A Commonwealth Online member') || ' sent you a friend request.',
      case
        when actor_name is null then null
        else '/member/?username=' || actor_name
      end
    );
  elsif tg_op = 'UPDATE' and old.status = 'pending' and new.status = 'accepted' then
    select display_name
    into actor_name
    from public.profiles
    where id = new.addressee_id;

    insert into public.user_notifications (
      user_id,
      actor_id,
      type,
      title,
      body,
      target_url
    )
    values (
      new.requester_id,
      new.addressee_id,
      'friend_accepted',
      'Friend request accepted',
      coalesce(actor_name, 'A Commonwealth Online member') || ' accepted your friend request.',
      case
        when actor_name is null then null
        else '/member/?username=' || actor_name
      end
    );
  end if;

  return new;
end;
$function$;

commit;
