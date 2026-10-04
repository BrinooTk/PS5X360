// Independently implemented relay. The Discord webhook is a Worker secret.
export const MAX_BYTES = 2 * 1024 * 1024;
const answer = (status, text) => new Response(text, {
  status, headers: {"Content-Type": "text/plain", "Cache-Control": "no-store"}
});

async function boundedBody(request) {
  const reader = request.body?.getReader();
  if (!reader) return null;
  let length = 0;
  const chunks = [];
  for (;;) {
    const {done, value} = await reader.read();
    if (done) break;
    length += value.byteLength;
    if (length > MAX_BYTES) { await reader.cancel(); return null; }
    chunks.push(value);
  }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return bytes;
}

export async function handle(request, env, send = fetch) {
  const url = new URL(request.url);
  if (url.pathname === "/health" && request.method === "GET")
    return answer(200, "PS5X360 relay; receipt requires Discord acknowledgement.\n");
  if (url.pathname !== "/v1/reports") return answer(404, "Not found");
  if (request.method !== "POST") return answer(405, "POST required");
  if (!env.DISCORD_WEBHOOK_URL || !env.UPLOAD_TOKEN) return answer(503, "Relay setup incomplete");
  const authorization = request.headers.get("Authorization");
  // Public console clients have only a separate write-only submission key.
  // Neither credential grants access to the Discord webhook or stored messages.
  if (authorization !== `Bearer ${env.UPLOAD_TOKEN}` &&
      (!env.CONSOLE_TOKEN || authorization !== `Bearer ${env.CONSOLE_TOKEN}`))
    return answer(401, "Unauthorized");
  if (!request.headers.get("Content-Type")?.startsWith("text/plain"))
    return answer(415, "Text report required");
  const id = request.headers.get("X-Report-ID") || "";
  if (!/^[a-f0-9]{64}$/.test(id)) return answer(400, "Invalid report ID");
  if (Number(request.headers.get("Content-Length")) > MAX_BYTES)
    return answer(413, "Report too large");
  // Cloudflare provides this header. Rate limiting is per network address.
  if (env.UPLOAD_LIMITER) {
    const {success} = await env.UPLOAD_LIMITER.limit({
      key: request.headers.get("CF-Connecting-IP") || "unknown"
    });
    if (!success) return new Response("Try later", {status:429, headers:{"Retry-After":"60"}});
  }
  const bytes = await boundedBody(request);
  if (!bytes) return answer(413, "Report too large");
  const text = new TextDecoder().decode(bytes);
  if (!text.startsWith("PS5X360 diagnostic report v1\n")) return answer(400, "Invalid report");
  const computed = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)),
    b => b.toString(16).padStart(2, "0")).join("");
  if (computed !== id) return answer(400, "Report checksum mismatch");
  const webhook = new URL(env.DISCORD_WEBHOOK_URL);
  if (webhook.protocol !== "https:" || webhook.hostname !== "discord.com" ||
      !webhook.pathname.startsWith("/api/webhooks/")) return answer(503, "Invalid relay configuration");
  webhook.searchParams.set("wait", "true");
  const summary = (text.match(/^Game: (.*)$/m)?.[1] || "Unknown").slice(0, 160);
  const form = new FormData();
  form.append("payload_json", JSON.stringify({
    content: `PS5X360 diagnostics: ${summary}\nReport: ${id}`,
    allowed_mentions: {parse: []}
  }));
  form.append("files[0]", new Blob([bytes], {type:"text/plain"}), `PS5X360-${id.slice(0,16)}.txt`);
  try {
    const response = await send(webhook.toString(), {
      method: "POST", body: form, signal: AbortSignal.timeout(20000), redirect: "manual"
    });
    if (!response.ok) return answer(response.status === 429 ? 429 : 502,
      `Delivery pending (receiver HTTP ${response.status})`);
    // Do not return Discord channel/message details to the client.
    return answer(200, `accepted ${id}\n`);
  } catch (error) {
    const kind = ["TypeError", "TimeoutError", "AbortError", "Error"].includes(error?.name)
      ? error.name : "Error";
    // No report, URL or token in error responses. Useful for transport diagnosis.
    return answer(502, `Delivery pending (receiver request ${kind})`);
  }
}

export default {fetch(request, env) { return handle(request, env); }};
