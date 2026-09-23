begin;

-- Replace the displayed badge set in one transaction. This prevents a
-- replacement from being rejected by the three-badge trigger while the old
-- selection is still present, and prevents partial client-side updates.
create or replace function public.set_displayed_badges(p_badge_ids bigint[])
returns void
language plpgsql
security definer
set search_path = ''
as $function$
declare
  requested_count integer := cardinality(coalesce(p_badge_ids, '{}'::bigint[]));
  distinct_count integer;
  missing_count integer;
begin
  if auth.uid() is null then
    raise exception 'authentication required';
  end if;

  select count(distinct badge_id)::integer
  into distinct_count
  from unnest(coalesce(p_badge_ids, '{}'::bigint[])) as requested(badge_id);

  if requested_count > 3 or requested_count <> distinct_count then
    raise exception 'a user may display at most three distinct badges';
  end if;

  select count(*)::integer
  into missing_count
  from (
    select distinct requested.badge_id
    from unnest(coalesce(p_badge_ids, '{}'::bigint[])) as requested(badge_id)
    left join public.user_badge_assignments assignment
      on assignment.user_id = auth.uid()
     and assignment.badge_id = requested.badge_id
    where assignment.badge_id is null
  ) missing;

  if missing_count > 0 then
    raise exception 'one or more selected badges have not been earned';
  end if;

  update public.user_badge_assignments
  set is_displayed = false,
      display_order = 0
  where user_id = auth.uid();

  update public.user_badge_assignments assignment
  set is_displayed = true,
      display_order = selected.display_order
  from (
    select requested.badge_id,
           requested.position::smallint - 1 as display_order
    from unnest(coalesce(p_badge_ids, '{}'::bigint[])) with ordinality
      as requested(badge_id, position)
  ) selected
  where assignment.user_id = auth.uid()
    and assignment.badge_id = selected.badge_id;
end;
$function$;

revoke all on function public.set_displayed_badges(bigint[]) from public, anon;
grant execute on function public.set_displayed_badges(bigint[]) to authenticated, service_role;

-- Block rows are intentionally private. This narrow RPC exposes only the
-- relationship state needed to render controls for the authenticated viewer.
create or replace function public.get_member_relationship(p_target_user_id uuid)
returns jsonb
language sql
stable
security definer
set search_path = ''
as $function$
  select jsonb_build_object(
    'blocked_target', exists (
      select 1
      from public.user_blocks b
      where b.blocker_id = auth.uid()
        and b.blocked_id = p_target_user_id
    ),
    'blocked_by_target', exists (
      select 1
      from public.user_blocks b
      where b.blocker_id = p_target_user_id
        and b.blocked_id = auth.uid()
    )
  )
  where auth.uid() is not null
    and p_target_user_id <> auth.uid();
$function$;

revoke all on function public.get_member_relationship(uuid) from public, anon;
grant execute on function public.get_member_relationship(uuid) to authenticated, service_role;

commit;
