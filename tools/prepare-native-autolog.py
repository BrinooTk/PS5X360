"""Fetch verified MbedTLS and generate public console-client build inputs.

The client key grants report submission only. It is distributed in the ELF and
must never be reused as the Discord webhook or the owner's private upload key.
"""
from pathlib import Path
import hashlib, json, secrets, subprocess, shutil, tarfile, urllib.request

root = Path(__file__).resolve().parents[1]
deps = root / '.deps/autolog'
build = root / 'build/autolog-native'
deps.mkdir(parents=True, exist_ok=True)
build.mkdir(parents=True, exist_ok=True)
url = 'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2'
expected = 'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'
archive = deps / 'mbedtls-3.6.7.tar.bz2'
if not archive.exists():
    request = urllib.request.Request(url, headers={'User-Agent':'PS5X360-AutoLog-Build/1.0'})
    with urllib.request.urlopen(request, timeout=120) as response:
        archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('MbedTLS archive checksum mismatch; do not build.')
source = deps / 'mbedtls-3.6.7'
if not source.exists():
    with tarfile.open(archive) as files:
        files.extractall(deps, filter='data')

# Node ships the Mozilla CA set; record exactly which trust store was embedded.
roots = subprocess.check_output(['node','-e',"process.stdout.write(require('tls').rootCertificates.join('\\n')+'\\n')"], text=True)
assert roots.count('BEGIN CERTIFICATE') > 50
(build / 'roots.pem').write_text(roots, encoding='ascii')
(build / 'roots.h').write_text('static const char CA_ROOTS[] =\n' +
    '\n'.join(json.dumps(line+'\n') for line in roots.splitlines()) + ';\n', encoding='ascii')
config = build / 'console-client.json'
if config.exists():
    settings = json.loads(config.read_text())
else:
    settings = {'host':'ps5x360-logs.ps5x360-log-relay.workers.dev',
                'key':secrets.token_urlsafe(32)}
    config.write_text(json.dumps(settings), encoding='ascii')
assert settings['host'] == 'ps5x360-logs.ps5x360-log-relay.workers.dev'
(build / 'relay_config.h').write_text(
    '#define RELAY_HOST '+json.dumps(settings['host'])+'\n'+
    '#define CONSOLE_UPLOAD_KEY '+json.dumps(settings['key'])+'\n', encoding='ascii')
npx = shutil.which('npx.cmd') or shutil.which('npx')
result = subprocess.run([npx,'wrangler','secret','put','CONSOLE_TOKEN'],
    cwd=root/'server/autolog', input=settings['key']+'\n', text=True,
    encoding='utf-8', errors='replace', capture_output=True)
if result.returncode:
    raise SystemExit('Console submission key provisioning failed; keep build inputs and retry.')
print('MbedTLS verified; CA roots generated; write-only console submission credential provisioned.')
