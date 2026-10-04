"""Owner-only setup: create a local upload token and store it as a Worker secret.

Requires an authenticated Wrangler. Does not print or publish the token. No logs
are uploaded and the collector is left disabled until the webhook is configured.
"""
import argparse
import json
from pathlib import Path
import secrets
import shutil
import subprocess
import autolog

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--endpoint', required=True)
parser.add_argument('--host', required=True)
args = parser.parse_args()
path = autolog.HOME / 'config.json'
if path.exists():
    config = json.loads(path.read_text(encoding='utf-8'))
    if config['endpoint'] != args.endpoint or config['host'] != args.host:
        raise SystemExit('Existing collector configuration differs; preserve it and configure explicitly.')
else:
    config = {'host':args.host, 'port':2121, 'endpoint':args.endpoint,
              'upload_token':secrets.token_urlsafe(32), 'enabled':False}
autolog.validate(config)
# Persist first: interrupted provisioning won't lose the token needed to retry.
autolog.atomic_json(path, config)
npx = shutil.which('npx.cmd' if __import__('os').name == 'nt' else 'npx')
if not npx: raise SystemExit('Install Node.js first.')
result = subprocess.run([npx,'wrangler','secret','put','UPLOAD_TOKEN'],
    input=config['upload_token']+'\n', text=True, encoding='utf-8', errors='replace', capture_output=True,
    cwd=Path(__file__).resolve().parents[1] / 'server/autolog')
if result.returncode:
    raise SystemExit('Cloudflare secret setup failed; login or review account permissions and retry.')
print('Worker upload token configured; local collector saved and remains disabled.')
