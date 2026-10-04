// SPDX-License-Identifier: MIT
// Directory fallback through the console owner's already enabled FTP service.
// Loopback only; reads only the two fixed AutoLog directories. No privileges changed.
typedef struct {char name[256];long long size;char signature[512];} LocalFile;
static int ftp_reply(int fd,char* out,size_t capacity) {
  size_t pos=0;int code=0,multi=0;
  while(pos+1<capacity){char c;if(recv(fd,&c,1,0)!=1)return -1;out[pos++]=c;out[pos]=0;
    if(c=='\n'){
      char* line=out;for(size_t i=0;i+1<pos;i++)if(out[i]=='\n')line=out+i+1;
      // Find the start of the completed line, not the next empty line.
      size_t begin=pos>1?pos-2:0;while(begin&&out[begin-1]!='\n')begin--;line=out+begin;
      int current=atoi(line);
      if(!code){code=current;multi=line[3]=='-';}
      if(!multi||(current==code&&line[3]==' '))return code;
    }
  }return -1;
}
static int ftp_command(int fd,const char* command,char* reply,size_t cap) {
  size_t len=strlen(command),pos=0;while(pos<len){ssize_t n=send(fd,command+pos,len-pos,0);if(n<=0)return -1;pos+=(size_t)n;}
  return ftp_reply(fd,reply,cap);
}
static int loopback(int port) {
  int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
  struct timeval timeout={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
  struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((unsigned short)port);
  addr.sin_addr.s_addr=htonl(0x7f000001);
  if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))){close(fd);return -1;}return fd;
}
static int local_ftp_list(const char* folder,LocalFile* files,int maximum) {
  if(strcmp(folder,LOGS)&&strcmp(folder,QUEUE))return -1;
  int control=loopback(2121);if(control<0)control=loopback(1337);if(control<0)return -1;
  char reply[2048],command[1024];int count=-1,data=-1;
  if(ftp_reply(control,reply,sizeof(reply))!=220)goto done;
  int code=ftp_command(control,"USER anonymous\r\n",reply,sizeof(reply));
  if(code==331)code=ftp_command(control,"PASS ps5x360@localhost\r\n",reply,sizeof(reply));
  if(code!=230||ftp_command(control,"TYPE I\r\n",reply,sizeof(reply))!=200)goto done;
  if(ftp_command(control,"PASV\r\n",reply,sizeof(reply))!=227)goto done;
  unsigned a,b,c,d,p,q;char* paren=strchr(reply,'(');
  if(!paren||sscanf(paren+1,"%u,%u,%u,%u,%u,%u",&a,&b,&c,&d,&p,&q)!=6||p>255||q>255||p*256+q==0)goto done;
  data=loopback((int)(p*256+q));if(data<0)goto done;
  snprintf(command,sizeof(command),"LIST %s\r\n",folder);
  code=ftp_command(control,command,reply,sizeof(reply));if(code!=150&&code!=125)goto done;
  char* listing=calloc(1,128*1024);if(!listing)goto done;size_t used=0;
  while(used<128*1024-1){ssize_t n=recv(data,listing+used,128*1024-1-used,0);if(n<0){free(listing);goto done;}if(!n)break;used+=(size_t)n;}
  close(data);data=-1;if(ftp_reply(control,reply,sizeof(reply))!=226){free(listing);goto done;}
  count=0;char* line=listing;
  while(*line&&count<maximum){char* end=strchr(line,'\n');if(end)*end=0;
    if(*line=='-'){
      char* name=line;int fields=0;
      while(*name&&fields<8){while(*name==' '||*name=='\t')name++;while(*name&&*name!=' '&&*name!='\t')name++;fields++;}
      while(*name==' '||*name=='\t')name++;size_t len=strlen(name);if(len&&name[len-1]=='\r')name[--len]=0;
      long long size=0;sscanf(line,"%*s %*s %*s %*s %lld",&size);
      if(fields==8&&len>0&&len<256&&size>=0&&!strchr(name,'/')&&!strchr(name,'\\')){
        snprintf(files[count].name,256,"%s",name);files[count].size=size;
        snprintf(files[count].signature,512,"%s",line);count++;
      }
    }
    if(!end)break;line=end+1;
  }free(listing);
done:
  if(data>=0)close(data);close(control);return count;
}
static int local_list(const char* folder,LocalFile* files,int maximum) {
  DIR* dir=opendir(folder);
  if(!dir)return local_ftp_list(folder,files,maximum);
  int count=0;struct dirent* e;
  while((e=readdir(dir))&&count<maximum){char path[1024];struct stat st;
    snprintf(path,sizeof(path),"%s/%s",folder,e->d_name);
    if(lstat(path,&st)||!S_ISREG(st.st_mode))continue;
    snprintf(files[count].name,256,"%s",e->d_name);files[count].size=st.st_size;
    snprintf(files[count].signature,512,"%lld:%lld",(long long)st.st_size,(long long)st.st_mtime);count++;
  }closedir(dir);return count;
}
static time_t name_time(const char* name) {
  const char* utc=strstr(name,"-UTC-");if(!utc||utc-name<15)return 0;
  int y,m,d,h,min,s;if(sscanf(utc-15,"%4d%2d%2d-%2d%2d%2d",&y,&m,&d,&h,&min,&s)!=6)return 0;
  if(y<2000||y>2100||m<1||m>12||d<1||d>31||h>23||min>59||s>59)return 0;
  y-=m<=2;int era=y/400;unsigned yy=(unsigned)(y-era*400);
  unsigned day=(153*(m+(m>2?-3:9))+2)/5+d-1;
  long long days=(long long)era*146097+yy*365+yy/4-yy/100+day-719468;
  return (time_t)(days*86400+h*3600+min*60+s);
}
#ifdef PS5
typedef struct {LocalFile files[512];int count,index;struct dirent entry;} AutoLogDirectory;
static LocalFile directory_cache[512];static int directory_cache_count;
static char directory_cache_path[1024];
static AutoLogDirectory* autolog_opendir(const char* path) {
  AutoLogDirectory* dir=calloc(1,sizeof(*dir));if(!dir)return NULL;
  dir->count=local_list(path,dir->files,512);
  if(dir->count<0){free(dir);errno=EIO;return NULL;}
  memcpy(directory_cache,dir->files,(size_t)dir->count*sizeof(LocalFile));directory_cache_count=dir->count;
  snprintf(directory_cache_path,sizeof(directory_cache_path),"%s",path);return dir;
}
static struct dirent* autolog_readdir(AutoLogDirectory* dir) {
  if(dir->index>=dir->count)return NULL;
  memset(&dir->entry,0,sizeof(dir->entry));snprintf(dir->entry.d_name,sizeof(dir->entry.d_name),"%s",dir->files[dir->index++].name);
  return &dir->entry;
}
static int autolog_closedir(AutoLogDirectory* dir){free(dir);return 0;}
static int autolog_lstat(const char* path,struct stat* st) {
  if(!lstat(path,st))return 0;
  const char* slash=strrchr(path,'/');if(!slash)return -1;
  char folder[1024];size_t len=(size_t)(slash-path);if(len>=sizeof(folder))return -1;
  memcpy(folder,path,len);folder[len]=0;
  if(strcmp(folder,LOGS)&&strcmp(folder,QUEUE)){errno=ENOENT;return -1;}
  if(strcmp(directory_cache_path,folder)){
    directory_cache_count=local_list(folder,directory_cache,512);
    snprintf(directory_cache_path,sizeof(directory_cache_path),"%s",folder);
  }
  for(int i=0;i<directory_cache_count;i++)if(!strcmp(slash+1,directory_cache[i].name)){
    memset(st,0,sizeof(*st));st->st_mode=S_IFREG|0600;st->st_size=(off_t)directory_cache[i].size;
    st->st_mtime=name_time(slash+1);return 0;
  }errno=ENOENT;return -1;
}
static int autolog_fstat(int fd,struct stat* st) {
  if(!fstat(fd,st))return 0;
  off_t current=lseek(fd,0,SEEK_CUR),size=lseek(fd,0,SEEK_END);
  if(current<0||size<0||lseek(fd,current,SEEK_SET)<0)return -1;
  memset(st,0,sizeof(*st));st->st_mode=S_IFREG|0600;st->st_size=size;return 0;
}
#define DIR AutoLogDirectory
#define opendir autolog_opendir
#define readdir autolog_readdir
#define closedir autolog_closedir
#define lstat autolog_lstat
#define fstat autolog_fstat
#endif
