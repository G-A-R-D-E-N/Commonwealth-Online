revoke all on function public.list_live_servers() from public, anon, authenticated;
grant execute on function public.list_live_servers() to service_role;
