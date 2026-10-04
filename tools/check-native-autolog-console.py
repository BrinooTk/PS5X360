"""Read only AutoLog's startup and collector status from the development PS5."""
import ftplib
ftp=ftplib.FTP();ftp.connect('192.168.0.19',2121,timeout=10);ftp.login()
try:
    for path in ['/data/homebrew/PPSA50011/autolog-startup.log',
                 '/data/homebrew/PPSA50011/autolog/status.txt']:
        data=bytearray()
        try:
            ftp.retrbinary('RETR '+path,data.extend)
            print(path+'\n'+data.decode(errors='replace')[-3000:])
        except ftplib.error_perm:
            print(path+': absent')
finally:ftp.quit()
