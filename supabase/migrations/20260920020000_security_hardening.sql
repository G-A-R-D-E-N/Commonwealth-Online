begin;

create index if not exists forum_threads_author_idx
  on public.forum_threads (author_id);

create index if not exists forum_posts_author_idx
  on public.forum_posts (author_id);

create index if not exists forum_reactions_user_idx
  on public.forum_reactions (user_id);

create index if not exists forum_reports_post_idx
  on public.forum_reports (post_id);

create index if not exists forum_reports_reporter_idx
  on public.forum_reports (reporter_id);

revoke all on function public.handle_new_user() from public;
grant execute on function public.handle_new_user() to service_role;

revoke all on function public.is_forum_moderator() from public;
grant execute on function public.is_forum_moderator() to authenticated, service_role;

commit;
