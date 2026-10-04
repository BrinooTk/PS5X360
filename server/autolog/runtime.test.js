import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {Miniflare, convertV4MiniflareOptions} from 'miniflare';

test('Worker runtime forwards a report through a mocked receiver', async()=>{
  let received = false;
  const runtime = new Miniflare(convertV4MiniflareOptions({
    modules:[{type:'ESModule',path:'src/worker.js'},{type:'ESModule',path:'src/index.js'}],
    compatibilityDate:'2026-10-04',
    bindings:{UPLOAD_TOKEN:'test-only',DISCORD_WEBHOOK_URL:'https://discord.com/api/webhooks/123/test-only'},
    outboundService: async request => {
      assert.equal(new URL(request.url).hostname,'discord.com');
      const form = await request.formData();
      assert.ok(form.get('files[0]'));
      received = true;
      return new Response('{}',{status:200});
    }
  }));
  try {
    const body='PS5X360 diagnostic report v1\nGame: runtime test\n';
    const id=createHash('sha256').update(body).digest('hex');
    const response=await runtime.dispatchFetch('https://relay.example/v1/reports',{
      method:'POST',body,headers:{Authorization:'Bearer test-only','Content-Type':'text/plain','X-Report-ID':id}
    });
    assert.equal(response.status,200,await response.text());
    assert.ok(received);
  } finally { await runtime.dispose(); }
});
