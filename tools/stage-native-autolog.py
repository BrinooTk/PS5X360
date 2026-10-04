"""Stage the reviewed ordinary AutoLog ELF in etaHEN's payload menu, without running it."""
from pathlib import Path
import ftplib, hashlib
root=Path(__file__).resolve().parents[1]
source=root/'build/autolog-native/PS5X360-AutoLog.elf'
data=source.read_bytes()
assert data.startswith(b'\x7fELF')
target='/data/etaHEN/payloads/PS5X360-AutoLog-v1.0.5-preview.elf'
ftp=ftplib.FTP();ftp.connect('192.168.0.19',2121,timeout=15);ftp.login()
try:
    current=bytearray()
    try:ftp.retrbinary('RETR '+target,current.extend)
    except ftplib.error_perm:pass
    if current and bytes(current)!=data:
        raise SystemExit('Different existing ELF preserved; stage under a new versioned name.')
    if not current:
        with source.open('rb') as stream:ftp.storbinary('STOR '+target+'.new',stream)
        actual=bytearray();ftp.retrbinary('RETR '+target+'.new',actual.extend)
        if bytes(actual)!=data:raise SystemExit('Console copy verification failed; final ELF not published.')
        ftp.rename(target+'.new',target)
    print('Staged, not started: '+target)
    print('Console bytes verified: SHA-256 '+hashlib.sha256(data).hexdigest())
finally:ftp.quit()
