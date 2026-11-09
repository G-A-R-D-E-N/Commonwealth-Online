const repositoryId = 1439534514;
const dispatchUrl = 'https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/actions/workflows/public-ci-sync.yml/dispatches';
const maximumBytes = 2 * 1024 * 1024;

async function readBody(request) {
    const reader = request.body?.getReader();
    if (!reader) return new Uint8Array();
    const chunks = [];
    let size = 0;
    while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        size += value.byteLength;
        if (size > maximumBytes) {
            await reader.cancel();
            return null;
        }
        chunks.push(value);
    }
    const body = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) {
        body.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return body;
}

async function validSignature(body, header, secret) {
    if (!/^sha256=[0-9a-f]{64}$/i.test(header || '')) return false;
    const signature = Uint8Array.from(header.slice(7).match(/../g), pair => parseInt(pair, 16));
    const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']);
    return crypto.subtle.verify('HMAC', key, signature, body);
}

export default {
    async fetch(request, env) {
        if (new URL(request.url).pathname !== '/github') return new Response('Not found', { status: 404 });
        if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });
        if (!env.WEBHOOK_SECRET || !env.PUBLIC_CI_DISPATCH_TOKEN) return new Response('Not configured', { status: 503 });
        const body = await readBody(request);
        if (body === null) return new Response('Payload too large', { status: 413 });
        if (!await validSignature(body, request.headers.get('x-hub-signature-256'), env.WEBHOOK_SECRET)) return new Response('Invalid signature', { status: 401 });
        let payload;
        try { payload = JSON.parse(new TextDecoder().decode(body)); }
        catch { return new Response('Invalid JSON', { status: 400 }); }
        if (payload.repository?.id !== repositoryId) return new Response('Wrong repository', { status: 403 });
        const event = request.headers.get('x-github-event');
        if (event === 'ping') return new Response(null, { status: 204 });
        const pr = event === 'pull_request' && ['opened', 'reopened', 'synchronize', 'closed', 'ready_for_review'].includes(payload.action)
            && payload.pull_request?.head?.repo?.id === repositoryId;
        const mainline = event === 'push' && ['refs/heads/main', 'refs/heads/nomad-dev'].includes(payload.ref);
        if (!pr && !mainline) return new Response(null, { status: 204 });
        try {
            const response = await fetch(dispatchUrl, {
                method: 'POST',
                headers: { 'Authorization': `Bearer ${env.PUBLIC_CI_DISPATCH_TOKEN}`, 'Accept': 'application/vnd.github+json', 'Content-Type': 'application/json', 'User-Agent': 'co-ci-sync-webhook' },
                body: JSON.stringify({ ref: 'main' }),
                signal: AbortSignal.timeout(5000),
            });
            return response.ok ? new Response(null, { status: 202 }) : new Response('Dispatch failed', { status: 502 });
        } catch {
            return new Response('Dispatch failed', { status: 502 });
        }
    },
};
