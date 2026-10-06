// SPDX-License-Identifier: MIT
// Independent userland collector. No kernel patching or emulator memory access.
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#include <signal.h>
#include <poll.h>
#ifdef PS5
#include <sys/sysctl.h>
extern int sceNetResolverCreate(const char*, int, int);
extern int sceNetResolverStartNtoa(int, const char*, struct in_addr*, int, int, int);
extern int sceNetResolverDestroy(int);
extern int sceNetInit(void);
extern int sceNetPoolCreate(const char*,int,int);
extern int sceKernelSendNotificationRequest(int, void*, size_t, int);
#else
#include <netdb.h>
#include <sys/random.h>
#endif
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/sha256.h>
#include "relay_config.h"
#include "roots.h"

#ifndef ROOT
#define ROOT "/data/homebrew/PPSA50011"
#endif
#define LOGS ROOT "/logs"
#define STATE ROOT "/autolog"
#define QUEUE STATE "/outbox"
#define CAP (1024 * 1024)
#define HEAD (32 * 1024)
#define TAIL (192 * 1024)
#include "local_files.h"
static regex_t private_line;
static time_t collect_after;
static volatile sig_atomic_t stopping;
static char scan_status[192];
static char transport_status[192];
static void stop(int n) { (void)n; stopping = 1; }
static int disabled(void) {
  FILE* marker=fopen(ROOT "/no-log-upload","rb");
  if(!marker)marker=fopen(STATE "/no-log-upload","rb");
  if(marker){fclose(marker);return 1;}return 0;
}
static void sha(const void* data, size_t size, char out[65]) {
  unsigned char digest[32];
  mbedtls_sha256((const unsigned char*)data, size, digest, 0);
  for (int i=0;i<32;i++) sprintf(out+2*i, "%02x", digest[i]);
  out[64]=0;
}
static int atomic_file(const char* path, const void* data, size_t size) {
  char temp[1024]; snprintf(temp, sizeof(temp), "%s.tmp", path);
  int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(fd<0) return -1;
  size_t pos=0; while(pos<size) {
    ssize_t n=write(fd,(const char*)data+pos,size-pos);
    if(n<=0) { close(fd); unlink(temp); return -1; } pos+=(size_t)n;
  }
  int ok=fsync(fd); if(close(fd)) ok=-1;
  if(ok || rename(temp,path)) { unlink(temp); return -1; }
  return 0;
}
// A whole sensitive line is dropped, rather than guessing the length of a secret.
static int redact_init(void) {
  return regcomp(&private_line,
    "source:|authorization|webhook|password|token|secret|roms[/\\\\]|game_path|"
    "https?://|ftp://|([0-9]{1,3}\\.){3}[0-9]{1,3}|"
    "([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}|([[:xdigit:]]{0,4}:){2}|"
    "[/\\\\](Users|home)[/\\\\]", REG_EXTENDED|REG_ICASE|REG_NOSUB);
}
static void append(char* report,size_t* used,const char* data,size_t len) {
  if(*used>=CAP-1) return;
  if(len>CAP-1-*used) len=CAP-1-*used;
  memcpy(report+*used,data,len); *used+=len; report[*used]=0;
}
static void redacted_append(char* report,size_t* used,char* text) {
  char* line=text;
  while(*line) {
    char* end=strchr(line,'\n'); if(end) *end=0;
    const char* value=regexec(&private_line,line,0,NULL,0)==0 ? "[private line removed]" : line;
    append(report,used,value,strlen(value)); append(report,used,"\n",1);
    if(!end) break; line=end+1;
  }
}
static int snapshot(const char* path,char* report,size_t* used) {
  int fd=open(path,O_RDONLY|O_NOFOLLOW); if(fd<0) return -1;
  struct stat before,after;
  if(fstat(fd,&before)||!S_ISREG(before.st_mode)) {close(fd);return -1;}
  char* text=calloc(1,HEAD+TAIL+128); if(!text) {close(fd);return -1;}
  size_t want=before.st_size>HEAD+TAIL ? HEAD : (size_t)before.st_size;
  ssize_t n=read(fd,text,want); int ok=n==(ssize_t)want;
  if(ok && before.st_size>HEAD+TAIL) {
    size_t pos=(size_t)n;
    const char* marker="\n[log middle omitted]\n";
    memcpy(text+pos,marker,strlen(marker));pos+=strlen(marker);
    if(lseek(fd,before.st_size-TAIL,SEEK_SET)<0) ok=0;
    n=read(fd,text+pos,TAIL); if(n!=TAIL) ok=0;
  }
  if(fstat(fd,&after)||before.st_size!=after.st_size||before.st_mtime!=after.st_mtime) ok=0;
  close(fd); if(ok) redacted_append(report,used,text); free(text); return ok?0:-1;
}

int mbedtls_hardware_poll(void* context,unsigned char* output,size_t len,size_t* olen) {
  (void)context; *olen=0;
#ifdef PS5
  int mib[2]={CTL_KERN,KERN_ARND};
  while(*olen<len) { size_t n=len-*olen; if(n>256)n=256;
    if(sysctl(mib,2,output+*olen,&n,NULL,0)||!n) {
      FILE* random=fopen("/dev/urandom","rb");
      if(!random)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
      size_t needed=len-*olen,got=fread(output+*olen,1,needed,random);fclose(random);
      if(got!=needed)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;*olen+=got;break;
    }
    *olen+=n;
  }
#else
  while(*olen<len) {ssize_t n=getrandom(output+*olen,len-*olen,0);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; *olen+=(size_t)n;
  }
#endif
  return 0;
}
static int net_send(void* ctx,const unsigned char* data,size_t len) {
  ssize_t n=send(*(int*)ctx,data,len,0); return n>=0?(int)n:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int net_recv(void* ctx,unsigned char* data,size_t len) {
  ssize_t n=recv(*(int*)ctx,data,len,0); return n>=0?(int)n:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int dial(void) {
  struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(443);
#ifdef PS5
  static int dns_pool=-1;
  if(dns_pool<0){int init=sceNetInit();dns_pool=sceNetPoolCreate("PS5X360AutoLog",64*1024,0);
    if(dns_pool<0){snprintf(transport_status,sizeof(transport_status),"Network init %d; pool failed %d",init,dns_pool);return -1;}}
  int resolver=sceNetResolverCreate("PS5X360AutoLog",dns_pool,0); if(resolver<0){snprintf(transport_status,sizeof(transport_status),"DNS create failed: %d",resolver);return -1;}
  int result=sceNetResolverStartNtoa(resolver,RELAY_HOST,&addr.sin_addr,5000000,2,0);
  sceNetResolverDestroy(resolver); if(result<0){snprintf(transport_status,sizeof(transport_status),"DNS resolve failed: %d",result);return -1;}
#else
  struct addrinfo hints={0},*result=NULL; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
  if(getaddrinfo(RELAY_HOST,NULL,&hints,&result))return -1;
  addr.sin_addr=((struct sockaddr_in*)result->ai_addr)->sin_addr;freeaddrinfo(result);
#endif
  int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){snprintf(transport_status,sizeof(transport_status),"Relay socket error %d",errno);return -1;}
  struct timeval timeout={20,0};
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#ifdef PS5
  // The payload environment denies fcntl on sockets. Use the ordinary blocking
  // socket API with the send/receive deadlines above in this separate collector.
  if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))){snprintf(transport_status,sizeof(transport_status),"Relay connect error %d",errno);close(fd);return -1;}
#else
  const int flags=fcntl(fd,F_GETFL,0);
  if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)){snprintf(transport_status,sizeof(transport_status),"Relay fcntl error %d",errno);close(fd);return -1;}
  if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))) {
    if(errno!=EINPROGRESS){snprintf(transport_status,sizeof(transport_status),"Relay connect error %d",errno);close(fd);return -1;}
    struct pollfd ready={fd,POLLOUT,0};int error=0;socklen_t len=sizeof(error);
    int polled=poll(&ready,1,10000);
    if(polled<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)||error){snprintf(transport_status,sizeof(transport_status),"Relay connect poll %d error %d errno %d",polled,error,errno);close(fd);return -1;}
  }
  if(fcntl(fd,F_SETFL,flags)){snprintf(transport_status,sizeof(transport_status),"Relay restore fcntl error %d",errno);close(fd);return -1;}
#endif
  return fd;
}
static int tls_write(mbedtls_ssl_context* ssl,const void* data,size_t len) {
  size_t pos=0; while(pos<len) {int n=mbedtls_ssl_write(ssl,(const unsigned char*)data+pos,len-pos);
    if(n<=0)return -1;pos+=(size_t)n;
  }return 0;
}
static int deliver(const char* payload,size_t size,const char* id) {
  if(disabled()||stopping)return 0;
  snprintf(transport_status,sizeof(transport_status),"Preparing native TLS");
#define TLS_TRY(label,expr) do {int code=(expr);if(code){snprintf(transport_status,sizeof(transport_status),"%s failed: %d",label,code);goto done;}}while(0)
  int ok=0,fd=-1;
  mbedtls_ssl_context ssl; mbedtls_ssl_config conf; mbedtls_x509_crt roots;
  mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context rng;
  mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&conf);mbedtls_x509_crt_init(&roots);
  mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);
  TLS_TRY("Secure entropy",mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,(const unsigned char*)"PS5X360-AutoLog",14));
  TLS_TRY("CA roots",mbedtls_x509_crt_parse(&roots,(const unsigned char*)CA_ROOTS,sizeof(CA_ROOTS)));
  TLS_TRY("TLS configuration",mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT));
  mbedtls_ssl_conf_authmode(&conf,MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf,&roots,NULL);mbedtls_ssl_conf_rng(&conf,mbedtls_ctr_drbg_random,&rng);
  TLS_TRY("TLS setup",mbedtls_ssl_setup(&ssl,&conf));TLS_TRY("TLS hostname",mbedtls_ssl_set_hostname(&ssl,RELAY_HOST));
  snprintf(transport_status,sizeof(transport_status),"Connecting to relay");
  fd=dial();if(fd<0)goto done;mbedtls_ssl_set_bio(&ssl,&fd,net_send,net_recv,NULL);
  TLS_TRY("TLS handshake",mbedtls_ssl_handshake(&ssl));TLS_TRY("TLS certificate",mbedtls_ssl_get_verify_result(&ssl));
  if(disabled()||stopping)goto done;
  char header[1024]; int h=snprintf(header,sizeof(header),
    "POST /v1/reports HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
    "User-Agent: PS5X360-AutoLog-Console/1.0\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: %zu\r\nX-Report-ID: %s\r\nConnection: close\r\n\r\n",RELAY_HOST,CONSOLE_UPLOAD_KEY,size,id);
  if(h<0||h>=(int)sizeof(header))goto done;
  TLS_TRY("HTTPS headers",tls_write(&ssl,header,(size_t)h));TLS_TRY("HTTPS report",tls_write(&ssl,payload,size));
  char response[8192];size_t pos=0;
  while(pos<sizeof(response)-1) {int n=mbedtls_ssl_read(&ssl,(unsigned char*)response+pos,sizeof(response)-1-pos);
    if(n<=0)break;pos+=(size_t)n;
  }response[pos]=0;
  char expected[80];snprintf(expected,sizeof(expected),"accepted %s\n",id);
  int http_code=0;sscanf(response,"HTTP/1.1 %d",&http_code);
  snprintf(transport_status,sizeof(transport_status),"Relay HTTP %d; response bytes %zu",http_code,pos);
  // A complete exact acknowledgement is required, including for chunked responses.
  char* body=strstr(response,"\r\n\r\n");
  if(body && !strncmp(response,"HTTP/1.1 200 ",13)) {
    body+=4;
    if(!strcmp(body,expected)) ok=1;
    else if(strstr(response,"Transfer-Encoding: chunked")||strstr(response,"transfer-encoding: chunked")) {
      char* end=NULL;unsigned long length=strtoul(body,&end,16);
      if(end && !strncmp(end,"\r\n",2) && length==strlen(expected) &&
         !strncmp(end+2,expected,length) && !strcmp(end+2+length,"\r\n0\r\n\r\n"))ok=1;
    }
  }
done:
  if(fd>=0)close(fd);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&conf);
  mbedtls_x509_crt_free(&roots);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);return ok;
#undef TLS_TRY
}
typedef struct {char name[256];char signature[65];time_t stable;} Observation;
static Observation observed[128];static size_t observed_count;
static int name_compare(const void* a,const void* b) {
  const char* x=a;const char* y=b;const char* tx=strstr(x,"-UTC-");const char* ty=strstr(y,"-UTC-");
  if(tx&&ty&&tx-x>=15&&ty-y>=15){int result=strncmp(tx-15,ty-15,19);if(result)return result;}
  return strcmp(x,y);
}
static int queue_count(void) {
  DIR* d=opendir(QUEUE);if(!d)return 0;int count=0;struct dirent* e;
  while((e=readdir(d)))if(strlen(e->d_name)==68&&!strcmp(e->d_name+64,".txt"))count++;
  closedir(d);return count;
}
// Positive lifecycle evidence is required: silence or low FPS never ends a session.
static const char* session_event(const char* path) {
  FILE* f=fopen(path,"rb");if(!f)return NULL;
  if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
  long size=ftell(f);if(size<0){fclose(f);return NULL;}
  long start=size>TAIL?size-TAIL:0;
  if(fseek(f,start,SEEK_SET)){fclose(f);return NULL;}
  char* text=calloc(1,TAIL+1);if(!text){fclose(f);return NULL;}
  fread(text,1,TAIL,f);fclose(f);
  const char* reason=NULL;
  if(strstr(text,"[X360] CRASH signal="))reason="native crash";
  else if(strstr(text,"The guest kernel has crashed")||strstr(text,"KeBugCheck: "))reason="guest kernel crash";
  else if(strstr(text,"Guest thread ")&&strstr(text," LR "))reason="guest crash";
  else if(strstr(text,"ENGINE RESTART")||strstr(text,"Guide: back to the launcher"))reason="return to launcher";
  else if(strstr(text,"ENGINE EXIT "))reason="emulator exit";
  free(text);return reason;
}
static void collect(void) {
  DIR* d=opendir(LOGS);if(!d){snprintf(scan_status,sizeof(scan_status),"scan: opendir error %d; %.145s",errno,local_files_status);return;}
  char (*names)[256]=calloc(512,256);if(!names){closedir(d);return;}
  size_t count=0;struct dirent* e;unsigned total=0,matched=0,stat_failed=0,mode_skip=0,time_skip=0;
  unsigned first_mode=0;long long first_time=0;int stat_errno=0;
  time_t now=time(NULL),latest_session=0;
  while((e=readdir(d))&&count<512) {
    total++;
    if(!strncmp(e->d_name,"Game-",5)&&!strstr(e->d_name,".part")){time_t started=name_time(e->d_name);if(started<=now&&started>latest_session)latest_session=started;}
    size_t len=strlen(e->d_name);
    if(len<10||len>=256||strncmp(e->d_name,"Game-",5)||strstr(e->d_name,"Game-Launcher-")||
       strcmp(e->d_name+len-4,".log")||strstr(e->d_name,".part")||strchr(e->d_name,'/')||strchr(e->d_name,'\\'))continue;
    matched++;
    char path[1024];struct stat st;snprintf(path,sizeof(path),LOGS "/%s",e->d_name);
    if(lstat(path,&st)){stat_failed++;stat_errno=errno;continue;}
    first_mode=st.st_mode;first_time=(long long)st.st_mtime;
    if(!S_ISREG(st.st_mode)){mode_skip++;continue;}
    if((collect_after && name_time(e->d_name)<=collect_after)||now-st.st_mtime>86400||st.st_mtime>now){time_skip++;continue;}
    snprintf(names[count++],256,"%s",e->d_name);
  }closedir(d);qsort(names,count,256,name_compare);
  snprintf(scan_status,sizeof(scan_status),"scan: entries %u matches %u eligible %zu statfail %u errno %d mode-skip %u time-skip %u last-mode %o last-time %lld now %lld",
    total,matched,count,stat_failed,stat_errno,mode_skip,time_skip,first_mode,first_time,(long long)now);
  for(size_t i=count>30?count-30:0;i<count&&!disabled()&&!stopping;i++) {
    if(queue_count()>=20)break;
    const char* reason=NULL;
    char paths[4][1024],signature[1024]={0};size_t pos=0;
    for(int p=0;p<4;p++) {
      if(!p)snprintf(paths[p],1024,LOGS "/%s",names[i]);
      else snprintf(paths[p],1024,LOGS "/%.*s.part%d.log",(int)strlen(names[i])-4,names[i],p);
      struct stat st;
      if(lstat(paths[p],&st)||!S_ISREG(st.st_mode))paths[p][0]=0;
      else pos+=(size_t)snprintf(signature+pos,sizeof(signature)-pos,"%d:%lld:%lld;",p,(long long)st.st_size,(long long)st.st_mtime);
    }
    for(int p=0;p<4;p++)if(paths[p][0]){
      const char* event=session_event(paths[p]);
      if(event&&(!reason||strstr(event,"crash")))reason=event;
    }
    if(!reason&&name_time(names[i])<latest_session)reason="subsequent emulator session observed";
    if(!reason)continue;
    char sig[65],key[65],seen_path[1024],old[65]={0};sha(signature,pos,sig);sha(names[i],strlen(names[i]),key);
    snprintf(seen_path,sizeof(seen_path),STATE "/%s.event-seen",key);
    FILE* f=fopen(seen_path,"rb");if(f){fread(old,1,64,f);fclose(f);if(strlen(old)==64)continue;}
    size_t o=0;while(o<observed_count&&strcmp(observed[o].name,names[i]))o++;
    if(o==observed_count){
      if(o>=128){o=0;for(size_t k=1;k<128;k++)if(observed[k].stable<observed[o].stable)o=k;
        observed[o].signature[0]=0;
      }else observed_count++;
      snprintf(observed[o].name,256,"%s",names[i]);
    }
    if(strcmp(observed[o].signature,sig)){strcpy(observed[o].signature,sig);observed[o].stable=now;continue;}
    if(now-observed[o].stable<30)continue;
    char* report=calloc(1,CAP);if(!report)break;size_t used=0;
    char prefix[320];
    int prefix_len=snprintf(prefix,sizeof(prefix),"PS5X360 diagnostic report v1\nCollector: PS5 AutoLog ELF 1.0.9-preview\nCapture: session event\nReason: %s\n",reason);
    append(report,&used,prefix,(size_t)prefix_len);
    int valid=1;
    for(int p=0;p<4;p++)if(paths[p][0]) {
      char title[320];int n=snprintf(title,sizeof(title),"\n--- %s (segment %d) ---\n",names[i],p);
      redacted_append(report,&used,title);(void)n;
      if(snapshot(paths[p],report,&used))valid=0;
    }
    // Check all segment metadata again after reading, including rotations.
    char after[1024]={0};size_t ap=0;
    for(int p=0;p<4;p++){struct stat st;char check[1024];
      if(!p)snprintf(check,sizeof(check),LOGS "/%s",names[i]);
      else snprintf(check,sizeof(check),LOGS "/%.*s.part%d.log",(int)strlen(names[i])-4,names[i],p);
      if(!lstat(check,&st)&&S_ISREG(st.st_mode))
        ap+=(size_t)snprintf(after+ap,sizeof(after)-ap,"%d:%lld:%lld;",p,(long long)st.st_size,(long long)st.st_mtime);
    }
    if(strcmp(signature,after))valid=0;
    if(valid && !disabled()) {
      char id[65],destination[1024];sha(report,used,id);snprintf(destination,sizeof(destination),QUEUE "/%s.txt",id);
      if(!atomic_file(destination,report,used))atomic_file(seen_path,sig,64);
    }free(report);
  }free(names);
}
static void record_receipt(const char* id,const char* kind) {
  char receipt[256];int size=snprintf(receipt,sizeof(receipt),
    "Discord acknowledged\nKind: %s\nReport: %s\nUTC epoch: %lld\n",kind,id,(long long)time(NULL));
  atomic_file(STATE "/last-delivery.txt",receipt,(size_t)size);
}
static size_t connection_report(char* report,size_t capacity) {
  int size=snprintf(report,capacity,
    "PS5X360 diagnostic report v1\nGame: AutoLog activation check\n"
    "Collector: PS5 AutoLog ELF 1.0.9-preview\n"
    "Capture: activation connectivity check; not a gameplay log\n"
    "No game files or saves included.\n"
    "UTC epoch: %lld\nProcess: %d\nCollection cutoff: %lld\nDirectory status: %.191s\n",
    (long long)time(NULL),(int)getpid(),(long long)collect_after,scan_status);
  return size>0&&(size_t)size<capacity?(size_t)size:0;
}
static int send_one(void) {
  DIR* d=opendir(QUEUE);if(!d)return 0;struct dirent* e;char name[80]={0};
  while((e=readdir(d)))if(strlen(e->d_name)==68&&!strcmp(e->d_name+64,".txt")){strcpy(name,e->d_name);break;}
  closedir(d);if(!*name)return 0;
  for(int i=0;i<64;i++)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return -1;
  char path[1024];snprintf(path,sizeof(path),QUEUE "/%s",name);
  int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;struct stat st;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>=CAP){close(fd);return -1;}
  char* data=malloc((size_t)st.st_size);if(!data){close(fd);return -1;}
  ssize_t n=read(fd,data,(size_t)st.st_size);close(fd);char id[65];sha(data,n>0?(size_t)n:0,id);
  int accepted=n==st.st_size&&!strncmp(id,name,64)&&!disabled()&&deliver(data,(size_t)n,id);
  free(data);if(accepted){record_receipt(id,"game session");unlink(path);printf("AutoLog: delivered %.16s\n",id);return 1;}return -1;
}
#ifndef AUTOLOG_TEST
int main(void) {
  signal(SIGPIPE,SIG_IGN);signal(SIGTERM,stop);signal(SIGINT,stop);
  if(disabled())return 0;
  mkdir(ROOT,0755);mkdir(STATE,0700);mkdir(QUEUE,0700);
  int lock=open(STATE "/collector.lock",O_CREAT|O_RDWR,0600);
  if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;
  const char* notice="PS5X360 AutoLog sends game diagnostics directly to the developer's private Discord via HTTPS.\n"
    "An activation connectivity check and directory diagnostics are also sent.\n"
    "No games or saves are sent. Redaction is best effort.\n"
    "To disable: create /data/homebrew/PPSA50011/no-log-upload and reload or wait 30 seconds.\n"
    "Load this ELF once after each console boot. It does not install a boot service.\n";
  atomic_file(STATE "/NOTICE.txt",notice,strlen(notice));puts(notice);
  char status[768];int status_len=snprintf(status,sizeof(status),"PS5 AutoLog 1.0.9-preview running; pid %d; started %lld UTC\n",(int)getpid(),(long long)time(NULL));
  atomic_file(STATE "/status.txt",status,(size_t)status_len);
#ifdef PS5
  // Notification ABI exposed by the SDK's ordinary userland notify sample.
  unsigned char notification[3120]={0};
  for(int i=0;i<4;i++)notification[0x10+i]=0xff;
  snprintf((char*)notification+45,1024,"PS5X360 AutoLog enabled: diagnostics sent to developer. To disable, create PPSA50011/no-log-upload.");
  sceKernelSendNotificationRequest(0,notification,sizeof(notification),0);
#endif
  FILE* cutoff_file=fopen(STATE "/collect-after.txt","rb");
  long long cutoff_value=0;
  if(cutoff_file){fscanf(cutoff_file,"%lld",&cutoff_value);fclose(cutoff_file);}
  if(cutoff_value<=0){
    cutoff_value=(long long)time(NULL);char text[32];
    int length=snprintf(text,sizeof(text),"%lld",cutoff_value);
    if(atomic_file(STATE "/collect-after.txt",text,(size_t)length))return 3;
  }
  collect_after=(time_t)cutoff_value;
  if(redact_init())return 2;
  unsigned delay=30,elapsed=30;
  int connection_pending=1;char probe[768],probe_id[65];size_t probe_size=0;
  while(!stopping&&!disabled()) {
    collect();
    if(elapsed>=delay){int result,was_probe=connection_pending;
      if(connection_pending){
        if(!probe_size){probe_size=connection_report(probe,sizeof(probe));sha(probe,probe_size,probe_id);}
        if(probe_size&&deliver(probe,probe_size,probe_id)){
          connection_pending=0;record_receipt(probe_id,"activation check");result=1;
        }else result=-1;
      }else result=send_one();
      delay=result<0?(delay<900?delay*2:1800):30;elapsed=0;
      status_len=snprintf(status,sizeof(status),"PS5 AutoLog 1.0.9-preview; pid %d; %s; queued %d; checked %lld UTC\n%s\n%s\n",(int)getpid(),
        result>0?(was_probe?"activation confirmed by Discord":"Discord acknowledged"):result<0?"delivery pending":"waiting for game logs",queue_count(),(long long)time(NULL),scan_status,transport_status);
      atomic_file(STATE "/status.txt",status,(size_t)status_len);
    }
    for(int i=0;i<30&&!stopping&&!disabled();i++)sleep(1);
    elapsed+=30;
  }
  const char* stopped="AutoLog stopped (disabled or process signal).\n";
  atomic_file(STATE "/status.txt",stopped,strlen(stopped));
  regfree(&private_line);close(lock);return 0;
}
#endif
