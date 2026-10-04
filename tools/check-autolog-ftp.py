"""Exercise the actual loopback FTP fallback against two owned mock servers."""
from pathlib import Path
import socket
import threading
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
stop = threading.Event()
deny = threading.Event()

def serve(port, reject):
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', port))
        listener.listen()
        listener.settimeout(.2)
        while not stop.is_set():
            try: connection, _ = listener.accept()
            except socket.timeout: continue
            with connection:
                connection.settimeout(4)
                stream = connection.makefile('rb')
                connection.sendall(b'220 test FTP\r\n')
                passive = None
                try:
                    while line := stream.readline():
                        command = line.decode().strip()
                        verb = command.split(' ', 1)[0]
                        if verb == 'USER': reply = '530 disabled' if reject or deny.is_set() else '331 password'
                        elif verb == 'PASS': reply = '230 ready'
                        elif verb == 'TYPE': reply = '200 binary'
                        elif verb == 'PASV':
                            passive = socket.socket()
                            passive.bind(('127.0.0.1', 0)); passive.listen()
                            passive.settimeout(4)
                            data_port = passive.getsockname()[1]
                            reply = f'227 passive (127,0,0,1,{data_port//256},{data_port%256})'
                        elif verb == 'CWD': reply = '250 directory selected'
                        elif command == 'LIST':
                            connection.sendall(b'150 listing\r\n')
                            data, _ = passive.accept()
                            with data:
                                data.sendall(b'-rw-r--r-- 1 owner group 123 Oct 4 20:00 Game-Midnight Club LA-12345678-20261004-220000-UTC-0.log\r\n')
                            passive.close(); passive = None
                            reply = '226 done'
                        else: reply = '550 absolute LIST unavailable'
                        connection.sendall((reply+'\r\n').encode())
                except (OSError, ValueError): pass
                finally:
                    if passive: passive.close()
                    stream.close()

threads = [threading.Thread(target=serve,args=(2121,True),daemon=True),
           threading.Thread(target=serve,args=(1337,False),daemon=True)]
for thread in threads: thread.start()
try:
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp)/'check.c'
        source.write_text('''#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define LOGS "/data/homebrew/PPSA50011/logs"
#define QUEUE "/data/homebrew/PPSA50011/autolog/outbox"
#include "local_files.h"
int main(int argc,char** argv) {
 LocalFile files[4];int count=local_ftp_list(LOGS,files,4);
 if(argc>1) {
  if(count!=-1||!strstr(local_files_status,"FTP 2121 login code 530")||
     !strstr(local_files_status,"FTP 1337 login code 530")) return 2;
  puts("PASS: both FTP failures retain per-port login diagnostics");return 0;
 }
 if(count!=1||files[0].size!=123||!strstr(files[0].name,"Midnight Club LA")) {
  printf("FAIL: FTP fallback returned %d files\\n",count);return 1;
 }
 puts("PASS: rejected 2121 login falls back to 1337; CWD + LIST accepts names with spaces");
 return 0;
}
''')
        subprocess.run(['clang-18','-I'+str(root/'native/autolog'),str(source),'-o',temp+'/check'],check=True)
        subprocess.run([temp+'/check'],check=True)
        deny.set()
        subprocess.run([temp+'/check','--failure'],check=True)
finally:
    stop.set()
    for thread in threads: thread.join(1)
