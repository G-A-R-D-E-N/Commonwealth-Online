begin;

alter table public.applications
  add column reviewed_by text,
  add column reviewed_at timestamptz,
  add column review_note text,
  add column discord_thread_id text;

create table public.application_rate_limits (
  bucket text primary key,
  window_started_at timestamptz not null default now(),
  request_count integer not null default 0 check (request_count >= 0),
  updated_at timestamptz not null default now()
);

alter table public.application_rate_limits enable row level security;

create or replace function public.consume_application_rate_limit(
  p_bucket text,
  p_limit integer,
  p_window_seconds integer
)
returns boolean
language plpgsql
security definer
set search_path = ''
as $$
declare
  allowed boolean;
begin
  if p_limit < 1 or p_window_seconds < 1 or char_length(p_bucket) < 1 then
    raise exception 'invalid rate limit arguments';
  end if;

  insert into public.application_rate_limits (bucket, window_started_at, request_count, updated_at)
  values (p_bucket, now(), 1, now())
  on conflict (bucket) do update
  set
    window_started_at = case
      when now() >= public.application_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then now()
      else public.application_rate_limits.window_started_at
    end,
    request_count = case
      when now() >= public.application_rate_limits.window_started_at + make_interval(secs => p_window_seconds)
      then 1
      else public.application_rate_limits.request_count + 1
    end,
    updated_at = now()
  returning request_count <= p_limit into allowed;

  return allowed;
end;
$$;

revoke all on public.application_rate_limits from public, anon, authenticated;
revoke all on function public.consume_application_rate_limit(text, integer, integer) from public, anon, authenticated;
grant execute on function public.consume_application_rate_limit(text, integer, integer) to service_role;

commit;
