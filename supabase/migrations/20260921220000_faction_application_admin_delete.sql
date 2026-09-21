begin;

create or replace function private.delete_faction_application(p_application_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  deleted_count integer;
begin
  if not exists (
    select 1
    from public.profiles
    where id = (select auth.uid())
      and role = 'admin'
  ) then
    raise exception 'admin access required';
  end if;

  delete from public.faction_applications
  where id = p_application_id;

  get diagnostics deleted_count = row_count;
  if deleted_count = 0 then
    raise exception 'faction application not found';
  end if;
end;
$function$;

create or replace function public.delete_faction_application(p_application_id uuid)
returns void
language sql
security invoker
set search_path = ''
as $function$
  select private.delete_faction_application(p_application_id);
$function$;

revoke all on function private.delete_faction_application(uuid) from public, anon;
grant execute on function private.delete_faction_application(uuid) to authenticated, service_role;

revoke all on function public.delete_faction_application(uuid) from public, anon;
grant execute on function public.delete_faction_application(uuid) to authenticated, service_role;

commit;
