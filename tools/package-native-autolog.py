"""Package the console ELF with reviewed source, licenses and build inputs."""
from pathlib import Path
import hashlib, json, zipfile, os, subprocess, re
root=Path(__file__).resolve().parents[1]
out=root/'build/autolog-native'
elf=out/'PS5X360-AutoLog.elf'
assert elf.read_bytes().startswith(b'\x7fELF')
names=['LICENSE','native/autolog/README.md','native/autolog/collector.c',
       'native/autolog/local_files.h',
       'native/autolog/start.c','native/autolog/test.c','native/autolog/mbedtls_user_config.h',
       'tools/build-native-autolog.sh','tools/check-native-autolog.sh',
       'tools/prepare-native-autolog.py','tools/package-native-autolog.py']
files={name:root/name for name in names}
files.update({'PS5X360-AutoLog.elf':elf,
              'build/autolog-native/roots.h':out/'roots.h',
              'build/autolog-native/roots.pem':out/'roots.pem',
              'build/autolog-native/relay_config.h':out/'relay_config.h',
              'build/autolog-native/sdk-syscall.c':out/'sdk-syscall.c',
              'build/autolog-native/payload.h':out/'payload.h',
              'dependencies/SDK-COPYING':out/'SDK-COPYING',
              'dependencies/mbedtls-3.6.7.tar.bz2':root/'.deps/autolog/mbedtls-3.6.7.tar.bz2'})
for p in (root/'.deps/autolog/mbedtls-3.6.7').glob('LICENSE*'):
    files['dependencies/'+p.name]=p
# Distribution must contain neither the owner's credential nor the Discord URL.
owner=Path(os.environ['LOCALAPPDATA'])/'PS5X360-AutoLog/config.json'
private_key=json.loads(owner.read_text())['upload_token'].encode() if owner.exists() else b''
for path in files.values():
    data=path.read_bytes()
    assert not private_key or private_key not in data, 'Private owner credential in archive'
    assert not re.search(rb'https://discord.com/api/webhooks/[0-9]{10,}/[A-Za-z0-9_-]{16,}',data), 'Discord webhook in archive'
manifest={'version':'1.0.5-preview','validation':'PS5 firmware 13.60: native collector read actual game logs and received Discord acknowledgement through verified HTTPS; other firmware unverified',
          'sdk_revision':(root/'.deps/references/PS5_Vulkan/.deps/native/ps5-payload-sdk/.ps5-sdk-revision').read_text().strip(),
          'node_ca_version':subprocess.check_output(['node','--version'],text=True).strip(),
          'sha256':{name:hashlib.sha256(p.read_bytes()).hexdigest() for name,p in files.items()}}
destination=root/'dist/PS5X360-AutoLog-ELF-v1.0.5-preview.zip'
with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED) as z:
    for name,path in files.items():z.write(path,'PS5X360-AutoLog-ELF/'+name)
    z.writestr('PS5X360-AutoLog-ELF/manifest.json',json.dumps(manifest,indent=2))
with zipfile.ZipFile(destination) as z:assert z.testzip() is None
digest=hashlib.sha256(destination.read_bytes()).hexdigest()
destination.with_suffix('.zip.sha256').write_text(digest+'  '+destination.name+'\n')
print(destination);print('Archive verified; private credentials excluded. SHA-256 '+digest)
