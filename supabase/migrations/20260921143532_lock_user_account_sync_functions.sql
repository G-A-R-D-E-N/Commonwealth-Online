revoke execute on function public.sync_user_account_from_auth() from public, anon, authenticated;
revoke execute on function public.sync_user_account_from_profile() from public, anon, authenticated;
grant execute on function public.sync_user_account_from_auth() to service_role;
grant execute on function public.sync_user_account_from_profile() to service_role;
