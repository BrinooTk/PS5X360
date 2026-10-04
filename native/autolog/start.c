// SPDX-License-Identifier: MIT
// Minimal userland ELF startup for an already running SDK-compatible loader.
// Resolves only this program's imports using the loader's dlsym callback.
// No SDK payload CRT, kernel read/write, permission changes or module patches.
#include <stdint.h>
#include <stddef.h>
#include <elf.h>
#include <fcntl.h>
typedef int (*Lookup)(int,const char*,void*);
typedef struct { Lookup lookup; uintptr_t unused[4]; int* result; } LoaderArgs;
extern unsigned char __image_start[] __attribute__((visibility("hidden")));
extern Elf64_Dyn _DYNAMIC[] __attribute__((visibility("hidden")));
extern unsigned char __bss_start[] __attribute__((visibility("hidden")));
extern unsigned char __bss_end[] __attribute__((visibility("hidden")));
extern int __crt_syscall_init(void*);
extern long __crt_syscall(long,...);
extern int main(void);
// Keep the entry point nonzero for loaders that use zero as "no entry".
__asm__(".pushsection .text\n.balign 16\n.space 16, 0x90\n.popsection");
static int userland_lookup(int handle,const char* name,void* output) {
  return (int)__crt_syscall(591,handle,name,output);
}
static void* resolve(Lookup lookup,const int* handles,int count,const char* name) {
  for(int i=0;i<count;i++){void* value=NULL;if(!lookup(handles[i],name,&value)&&value)return value;}
  return NULL;
}
static void diagnostic(Lookup lookup,const int* handles,int count,const char* message) {
  typedef int (*Notify)(int,void*,size_t,int);
  typedef int (*Open)(const char*,int,...);
  typedef long (*Write)(int,const void*,size_t);
  typedef int (*Close)(int);
  Notify notify=(Notify)resolve(lookup,handles,count,"sceKernelSendNotificationRequest");
  unsigned char request[3120];for(size_t i=0;i<sizeof(request);i++)request[i]=0;
  for(int i=0;i<4;i++)request[0x10+i]=0xff; // target_id = -1: text notification
  size_t len=0;while(message[len]&&len<3074){request[45+len]=(unsigned char)message[len];len++;}
  if(notify)notify(0,request,sizeof(request),0);
  Open open_file=(Open)resolve(lookup,handles,count,"open");
  Write write_file=(Write)resolve(lookup,handles,count,"write");
  Close close_file=(Close)resolve(lookup,handles,count,"close");
  if(open_file&&write_file&&close_file){
    int fd=open_file("/data/homebrew/PPSA50011/autolog-startup.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    if(fd>=0){write_file(fd,message,len);write_file(fd,"\n",1);close_file(fd);}
  }
}
__attribute__((visibility("default"))) int autolog_start(LoaderArgs* args) {
  if(!args||!args->lookup)return -1;
  // ELF loaders need not initialize the memory after a segment's file bytes.
  for(volatile unsigned char* b=__bss_start;b<__bss_end;b++)*b=0;
  uintptr_t base=(uintptr_t)__image_start;
  Elf64_Rela* reloc=NULL;size_t bytes=0;Elf64_Sym* symbols=NULL;const char* strings=NULL;
  for(Elf64_Dyn* d=_DYNAMIC;d->d_tag!=DT_NULL;d++) {
    switch(d->d_tag){
      case DT_RELA:reloc=(Elf64_Rela*)(base+d->d_un.d_ptr);break;
      case DT_RELASZ:bytes=d->d_un.d_val;break;
      case DT_SYMTAB:symbols=(Elf64_Sym*)(base+d->d_un.d_ptr);break;
      case DT_STRTAB:strings=(const char*)(base+d->d_un.d_ptr);break;
      case DT_JMPREL:if(d->d_un.d_ptr)return -2;break; // build uses -fno-plt
    }
  }
  if(!reloc||!symbols||!strings)return -3;
  for(size_t i=0;i<bytes/sizeof(*reloc);i++)if(ELF64_R_TYPE(reloc[i].r_info)==R_X86_64_RELATIVE)
    *(uintptr_t*)(base+reloc[i].r_offset)=base+reloc[i].r_addend;
  // SDK's userland syscall adapter accepts both supported loader argument forms.
  // Only dynlib symbol lookup is used. No privilege or kernel-memory API is linked.
  if(__crt_syscall_init(args))return -6;
  LoaderArgs compatible=*args;compatible.lookup=userland_lookup;args=&compatible;
  int handles[4]={0x2001,1,2,-1};int count=3;
  diagnostic(args->lookup,handles,count,"AutoLog diagnostic: ELF entry reached; resolving userland functions.");
  typedef int (*LoadModule)(const char*,size_t,const void*,unsigned,const void*,int*);
  LoadModule load=(LoadModule)resolve(args->lookup,handles,count,"sceKernelLoadStartModule");
  if(load){int error=0;int net=load("/system/common/lib/libSceNet.sprx",0,NULL,0,NULL,&error);
    if(net>=0)handles[count++]=net;
  }
  for(size_t i=0;i<bytes/sizeof(*reloc);i++) {
    unsigned type=ELF64_R_TYPE(reloc[i].r_info);if(type==R_X86_64_RELATIVE||type==R_X86_64_NONE)continue;
    if(type!=R_X86_64_GLOB_DAT&&type!=R_X86_64_64)return -4;
    Elf64_Sym* sym=symbols+ELF64_R_SYM(reloc[i].r_info);
    void* value=sym->st_shndx!=SHN_UNDEF?(void*)(base+sym->st_value):
      resolve(args->lookup,handles,count,strings+sym->st_name);
    if(!value&&ELF64_ST_BIND(sym->st_info)!=STB_WEAK){
      char message[256];const char* prefix="AutoLog startup failed: missing ";size_t n=0;
      while(prefix[n]){message[n]=prefix[n];n++;}
      const char* name=strings+sym->st_name;while(*name&&n<255)message[n++]=*name++;
      message[n]=0;diagnostic(args->lookup,handles,count,message);
      if(args->result)*args->result=-5;return -5;
    }
    *(uintptr_t*)(base+reloc[i].r_offset)=(uintptr_t)value+reloc[i].r_addend;
  }
  diagnostic(args->lookup,handles,count,"AutoLog diagnostic: userland functions resolved; entering collector.");
  int result=main();if(args->result)*args->result=result;return result;
}
