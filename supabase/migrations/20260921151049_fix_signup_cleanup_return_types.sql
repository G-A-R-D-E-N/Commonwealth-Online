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
      u.email::text,
      a.username::text,
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
      u.email::text as email,
      a.username::text as username,
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

revoke all on function private.prune_flagged_unconfirmed_accounts(interval, boolean)
  from public, anon, authenticated;
grant execute on function private.prune_flagged_unconfirmed_accounts(interval, boolean)
  to service_role;
