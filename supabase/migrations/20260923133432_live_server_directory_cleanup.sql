create extension if not exists pg_cron;

create index if not exists server_directory_rate_limits_updated_idx
  on public.server_directory_rate_limits (updated_at);

do $$
declare
  existing_job record;
begin
  for existing_job in
    select jobid
    from cron.job
    where jobname = 'commonwealth_server_directory_prune'
  loop
    perform cron.unschedule(existing_job.jobid);
  end loop;

  perform cron.schedule(
    'commonwealth_server_directory_prune',
    '*/5 * * * *',
    $cron$select public.prune_live_servers();$cron$
  );
end;
$$;
