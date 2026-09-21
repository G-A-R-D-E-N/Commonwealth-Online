begin;

alter table public.profiles
  drop constraint if exists profiles_avatar_url_allowed;

update public.profiles
set avatar_url = case avatar_url
  when '/assets/profile-icons/vault-dweller.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png'
  when '/assets/profile-icons/minuteman.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/cap_collector.png'
  when '/assets/profile-icons/ranger.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/rifleman.png'
  when '/assets/profile-icons/scribe.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/hacker.png'
  when '/assets/profile-icons/scavenger.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/scrapper.png'
  when '/assets/profile-icons/atom-cat.svg' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/medic.png'
  else 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png'
end;

alter table public.profiles
  alter column avatar_url set default 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png',
  alter column avatar_url set not null;

alter table public.profiles
  add constraint profiles_avatar_url_allowed
  check (
    avatar_url in (
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png',
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/hacker.png',
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/rifleman.png',
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/medic.png',
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/scrapper.png',
      'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/cap_collector.png'
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
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png'
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/hacker.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/hacker.png'
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/rifleman.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/rifleman.png'
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/medic.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/medic.png'
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/scrapper.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/scrapper.png'
      when 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/cap_collector.png' then 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/cap_collector.png'
      else 'https://cdn.jsdelivr.net/gh/CircuitBread0111/Fallout_Perk_Planner@918547cc872c3288122f9d15ed0416cf33aa8bbf/perk_images/armorer.png'
    end
  )
  on conflict (id) do nothing;

  return new;
end;
$function$;

revoke all on function public.handle_new_user() from public;
grant execute on function public.handle_new_user() to service_role;

commit;
