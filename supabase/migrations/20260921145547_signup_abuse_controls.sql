create schema if not exists private;
revoke all on schema private from public, anon, authenticated;
grant usage on schema private to service_role;

alter table public.user_accounts
  add column if not exists risk_score integer not null default 0 check (risk_score between 0 and 100),
  add column if not exists risk_flags text[] not null default '{}'::text[],
  add column if not exists review_status text not null default 'clear'
    check (review_status in ('clear', 'flagged', 'reviewed')),
  add column if not exists reviewed_at timestamptz,
  add column if not exists review_note text;

create table if not exists public.signup_rate_limits (
  bucket text primary key,
  window_started_at timestamptz not null default now(),
  request_count integer not null default 0 check (request_count >= 0),
  updated_at timestamptz not null default now()
);

alter table public.signup_rate_limits enable row level security;
revoke all on table public.signup_rate_limits from public, anon, authenticated;
grant all on table public.signup_rate_limits to service_role;

create or replace function public.consume_signup_rate_limit(
  p_bucket text,
  p_limit integer,
  p_window_seconds integer
)
returns boolean
language plpgsql
security definer
set search_path = ''
as $function$
declare
  allowed boolean;
begin
  if p_limit < 1 or p_window_seconds < 1 or char_length(p_bucket) < 1 then
    raise exception 'invalid rate limit arguments';
  end if;

  insert into public.signup_rate_limits (bucket, window_started_at, request_count, updated_at)
  values (p_bucket, now(), 1, now())
  on conflict (bucket) do update
  set
    window_started_at = case
      when now() >= public.signup_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then now()
      else public.signup_rate_limits.window_started_at
    end,
    request_count = case
      when now() >= public.signup_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then 1
      else public.signup_rate_limits.request_count + 1
    end,
    updated_at = now()
  returning request_count <= p_limit into allowed;

  return allowed;
end;
$function$;

revoke execute on function public.consume_signup_rate_limit(text, integer, integer)
  from public, anon, authenticated;
grant execute on function public.consume_signup_rate_limit(text, integer, integer)
  to service_role;

create or replace function private.refresh_user_account_risk(p_user_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  account_row public.user_accounts%rowtype;
  auth_row auth.users%rowtype;
  local_part text;
  username_value text;
  username_vowels integer;
  email_vowels integer;
  score integer := 0;
  flags text[] := '{}'::text[];
begin
  select * into account_row
  from public.user_accounts
  where id = p_user_id;

  select * into auth_row
  from auth.users
  where id = p_user_id;

  if not found or account_row.id is null then
    return;
  end if;

  username_value := coalesce(account_row.username, '');
  local_part := split_part(coalesce(auth_row.email, ''), '@', 1);
  username_vowels := char_length(lower(username_value))
    - char_length(regexp_replace(lower(username_value), '[aeiou]', '', 'g'));
  email_vowels := char_length(lower(local_part))
    - char_length(regexp_replace(lower(local_part), '[aeiou]', '', 'g'));

  if auth_row.email_confirmed_at is null then
    score := score + 10;
    flags := array_append(flags, 'unconfirmed');
  end if;

  if auth_row.last_sign_in_at is null then
    score := score + 5;
    flags := array_append(flags, 'never_signed_in');
  end if;

  if char_length(username_value) >= 14
     and username_value ~ '^[A-Za-z0-9]+$'
     and username_vowels * 5 < char_length(username_value) then
    score := score + 30;
    flags := array_append(flags, 'randomized_username');
  end if;

  if local_part ~ '[0-9]'
     and (
       char_length(local_part)
       - char_length(regexp_replace(local_part, '[._+-]', '', 'g'))
     ) >= 4 then
    score := score + 20;
    flags := array_append(flags, 'fragmented_email_local');
  end if;

  if char_length(local_part) >= 18
     and local_part ~ '^[A-Za-z0-9._+-]+$'
     and email_vowels * 5 < greatest(char_length(local_part), 1) then
    score := score + 20;
    flags := array_append(flags, 'randomized_email_local');
  end if;

  if auth_row.email_confirmed_at is null
     and auth_row.last_sign_in_at is null
     and auth_row.created_at < now() - interval '24 hours' then
    score := score + 20;
    flags := array_append(flags, 'stale_unconfirmed_24h');
  end if;

  if auth_row.email_confirmed_at is null
     and auth_row.created_at < now() - interval '7 days' then
    score := score + 20;
    flags := array_append(flags, 'stale_unconfirmed_7d');
  end if;

  score := least(score, 100);

  update public.user_accounts
  set
    risk_score = score,
    risk_flags = flags,
    review_status = case
      when public.user_accounts.review_status = 'reviewed' then 'reviewed'
      when score >= 50 then 'flagged'
      else 'clear'
    end
  where id = p_user_id;
end;
$function$;

create or replace function private.refresh_user_account_risk_trigger()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  perform private.refresh_user_account_risk(new.id);
  return new;
end;
$function$;

drop trigger if exists zz_refresh_user_account_risk_auth on auth.users;
create trigger zz_refresh_user_account_risk_auth
after insert or update of email, email_confirmed_at, last_sign_in_at, raw_user_meta_data, updated_at
on auth.users
for each row
execute function private.refresh_user_account_risk_trigger();

drop trigger if exists zz_refresh_user_account_risk_profile on public.profiles;
create trigger zz_refresh_user_account_risk_profile
after insert or update of display_name, avatar_url, role, updated_at
on public.profiles
for each row
execute function private.refresh_user_account_risk_trigger();

create or replace function private.prune_flagged_unconfirmed_accounts(
  p_older_than interval default interval '14 days',
  p_dry_run boolean default true
)
returns table (
  id uuid,
  email text,
  username text,
  risk_score integer,
  action text
)
language plpgsql
security definer
set search_path = ''
as $function$
begin
  if p_older_than < interval '24 hours' then
    raise exception 'cleanup threshold must be at least 24 hours';
  end if;

  if p_dry_run then
    return query
    select
      u.id,
      u.email,
      a.username,
      a.risk_score,
      'would_delete'::text
    from auth.users u
    join public.user_accounts a on a.id = u.id
    where u.email_confirmed_at is null
      and u.last_sign_in_at is null
      and u.created_at < now() - p_older_than
      and a.review_status = 'flagged'
      and a.risk_score >= 50
    order by u.created_at;
    return;
  end if;

  return query
  with candidates as (
    select
      u.id,
      u.email,
      a.username,
      a.risk_score
    from auth.users u
    join public.user_accounts a on a.id = u.id
    where u.email_confirmed_at is null
      and u.last_sign_in_at is null
      and u.created_at < now() - p_older_than
      and a.review_status = 'flagged'
      and a.risk_score >= 50
  ),
  deleted as (
    delete from auth.users u
    using candidates c
    where u.id = c.id
    returning c.id, c.email, c.username, c.risk_score
  )
  select
    d.id,
    d.email,
    d.username,
    d.risk_score,
    'deleted'::text
  from deleted d;
end;
$function$;

revoke all on function private.refresh_user_account_risk(uuid) from public, anon, authenticated;
revoke all on function private.refresh_user_account_risk_trigger() from public, anon, authenticated;
revoke all on function private.prune_flagged_unconfirmed_accounts(interval, boolean)
  from public, anon, authenticated;

grant execute on function private.refresh_user_account_risk(uuid) to service_role;
grant execute on function private.refresh_user_account_risk_trigger() to service_role;
grant execute on function private.prune_flagged_unconfirmed_accounts(interval, boolean)
  to service_role;

select private.refresh_user_account_risk(id)
from public.user_accounts;
