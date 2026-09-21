begin;

create unique index if not exists forum_reports_open_post_reporter_unique
  on public.forum_reports (post_id, reporter_id)
  where status = 'open';

drop policy if exists "Authenticated users can create threads"
on public.forum_threads;

create policy "Authenticated users can create threads"
on public.forum_threads
for insert
to authenticated
with check (
  author_id = (select auth.uid())
  and exists (
    select 1
    from public.forum_categories
    where id = category_id
      and (
        not is_locked
        or private.is_forum_moderator()
      )
  )
);

create or replace function public.create_forum_thread(
  p_category_id bigint,
  p_title text,
  p_body text
)
returns bigint
language plpgsql
security invoker
set search_path = ''
as $function$
declare
  thread_id bigint;
begin
  insert into public.forum_threads (category_id, title)
  values (p_category_id, btrim(p_title))
  returning id into thread_id;

  insert into public.forum_posts (thread_id, body)
  values (thread_id, btrim(p_body));

  return thread_id;
end;
$function$;

revoke all on function public.create_forum_thread(bigint, text, text) from public, anon;
grant execute on function public.create_forum_thread(bigint, text, text) to authenticated, service_role;

create or replace function private.notify_forum_reply()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  thread_author uuid;
  thread_title text;
  actor_name text;
  post_count bigint;
  target_page bigint;
begin
  select author_id, title
  into thread_author, thread_title
  from public.forum_threads
  where id = new.thread_id;

  if thread_author is null or thread_author = new.author_id then
    return new;
  end if;

  select display_name
  into actor_name
  from public.profiles
  where id = new.author_id;

  select count(*)
  into post_count
  from public.forum_posts
  where thread_id = new.thread_id;

  target_page := greatest(1, ceil(post_count / 50.0)::bigint);

  insert into public.user_notifications (
    user_id,
    actor_id,
    type,
    title,
    body,
    target_url
  )
  values (
    thread_author,
    new.author_id,
    'system',
    'New forum reply',
    coalesce(actor_name, 'A Commonwealth Online member')
      || ' replied to "'
      || left(thread_title, 120)
      || '".',
    '/forum/thread/?id=' || new.thread_id::text || '&page=' || target_page::text
  );

  return new;
end;
$function$;

drop trigger if exists notify_forum_reply
on public.forum_posts;

create trigger notify_forum_reply
after insert on public.forum_posts
for each row
execute function private.notify_forum_reply();

revoke all on function private.notify_forum_reply() from public, anon, authenticated;
grant execute on function private.notify_forum_reply() to service_role;

create or replace function private.mark_forum_post_edited()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if new.body is distinct from old.body then
    new.edited_at := now();
  end if;
  return new;
end;
$function$;

drop trigger if exists mark_forum_post_edited
on public.forum_posts;

create trigger mark_forum_post_edited
before update of body on public.forum_posts
for each row
execute function private.mark_forum_post_edited();

revoke all on function private.mark_forum_post_edited() from public, anon, authenticated;
grant execute on function private.mark_forum_post_edited() to service_role;

create or replace function private.touch_forum_thread()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  target_thread_id bigint;
begin
  if tg_op = 'DELETE' then
    target_thread_id := old.thread_id;
  else
    target_thread_id := new.thread_id;
  end if;

  update public.forum_threads
  set updated_at = now()
  where id = target_thread_id;

  if tg_op = 'DELETE' then
    return old;
  end if;

  return new;
end;
$function$;

drop trigger if exists touch_forum_thread_on_post
on public.forum_posts;

create trigger touch_forum_thread_on_post
after insert or update or delete on public.forum_posts
for each row
execute function private.touch_forum_thread();

revoke all on function private.touch_forum_thread() from public, anon, authenticated;
grant execute on function private.touch_forum_thread() to service_role;

create or replace function private.moderate_forum_thread(
  p_thread_id bigint,
  p_locked boolean,
  p_pinned boolean
)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if not exists (
    select 1
    from public.profiles
    where id = (select auth.uid())
      and role in ('moderator', 'admin')
  ) then
    raise exception 'moderator access required';
  end if;

  update public.forum_threads
  set
    is_locked = p_locked,
    is_pinned = p_pinned,
    updated_at = now()
  where id = p_thread_id;

  if not found then
    raise exception 'forum thread not found';
  end if;
end;
$function$;

create or replace function public.moderate_forum_thread(
  p_thread_id bigint,
  p_locked boolean,
  p_pinned boolean
)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.moderate_forum_thread(p_thread_id, p_locked, p_pinned);
$function$;

revoke all on function private.moderate_forum_thread(bigint, boolean, boolean) from public, anon;
grant execute on function private.moderate_forum_thread(bigint, boolean, boolean) to authenticated, service_role;

revoke all on function public.moderate_forum_thread(bigint, boolean, boolean) from public, anon;
grant execute on function public.moderate_forum_thread(bigint, boolean, boolean) to authenticated, service_role;

commit;
