begin;

alter table public.applications
  add column if not exists discord_post_attempts integer not null default 0,
  add column if not exists discord_post_next_attempt_at timestamptz;

create index if not exists applications_discord_post_due_idx
  on public.applications (discord_post_next_attempt_at)
  where discord_thread_id is null and discord_post_next_attempt_at is not null;

create or replace function public.claim_discord_posts(
  p_public_id uuid,
  p_limit integer,
  p_lease_seconds integer
)
returns setof public.applications
language plpgsql
security definer
set search_path = ''
as $$
begin
  if p_limit < 1 or p_lease_seconds < 1 then
    raise exception 'invalid claim arguments';
  end if;

  return query
  with due as (
    select a.id
    from public.applications a
    where a.discord_thread_id is null
      and a.status in ('pending', 'reviewing', 'more_info_requested')
      and a.discord_post_next_attempt_at is not null
      and a.discord_post_next_attempt_at <= now()
      and a.discord_post_attempts < 12
      and (p_public_id is null or a.public_id = p_public_id)
    order by a.discord_post_next_attempt_at, a.id
    limit p_limit
    for update skip locked
  )
  update public.applications t
  set discord_post_next_attempt_at = now() + make_interval(secs => p_lease_seconds)
  from due
  where t.id = due.id
  returning t.*;
end;
$$;

create or replace function public.record_discord_post_failure(p_public_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
begin
  update public.applications
  set
    discord_post_attempts = discord_post_attempts + 1,
    discord_post_next_attempt_at = now() + make_interval(
      mins => least(60, 5 * power(2, least(discord_post_attempts, 6))::integer)
    )
  where public_id = p_public_id
    and discord_thread_id is null
    and discord_post_next_attempt_at is not null;
end;
$$;

create or replace function public.enforce_application_limits()
returns trigger
language plpgsql
set search_path = ''
as $$
begin
  if char_length(coalesce(new.timezone, '')) > 64
    or char_length(coalesce(new.availability, '')) > 500
    or char_length(coalesce(new.experience, '')) > 2000
    or char_length(new.motivation) > 4000
    or char_length(new.answers::text) > 4000
  then
    raise exception 'application field exceeds its length limit' using errcode = '23514';
  end if;
  return new;
end;
$$;

drop trigger if exists applications_enforce_limits on public.applications;
create trigger applications_enforce_limits
  before insert on public.applications
  for each row execute function public.enforce_application_limits();

revoke all on function public.claim_discord_posts(uuid, integer, integer) from public, anon, authenticated;
revoke all on function public.record_discord_post_failure(uuid) from public, anon, authenticated;
revoke all on function public.enforce_application_limits() from public, anon, authenticated;
grant execute on function public.claim_discord_posts(uuid, integer, integer) to service_role;
grant execute on function public.record_discord_post_failure(uuid) to service_role;

commit;
