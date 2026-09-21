begin;

create table public.profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  display_name text not null check (char_length(display_name) between 1 and 80),
  avatar_url text,
  role text not null default 'member' check (role in ('member', 'moderator', 'admin')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.forum_categories (
  id bigint generated always as identity primary key,
  slug text not null unique check (slug ~ '^[a-z0-9]+(?:-[a-z0-9]+)*$'),
  name text not null check (char_length(name) between 1 and 80),
  description text,
  position integer not null default 0,
  is_locked boolean not null default false,
  created_at timestamptz not null default now()
);

create table public.forum_threads (
  id bigint generated always as identity primary key,
  category_id bigint not null references public.forum_categories(id) on delete restrict,
  author_id uuid not null default auth.uid() references public.profiles(id) on delete restrict,
  title text not null check (char_length(title) between 3 and 160),
  is_pinned boolean not null default false,
  is_locked boolean not null default false,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index forum_threads_category_created_idx
  on public.forum_threads (category_id, is_pinned desc, created_at desc);

create table public.forum_posts (
  id bigint generated always as identity primary key,
  thread_id bigint not null references public.forum_threads(id) on delete cascade,
  author_id uuid not null default auth.uid() references public.profiles(id) on delete restrict,
  body text not null check (char_length(body) between 1 and 12000),
  edited_at timestamptz,
  created_at timestamptz not null default now()
);

create index forum_posts_thread_created_idx
  on public.forum_posts (thread_id, created_at);

create table public.forum_reactions (
  post_id bigint not null references public.forum_posts(id) on delete cascade,
  user_id uuid not null default auth.uid() references public.profiles(id) on delete cascade,
  emoji text not null check (emoji in ('👍', '❤️', '😂', '🎉', '👀')),
  created_at timestamptz not null default now(),
  primary key (post_id, user_id, emoji)
);

create table public.forum_reports (
  id bigint generated always as identity primary key,
  post_id bigint not null references public.forum_posts(id) on delete cascade,
  reporter_id uuid not null default auth.uid() references public.profiles(id) on delete cascade,
  reason text not null check (char_length(reason) between 3 and 1000),
  status text not null default 'open' check (status in ('open', 'reviewed', 'dismissed')),
  created_at timestamptz not null default now()
);

create or replace function public.is_forum_moderator()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1
    from public.profiles
    where id = (select auth.uid())
      and role in ('moderator', 'admin')
  );
$$;

revoke all on function public.is_forum_moderator() from public;
grant execute on function public.is_forum_moderator() to authenticated;

create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into public.profiles (id, display_name, avatar_url)
  values (
    new.id,
    coalesce(
      nullif(new.raw_user_meta_data ->> 'global_name', ''),
      nullif(new.raw_user_meta_data ->> 'full_name', ''),
      nullif(new.raw_user_meta_data ->> 'name', ''),
      nullif(new.raw_user_meta_data ->> 'user_name', ''),
      'Member'
    ),
    coalesce(
      nullif(new.raw_user_meta_data ->> 'avatar_url', ''),
      nullif(new.raw_user_meta_data ->> 'picture', '')
    )
  )
  on conflict (id) do nothing;

  return new;
end;
$$;

drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users
  for each row execute function public.handle_new_user();

alter table public.profiles enable row level security;
alter table public.forum_categories enable row level security;
alter table public.forum_threads enable row level security;
alter table public.forum_posts enable row level security;
alter table public.forum_reactions enable row level security;
alter table public.forum_reports enable row level security;

create policy "Public profiles are readable"
  on public.profiles for select
  using (true);

create policy "Users can update their own profile"
  on public.profiles for update
  to authenticated
  using (id = (select auth.uid()))
  with check (id = (select auth.uid()));

create policy "Forum categories are public"
  on public.forum_categories for select
  using (true);

create policy "Forum threads are public"
  on public.forum_threads for select
  using (true);

create policy "Authenticated users can create threads"
  on public.forum_threads for insert
  to authenticated
  with check (
    author_id = (select auth.uid())
    and exists (
      select 1
      from public.forum_categories
      where id = category_id
        and not is_locked
    )
  );

create policy "Authors can edit unlocked threads"
  on public.forum_threads for update
  to authenticated
  using (
    (author_id = (select auth.uid()) and not is_locked)
    or public.is_forum_moderator()
  )
  with check (
    author_id = (select auth.uid())
    or public.is_forum_moderator()
  );

create policy "Authors can delete unlocked threads"
  on public.forum_threads for delete
  to authenticated
  using (
    (author_id = (select auth.uid()) and not is_locked)
    or public.is_forum_moderator()
  );

create policy "Forum posts are public"
  on public.forum_posts for select
  using (true);

create policy "Authenticated users can reply to unlocked threads"
  on public.forum_posts for insert
  to authenticated
  with check (
    author_id = (select auth.uid())
    and exists (
      select 1
      from public.forum_threads
      where id = thread_id
        and not is_locked
    )
  );

create policy "Authors can edit posts in unlocked threads"
  on public.forum_posts for update
  to authenticated
  using (
    (
      author_id = (select auth.uid())
      and exists (
        select 1
        from public.forum_threads
        where id = thread_id
          and not is_locked
      )
    )
    or public.is_forum_moderator()
  )
  with check (
    author_id = (select auth.uid())
    or public.is_forum_moderator()
  );

create policy "Authors can delete posts in unlocked threads"
  on public.forum_posts for delete
  to authenticated
  using (
    (
      author_id = (select auth.uid())
      and exists (
        select 1
        from public.forum_threads
        where id = thread_id
          and not is_locked
      )
    )
    or public.is_forum_moderator()
  );

create policy "Forum reactions are public"
  on public.forum_reactions for select
  using (true);

create policy "Users can add their own reactions"
  on public.forum_reactions for insert
  to authenticated
  with check (user_id = (select auth.uid()));

create policy "Users can remove their own reactions"
  on public.forum_reactions for delete
  to authenticated
  using (user_id = (select auth.uid()));

create policy "Users can report posts"
  on public.forum_reports for insert
  to authenticated
  with check (reporter_id = (select auth.uid()));

create policy "Moderators can read reports"
  on public.forum_reports for select
  to authenticated
  using (public.is_forum_moderator());

create policy "Moderators can update reports"
  on public.forum_reports for update
  to authenticated
  using (public.is_forum_moderator())
  with check (public.is_forum_moderator());

revoke all on public.profiles from anon, authenticated;
revoke all on public.forum_categories from anon, authenticated;
revoke all on public.forum_threads from anon, authenticated;
revoke all on public.forum_posts from anon, authenticated;
revoke all on public.forum_reactions from anon, authenticated;
revoke all on public.forum_reports from anon, authenticated;

grant select on public.profiles to anon, authenticated;
grant update (display_name, avatar_url) on public.profiles to authenticated;

grant select on public.forum_categories to anon, authenticated;

grant select on public.forum_threads to anon, authenticated;
grant insert (category_id, title) on public.forum_threads to authenticated;
grant update (title) on public.forum_threads to authenticated;
grant delete on public.forum_threads to authenticated;

grant select on public.forum_posts to anon, authenticated;
grant insert (thread_id, body) on public.forum_posts to authenticated;
grant update (body) on public.forum_posts to authenticated;
grant delete on public.forum_posts to authenticated;

grant select on public.forum_reactions to anon, authenticated;
grant insert (post_id, emoji) on public.forum_reactions to authenticated;
grant delete on public.forum_reactions to authenticated;

grant insert (post_id, reason) on public.forum_reports to authenticated;
grant select on public.forum_reports to authenticated;
grant update (status) on public.forum_reports to authenticated;

insert into public.forum_categories (slug, name, description, position, is_locked)
values
  ('announcements', 'Announcements', 'Official Commonwealth Online news and notices.', 10, true),
  ('development', 'Development', 'Engineering, networking and project development discussion.', 20, false),
  ('multiplayer-help', 'Multiplayer Help', 'Installation, connection and multiplayer troubleshooting.', 30, false),
  ('bug-reports', 'Bug Reports', 'Reproducible issues and playtest findings.', 40, false),
  ('mod-compatibility', 'Mod Compatibility', 'Compatibility reports and configuration discussion.', 50, false),
  ('servers', 'Servers', 'Public server discussion and hosting topics.', 60, false),
  ('suggestions', 'Suggestions', 'Feature proposals and community feedback.', 70, false),
  ('general', 'General', 'General Commonwealth Online discussion.', 80, false)
on conflict (slug) do update
set
  name = excluded.name,
  description = excluded.description,
  position = excluded.position,
  is_locked = excluded.is_locked;

commit;
