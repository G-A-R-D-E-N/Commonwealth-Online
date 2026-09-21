begin;

create or replace function private.notify_faction_application_status()
returns trigger
language plpgsql
security definer
set search_path = ''
as $function$
declare
  notification_title text;
  notification_body text;
begin
  if new.status is not distinct from old.status then
    return new;
  end if;

  case new.status
    when 'reviewing' then
      notification_title := 'Faction application under review';
      notification_body := 'An administrator started reviewing your faction application.';
    when 'changes_requested' then
      notification_title := 'Faction application needs changes';
      notification_body := coalesce(
        'Changes were requested: ' || nullif(left(btrim(new.review_note), 380), ''),
        'An administrator requested changes to your faction application.'
      );
    when 'approved' then
      notification_title := 'Faction approved';
      notification_body := 'Your faction application was approved.';
    when 'rejected' then
      notification_title := 'Faction application denied';
      notification_body := coalesce(
        'Your faction application was denied: ' || nullif(left(btrim(new.review_note), 380), ''),
        'Your faction application was not approved.'
      );
    else
      return new;
  end case;

  insert into public.user_notifications (
    user_id,
    actor_id,
    type,
    title,
    body,
    target_url
  )
  values (
    new.applicant_id,
    new.reviewed_by,
    'system',
    notification_title,
    notification_body,
    '/factions/apply/'
  );

  return new;
end;
$function$;

drop trigger if exists notify_faction_application_status
on public.faction_applications;

create trigger notify_faction_application_status
after update of status on public.faction_applications
for each row
execute function private.notify_faction_application_status();

revoke all on function private.notify_faction_application_status()
from public, anon, authenticated;

grant execute on function private.notify_faction_application_status()
to service_role;

commit;
