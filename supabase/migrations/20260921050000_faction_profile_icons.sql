begin;

alter table public.profiles
  drop constraint if exists profiles_avatar_url_allowed;

alter table public.profiles
  add constraint profiles_avatar_url_allowed
  check (
    avatar_url in (
      '/assets/profile-icons/armorer.png',
      '/assets/profile-icons/hacker.png',
      '/assets/profile-icons/rifleman.png',
      '/assets/profile-icons/medic.png',
      '/assets/profile-icons/scrapper.png',
      '/assets/profile-icons/cap_collector.png',
      '/assets/profile-images/Icon__Brotherhood.png',
      '/assets/profile-images/Icon__Institute.png',
      '/assets/profile-images/Icon__Minutemen.png',
      '/assets/profile-images/Icon__Railroad.png'
    )
  );

create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
begin
  insert into public.profiles (id, display_name, avatar_url)
  values (
    new.id,
    coalesce(
      nullif(new.raw_user_meta_data ->> 'display_name', ''),
      nullif(new.raw_user_meta_data ->> 'global_name', ''),
      nullif(new.raw_user_meta_data ->> 'full_name', ''),
      nullif(new.raw_user_meta_data ->> 'name', ''),
      nullif(new.raw_user_meta_data ->> 'user_name', ''),
      'Member'
    ),
    case new.raw_user_meta_data ->> 'avatar_url'
      when '/assets/profile-icons/armorer.png' then '/assets/profile-icons/armorer.png'
      when '/assets/profile-icons/hacker.png' then '/assets/profile-icons/hacker.png'
      when '/assets/profile-icons/rifleman.png' then '/assets/profile-icons/rifleman.png'
      when '/assets/profile-icons/medic.png' then '/assets/profile-icons/medic.png'
      when '/assets/profile-icons/scrapper.png' then '/assets/profile-icons/scrapper.png'
      when '/assets/profile-icons/cap_collector.png' then '/assets/profile-icons/cap_collector.png'
      when '/assets/profile-images/Icon__Brotherhood.png' then '/assets/profile-images/Icon__Brotherhood.png'
      when '/assets/profile-images/Icon__Institute.png' then '/assets/profile-images/Icon__Institute.png'
      when '/assets/profile-images/Icon__Minutemen.png' then '/assets/profile-images/Icon__Minutemen.png'
      when '/assets/profile-images/Icon__Railroad.png' then '/assets/profile-images/Icon__Railroad.png'
      else '/assets/profile-icons/armorer.png'
    end
  )
  on conflict (id) do nothing;

  return new;
end;
$function$;

revoke all on function public.handle_new_user() from public;
grant execute on function public.handle_new_user() to service_role;

commit;
