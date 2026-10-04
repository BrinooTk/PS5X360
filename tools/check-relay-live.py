"""Send one synthetic setup report to the configured owner's private relay.

No console logs or personal data are included. Requires local collector config.
"""
import hashlib
import json
import urllib.error
import autolog

config = json.loads((autolog.HOME / 'config.json').read_text(encoding='utf-8'))
body = (autolog.MAGIC + 'Game: AutoLog delivery test\nBuild: collector 1.0\n'
        'Synthetic report; no console logs, games, saves or personal data attached.\n').encode()
identifier = hashlib.sha256(body).hexdigest()
try:
    reply = autolog.upload(config, identifier, body)
    if reply.strip() != 'accepted ' + identifier:
        raise SystemExit('Relay did not confirm delivery.')
    print('Discord acknowledged the synthetic report. Report ID: ' + identifier[:16])
except urllib.error.HTTPError as error:
    if error.code == 503:
        raise SystemExit('Relay reachable via verified HTTPS; webhook setup still incomplete (503).')
    detail = error.read(200).decode('utf-8', 'replace')
    if detail.startswith('Delivery pending (receiver '):
        raise SystemExit(detail)
    raise SystemExit('Delivery pending, HTTP ' + str(error.code))
except urllib.error.URLError as error:
    raise SystemExit('Delivery pending: ' + type(error.reason).__name__)
