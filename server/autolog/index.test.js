import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {handle, MAX_BYTES} from './src/index.js';
globalThis.crypto ??= webcrypto;
const env = {UPLOAD_TOKEN:'test-only', DISCORD_WEBHOOK_URL:'https://discord.com/api/webhooks/123/test-only'};
const body = 'PS5X360 diagnostic report v1\nGame: Sonic\nBuild: test\n';
async function request(text = body, extra = {}) {
  const id = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text))),
    b=>b.toString(16).padStart(2,'0')).join('');
  return new Request('https://relay.example/v1/reports', {method:'POST', body:text,
    headers:{'Authorization':'Bearer test-only','Content-Type':'text/plain','X-Report-ID':id,...extra}});
}
test('verified report becomes Discord attachment, without mentions', async()=>{
  let called = false;
  const response = await handle(await request(), env, async(url, options)=>{
    called = true;
    assert.equal(options.redirect, 'manual'); // Edge runtime rejects redirect:'error'.
    assert.equal(new URL(url).searchParams.get('wait'), 'true');
    assert.deepEqual(JSON.parse(options.body.get('payload_json')).allowed_mentions, {parse:[]});
    assert.equal(await options.body.get('files[0]').text(), body);
    return new Response('{}',{status:200});
  });
  assert.equal(response.status,200); assert.ok(called);
});
test('failed delivery is not acknowledged', async()=>{
  assert.equal((await handle(await request(),env,async()=>new Response('',{status:500}))).status,502);
});
test('console submission credential is separate from the owner credential', async()=>{
  const consoleEnv={...env,CONSOLE_TOKEN:'console-write-only'};
  assert.equal((await handle(await request(body,{Authorization:'Bearer console-write-only'}),consoleEnv,
    async()=>new Response('{}',{status:200}))).status,200);
  assert.equal((await handle(await request(body,{Authorization:'Bearer console-write-only'}),env,
    async()=>assert.fail('not configured'))).status,401);
});
test('authorization and rate limiting happen before forwarding', async()=>{
  const noSend = async()=>assert.fail('unexpected forwarding');
  assert.equal((await handle(await request(body,{Authorization:''}),env,noSend)).status,401);
  assert.equal((await handle(await request(),{...env,UPLOAD_LIMITER:{limit:async()=>({success:false})}},noSend)).status,429);
});
test('size and report validation', async()=>{
  const noSend=async()=>assert.fail('unexpected forwarding');
  assert.equal((await handle(await request('x'.repeat(MAX_BYTES+1)),env,noSend)).status,413);
  assert.equal((await handle(await request('wrong report'),env,noSend)).status,400);
  assert.equal((await handle(await request(body,{'X-Report-ID':'0'.repeat(64)}),env,noSend)).status,400);
});
