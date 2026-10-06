// SPDX-License-Identifier: MIT
#define AUTOLOG_TEST
#define ROOT "/tmp/ps5x360-autolog-test"
#include "collector.c"
#include <assert.h>
int main(int argc,char** argv) {
  assert(!redact_init());
  char probe[768];strcpy(scan_status,"scan: opendir error 5; FTP 1337 connect errno 61");
  size_t probe_size=connection_report(probe,sizeof(probe));
  assert(probe_size>0&&strstr(probe,"activation connectivity check")&&strstr(probe,"FTP 1337"));
  assert(!strncmp(probe,"PS5X360 diagnostic report v1\n",28));
  char report[4096]={0};size_t used=0;
  char input[]="Game: Sonic\nFPS: 60\nSource: /games/private\nAuthorization: xyz\nIP: 192.168.0.19\nIPv6: 2001:db8::1\nMAC: aa:bb:cc:dd:ee:ff\nhttps://private.example\n";
  redacted_append(report,&used,input);
  assert(strstr(report,"Game: Sonic")&&strstr(report,"FPS: 60"));
  assert(!strstr(report,"xyz")&&!strstr(report,"192.168")&&!strstr(report,"db8")&&!strstr(report,"aa:bb"));
  assert(!strstr(report,"/games")&&!strstr(report,"https://"));
  char id[65];sha("abc",3,id);
  assert(!strcmp(id,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  mkdir(ROOT,0700);mkdir(LOGS,0700);mkdir(STATE,0700);mkdir(QUEUE,0700);
  assert(!atomic_file(ROOT "/test", "abc",3));
  assert(!disabled());assert(!atomic_file(ROOT "/no-log-upload","",0));assert(disabled());
  assert(!deliver("test",4,id));unlink(ROOT "/no-log-upload");
  // Queue acceptance and snapshot association, without uploading real game data.
  const char* game="PS5X360 test-build\nGame: AutoLog native self-test\nSource: /private/game\nFPS: 60\n";
  assert(!atomic_file(LOGS "/Game-Test-12345678-20261004-010101-UTC-0.log",game,strlen(game)));
  collect_after=name_time("Game-Test-12345678-20261004-010101-UTC-0.log");
  collect();assert(observed_count==0);assert(queue_count()==0);
  collect_after=0;
  collect();assert(observed_count==0);assert(queue_count()==0); // quiet active game must not upload
  char ended[256];snprintf(ended,sizeof(ended),"%s\ni> ENGINE EXIT 0\n",game);
  assert(!atomic_file(LOGS "/Game-Test-12345678-20261004-010101-UTC-0.log",ended,strlen(ended)));
  assert(!strcmp(session_event(LOGS "/Game-Test-12345678-20261004-010101-UTC-0.log"),"emulator exit"));
  collect();assert(observed_count==1);observed[0].stable-=31;collect();assert(queue_count()==1);
  collect();assert(queue_count()==1); // unchanged files aren't queued twice
  assert(!atomic_file(ROOT "/crash-test","[X360] CRASH signal=b\n",strlen("[X360] CRASH signal=b\n")));
  assert(!strcmp(session_event(ROOT "/crash-test"),"native crash"));
  const char* guide="Guide: back to the launcher\n";
  assert(!atomic_file(ROOT "/guide-test",guide,strlen(guide)));
  assert(!strcmp(session_event(ROOT "/guide-test"),"return to launcher"));
  const char* bugcheck="KeBugCheck: *** STOP: 0x00000000\nGuest thread 123 LR 82001000 CTR 00000000\n";
  assert(!atomic_file(ROOT "/bugcheck-test",bugcheck,strlen(bugcheck)));
  assert(!strcmp(session_event(ROOT "/bugcheck-test"),"guest kernel crash"));
  const char* extra="i> ENGINE EXIT 0\nadditional final diagnostics\n";
  assert(!atomic_file(LOGS "/Game-Test-12345678-20261004-010101-UTC-0.log",extra,strlen(extra)));
  collect();assert(queue_count()==1); // each ended session is sent once
  assert(!atomic_file(ROOT "/no-log-upload","",0));assert(send_one()<0);assert(queue_count()==1);
  unlink(ROOT "/no-log-upload");
  if(argc>1&&!strcmp(argv[1],"--live")) {
    assert(send_one()==1);assert(queue_count()==0);
    puts("Native TLS transport accepted by owned live relay (HOST, not PS5).");
  }
  regfree(&private_line);puts("Native redaction, digest, opt-out, atomic outbox and deduplication checks passed.");
  return 0;
}
