begin;

update public.profiles
set avatar_url = '/assets/profile-icons/vault-dweller.svg'
where avatar_url is null
   or avatar_url not in (
     '/assets/profile-icons/vault-dweller.svg',
     '/assets/profile-icons/minuteman.svg',
     '/assets/profile-icons/ranger.svg',
     '/assets/profile-icons/scribe.svg',
     '/assets/profile-icons/scavenger.svg',
     '/assets/profile-icons/atom-cat.svg'
   );

alter table public.profiles
  alter column avatar_url set default '/assets/profile-icons/vault-dweller.svg',
  alter column avatar_url set not null;

alter table public.profiles
  drop constraint if exists profiles_avatar_url_allowed;

alter table public.profiles
  add constraint profiles_avatar_url_allowed
  check (
    avatar_url in (
      '/assets/profile-icons/vault-dweller.svg',
      '/assets/profile-icons/minuteman.svg',
      '/assets/profile-icons/ranger.svg',
      '/assets/profile-icons/scribe.svg',
      '/assets/profile-icons/scavenger.svg',
      '/assets/profile-icons/atom-cat.svg'
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
      when '/assets/profile-icons/vault-dweller.svg' then '/assets/profile-icons/vault-dweller.svg'
      when '/assets/profile-icons/minuteman.svg' then '/assets/profile-icons/minuteman.svg'
      when '/assets/profile-icons/ranger.svg' then '/assets/profile-icons/ranger.svg'
      when '/assets/profile-icons/scribe.svg' then '/assets/profile-icons/scribe.svg'
      when '/assets/profile-icons/scavenger.svg' then '/assets/profile-icons/scavenger.svg'
      when '/assets/profile-icons/atom-cat.svg' then '/assets/profile-icons/atom-cat.svg'
      else '/assets/profile-icons/vault-dweller.svg'
    end
  )
  on conflict (id) do nothing;

  return new;
end;
$function$;

revoke all on function public.handle_new_user() from public;
grant execute on function public.handle_new_user() to service_role;

commit;
