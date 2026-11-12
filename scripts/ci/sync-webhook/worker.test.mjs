import test from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import worker from './worker.mjs';

const env = { WEBHOOK_SECRET: 'test-secret', PUBLIC_CI_DISPATCH_TOKEN: 'test-token' };
const repo = { id: 1439534514 };

function request(event, payload, secret = env.WEBHOOK_SECRET) {
    const body = JSON.stringify(payload);
    const signature = createHmac('sha256', secret).update(body).digest('hex');
    return new Request('https://example.test/github', { method: 'POST', headers: { 'x-github-event': event, 'x-hub-signature-256': `sha256=${signature}` }, body });
}

test('dispatches a signed private PR update to the public workflow', async () => {
    const original = globalThis.fetch;
    let call;
    globalThis.fetch = async (url, options) => { call = { url, options }; return new Response(null, { status: 204 }); };
    try {
        const response = await worker.fetch(request('pull_request', { repository: repo, action: 'synchronize', pull_request: { head: { repo } } }), env);
        assert.equal(response.status, 202);
        assert.match(call.url, /G-A-R-D-E-N\/Commonwealth-Online\/actions\/workflows\/public-ci-sync.yml\/dispatches$/);
        assert.deepEqual(JSON.parse(call.options.body), { ref: 'main' });
        assert.equal(call.options.headers.Authorization, 'Bearer test-token');
    } finally { globalThis.fetch = original; }
});

test('rejects invalid signatures and other repositories', async () => {
    assert.equal((await worker.fetch(request('push', { repository: repo, ref: 'refs/heads/nomad-dev' }, 'wrong'), env)).status, 401);
    assert.equal((await worker.fetch(request('push', { repository: { id: 1 }, ref: 'refs/heads/nomad-dev' }), env)).status, 403);
});

test('ignores unneeded events and checks configuration', async () => {
    assert.equal((await worker.fetch(request('push', { repository: repo, ref: 'refs/heads/feature' }), env)).status, 204);
    assert.equal((await worker.fetch(request('pull_request', { repository: repo, action: 'edited', pull_request: { head: { repo } } }), env)).status, 204);
    assert.equal((await worker.fetch(request('pull_request', { repository: repo, action: 'opened', pull_request: { head: { repo: { id: 2 } } } }), env)).status, 204);
    assert.equal((await worker.fetch(request('ping', { repository: repo }), env)).status, 204);
    assert.equal((await worker.fetch(request('ping', { repository: repo }), {})).status, 503);
});

test('surfaces a failed public dispatch', async () => {
    const original = globalThis.fetch;
    globalThis.fetch = async () => new Response(null, { status: 403 });
    try { assert.equal((await worker.fetch(request('push', { repository: repo, ref: 'refs/heads/nomad-dev' }), env)).status, 502); }
    finally { globalThis.fetch = original; }
});

test('dispatches a mainline update and handles network errors', async () => {
    const original = globalThis.fetch;
    globalThis.fetch = async () => new Response(null, { status: 204 });
    try {
        assert.equal((await worker.fetch(request('push', { repository: repo, ref: 'refs/heads/nomad-dev' }), env)).status, 202);
        globalThis.fetch = async () => { throw new Error('network'); };
        assert.equal((await worker.fetch(request('push', { repository: repo, ref: 'refs/heads/main' }), env)).status, 502);
    } finally { globalThis.fetch = original; }
});

test('rejects oversized bodies before dispatch', async () => {
    const body = 'x'.repeat(2 * 1024 * 1024 + 1);
    const req = new Request('https://example.test/github', { method: 'POST', body });
    assert.equal((await worker.fetch(req, env)).status, 413);
});
