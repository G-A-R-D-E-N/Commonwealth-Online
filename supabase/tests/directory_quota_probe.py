import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import uuid


parser = argparse.ArgumentParser()
parser.add_argument('--postgres-bin', type=Path, required=True)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
data = root / 'build/proofs' / ('postgres-data-' + uuid.uuid4().hex)
data.parent.mkdir(parents=True, exist_ok=True)
assert data.resolve().is_relative_to((root / 'build/proofs').resolve())
binary = args.postgres_bin.resolve()
suffix = '.exe' if os.name == 'nt' else ''
env = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ['PATH'])
env.pop('PGOPTIONS', None)
with socket.socket() as endpoint:
    endpoint.bind(('127.0.0.1', 0))
    port = endpoint.getsockname()[1]
base = [str(binary / ('psql' + suffix)), '-XqAt', '-v', 'ON_ERROR_STOP=1',
        '-h', '127.0.0.1', '-p', str(port), '-U', 'postgres', '-d', 'postgres']


def sql(text, check=True):
    result = subprocess.run(base, input=text, capture_output=True, text=True,
                            env=env, check=check, timeout=15)
    return result.stdout.strip() if check else result


def registration(host, port, **metadata):
    body = json.dumps({'name': 'quota-proof', 'port': port, 'maxPlayers': 8, **metadata})
    return f"select public.register_live_server('{host}', '{body}'::jsonb, true);"


def race(name, seed, first_query, second_query, limit, expected_second,
         finish='commit', expected_first=201):
    sql('truncate public.live_servers;' + seed)
    first = subprocess.Popen(base, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, env=env)
    with concurrent.futures.ThreadPoolExecutor() as pool:
        try:
            first.stdin.write('begin; set role service_role;\n' + first_query + '\n')
            first.stdin.flush()
            first_reply = json.loads(pool.submit(first.stdout.readline).result(timeout=8))
            assert first_reply['status'] == expected_first, name
            second = pool.submit(sql, "set role service_role; set application_name='quota_second';" + second_query)
            deadline = time.monotonic() + 5
            waited = False
            while not second.done():
                waited = sql("select exists(select 1 from pg_stat_activity where application_name='quota_second' and wait_event_type='Lock');") == 't'
                if waited:
                    break
                assert time.monotonic() < deadline, 'Second registration neither completed nor waited'
                time.sleep(0.02)
            first.stdin.write(f'{finish};\n\\q\n')
            first.stdin.flush()
            first.wait(timeout=8)
            assert first.returncode == 0, name
            second_reply = json.loads(second.result(timeout=8))
            count = int(sql('select count(*) from public.live_servers;'))
            print(json.dumps({'case': name, 'first_status': first_reply['status'],
                              'second_status': second_reply['status'], 'waited': waited,
                              'count': count, 'limit': limit}), flush=True)
            ok = count <= limit and second_reply['status'] == expected_second
            if not args.baseline:
                assert waited and ok, name
            return ok, first_reply, second_reply
        finally:
            if first.poll() is None:
                first.kill()
                first.wait()


def security_state():
    return sql("""
      select jsonb_build_object(
        'rls', (select bool_and(relrowsecurity) from pg_class where oid in
          ('public.live_servers'::regclass, 'public.server_directory_rate_limits'::regclass)),
        'anon_table', has_table_privilege('anon','public.live_servers','insert'),
        'authenticated_table', has_table_privilege('authenticated','public.live_servers','insert'),
        'anon_rpc', has_function_privilege('anon','public.register_live_server(text,jsonb,boolean)','execute'),
        'authenticated_rpc', has_function_privilege('authenticated','public.register_live_server(text,jsonb,boolean)','execute'),
        'service_rpc', has_function_privilege('service_role','public.register_live_server(text,jsonb,boolean)','execute'),
        'definer', p.prosecdef, 'settings', p.proconfig, 'acl', p.proacl, 'owner', p.proowner)
      from pg_proc p where p.oid='public.register_live_server(text,jsonb,boolean)'::regprocedure;
    """)


subprocess.run([str(binary / ('initdb' + suffix)), '-D', str(data), '-U', 'postgres',
                '-A', 'trust', '--encoding=UTF8', '--no-locale'], env=env, check=True,
               stdout=subprocess.DEVNULL)
started = False
try:
    subprocess.run([str(binary / ('pg_ctl' + suffix)), '-D', str(data), '-l', str(data / 'server.log'),
                    '-o', f'-h 127.0.0.1 -p {port}', '-w', 'start'], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    started = True
    assert sql('show transaction_isolation;') == 'read committed'
    sql('create role anon; create role authenticated; create role service_role; create schema extensions; create extension pgcrypto with schema extensions;')
    migrations = [root / 'supabase/migrations/20260923130526_live_servers.sql',
                  root / 'supabase/migrations/20260923131312_live_servers_list_via_edge_only.sql']
    for migration in migrations:
        sql(migration.read_text())
    before = security_state()
    if not args.baseline:
        migration = root / 'supabase/migrations/20261001154022_serialize_live_server_registration.sql'
        sql(migration.read_text())
        assert security_state() == before, 'Function privileges, owner, settings or RLS changed'
        state = json.loads(before)
        assert state['rls'] and state['service_rpc'] and state['definer']
        assert not any(state[key] for key in ['anon_table', 'authenticated_table', 'anon_rpc', 'authenticated_rpc'])
        print(json.dumps({'case': 'migration_security', 'unchanged': True,
                          'migration_sha256': hashlib.sha256(migration.read_bytes()).hexdigest()}))
    columns = 'id,secret_hash,host,port,name,max_players'
    ip_seed = f"insert into public.live_servers({columns}) select 'live-'||md5(n::text),repeat('a',64),'8.8.8.8'::inet,10000+n,'seed',8 from generate_series(1,3) n;"
    global_seed = f"insert into public.live_servers({columns}) select 'live-'||md5(n::text),repeat('a',64),('11.0.'||(n/256)||'.'||(n%256))::inet,10000+n,'seed',8 from generate_series(1,511) n;"
    per_ip = race('per_ip', ip_seed, registration('8.8.8.8', 40001), registration('8.8.8.8', 40002), 4, 429)
    global_cap = race('global', global_seed, registration('9.9.9.9', 40001), registration('8.8.8.8', 40002), 512, 503)
    if args.baseline:
        raise SystemExit(0 if per_ip[0] and global_cap[0] else 1)
    renewal = race('same_endpoint_renewal', ip_seed, registration('8.8.8.8', 40001),
                   registration('8.8.8.8', 40001), 4, 201)
    assert renewal[1]['serverId'] != renewal[2]['serverId'], 'Renewal did not rotate identity'
    assert sql(f"select id from public.live_servers where host='8.8.8.8' and port=40001;") == renewal[2]['serverId']
    for reply, expected in [(renewal[1], 404), (renewal[2], 200)]:
        status = sql(f"set role service_role; select public.heartbeat_live_server('{reply['serverId']}', '{reply['secret']}', '8.8.8.8', '{{}}'::jsonb)->>'status';")
        assert int(status) == expected, 'Renewal credentials were not rotated'
    race('global_same_endpoint_renewal', global_seed, registration('9.9.9.9', 40001),
         registration('9.9.9.9', 40001), 512, 201)
    race('rollback', ip_seed, registration('8.8.8.8', 40001), registration('8.8.8.8', 40002), 4, 201, finish='rollback')
    race('invalid_metadata', '', registration('8.8.8.8', 40001, name=''),
         registration('8.8.8.8', 40002), 1, 201, expected_first=422)
    sql('truncate public.live_servers;')
    sql(f"insert into public.live_servers({columns},last_seen) values ('live-'||md5('expired'),repeat('a',64),'8.8.8.8',10001,'seed',8,now()-interval '91 seconds');")
    for isolation in ['repeatable read', 'serializable', 'read uncommitted']:
        reply = json.loads(sql(f'begin isolation level {isolation}; select count(*) from public.live_servers;'
                               + registration('8.8.8.8', 40001) + 'commit;').splitlines()[-1])
        assert reply['status'] == 503 and reply['error']['code'] == 'unsupported_isolation'
        assert sql('select count(*) from public.live_servers;') == '1'
        print(json.dumps({'case': 'isolation_rejected', 'isolation': isolation, 'expired_row_preserved': True}))
    sql('truncate public.live_servers;')
    sql("create function public.quota_proof_error() returns trigger language plpgsql as $$ begin raise exception 'quota_proof_error'; end; $$;"
        'create trigger quota_proof_error before insert on public.live_servers for each row execute function public.quota_proof_error();')
    failed = sql(registration('8.8.8.8', 40001), check=False)
    assert failed.returncode != 0 and 'quota_proof_error' in failed.stderr
    sql('drop trigger quota_proof_error on public.live_servers; drop function public.quota_proof_error();')
    assert json.loads(sql(registration('8.8.8.8', 40001)))['status'] == 201
    assert sql("select count(*) from pg_locks where relation='public.live_servers'::regclass and mode='ShareRowExclusiveLock';") == '0'
    print(json.dumps({'case': 'exception_cleanup', 'registration_status': 201, 'remaining_registration_locks': 0}))
    sql('truncate public.live_servers;')
    suite = sql('set plpgsql.check_asserts=on;' + (root / 'supabase/tests/live_servers.test.sql').read_text(), check=False)
    assert suite.returncode != 0 and 'LIVE_SERVERS_SQL_TESTS_PASSED' in suite.stderr
    print(json.dumps({'case': 'existing_sql_suite', 'passed': True}))
finally:
    if started:
        subprocess.run([str(binary / ('pg_ctl' + suffix)), '-D', str(data), '-m', 'fast', '-w', 'stop'],
                       env=env, check=True, stdout=subprocess.DEVNULL)
