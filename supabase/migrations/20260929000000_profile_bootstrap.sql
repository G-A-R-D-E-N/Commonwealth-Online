begin;

-- The profile trigger was added after some accounts already existed. Ensure
-- those accounts can use the profile page, and let the frontend repair a
-- missing row if one is ever created without the trigger.
insert into public.profiles (id, display_name, avatar_url)
select
  users.id,
  coalesce(nullif(left(trim(coalesce(
    nullif(users.raw_user_meta_data ->> 'display_name', ''),
    nullif(users.raw_user_meta_data ->> 'global_name', ''),
    nullif(users.raw_user_meta_data ->> 'full_name', ''),
    nullif(users.raw_user_meta_data ->> 'name', ''),
    nullif(users.raw_user_meta_data ->> 'user_name', ''),
    'Member'
  )), 80), ''), 'Member'),
  case users.raw_user_meta_data ->> 'avatar_url'
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
    when '/assets/profile-images/CO.png' then '/assets/profile-images/CO.png'
    when '/assets/profile-images/Cool.png' then '/assets/profile-images/Cool.png'
    when '/assets/profile-images/Love.png' then '/assets/profile-images/Love.png'
    when '/assets/profile-images/Rage.png' then '/assets/profile-images/Rage.png'
    when '/assets/profile-images/Wink.png' then '/assets/profile-images/Wink.png'
    else '/assets/profile-icons/armorer.png'
  end
from auth.users users
on conflict (id) do nothing;

drop policy if exists "Users can create their own profile" on public.profiles;
create policy "Users can create their own profile"
  on public.profiles for insert
  to authenticated
  with check (
    id = (select auth.uid())
    and role = 'member'
  );

commit;
