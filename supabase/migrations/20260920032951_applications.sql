begin;

create table public.applications (
  id bigint generated always as identity primary key,
  public_id uuid not null default gen_random_uuid() unique,
  type text not null check (type in ('team', 'beta')),
  discord_handle text not null check (char_length(discord_handle) between 2 and 64),
  display_name text not null check (char_length(display_name) between 2 and 80),
  email text not null check (char_length(email) <= 254),
  timezone text,
  availability text,
  experience text,
  motivation text not null,
  answers jsonb not null default '{}'::jsonb,
  status text not null default 'pending'
    check (status in ('pending', 'reviewing', 'more_info_requested', 'accepted', 'rejected')),
  source text not null default 'web' check (source in ('web', 'discord-bot', 'api')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index applications_status_created_idx
  on public.applications (status, created_at desc);

alter table public.applications enable row level security;

revoke all on public.applications from anon, authenticated;

commit;
