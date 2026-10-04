"""Send only the reviewed ordinary AutoLog collector to the existing ELF loader."""
from pathlib import Path
import socket, ftplib, hashlib
root=Path(__file__).resolve().parents[1]
ftp=ftplib.FTP();ftp.connect('192.168.0.19',2121,timeout=10);ftp.login()
try:
    for path in ['/data/homebrew/PPSA50011/no-log-upload',
                 '/data/homebrew/PPSA50011/autolog/no-log-upload']:
        data=bytearray()
        try:ftp.retrbinary('RETR '+path,data.extend)
        except ftplib.error_perm:continue
        if bytes(data)!=b'debug-restart':
            raise SystemExit('User opt-out marker preserved; collector not sent.')
        try:ftp.delete(path)
        except ftplib.error_reply as error:
            if not str(error).startswith('226'):raise
finally:ftp.quit()
data=(root/'build/autolog-native/PS5X360-AutoLog.elf').read_bytes()
assert data.startswith(b'\x7fELF')
with socket.create_connection(('192.168.0.19',9021),timeout=10) as connection:
    connection.settimeout(5);connection.sendall(data);connection.shutdown(socket.SHUT_WR)
    try:reply=connection.recv(4096)
    except socket.timeout:reply=b''
print('Collector sent; SHA-256 '+hashlib.sha256(data).hexdigest())
print('Execution must be confirmed through collector status and Discord receipt.')
