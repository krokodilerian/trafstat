#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <mysql/mysql.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#define TSPORT 8789
#define TSIP "0.0.0.0"

#define HM_LOGFILE "LOGDIR/dwllog"

#define HM_WRITETHREADS 8

#define SECRET "SOMESECRET"

#define EVENT_DWLSTART 1
#define EVENT_DWLSTOP 2
#define EVENT_DWLFINISH 3

struct _eventmsg {
  time_t tm;
  int unsigned jobid;
  int unsigned premium;
  size_t bytessent;
  int unsigned seconds;
  int unsigned userid;
  int unsigned tcpi_total_retrans;
  int unsigned tcpi_snd_mss;
  char ip[256];
  char sign[SHA_DIGEST_LENGTH];
};

struct _peventmsg {
  struct _peventmsg *next;
  char ip[16];
  struct _eventmsg msg;
};

static volatile int rotatelog=0;

void daemonize();
void *receive_thread(void *ptr);
void *write_thread(void *ptr);
void sighup_hdl(int signal);

FILE *hfl_log;

int main(){
  daemonize();

  hfl_log=fopen(HM_LOGFILE, "a");
  setlinebuf(hfl_log);

  signal(SIGHUP,sighup_hdl);

  receive_thread(NULL);

  return 0;
}

void daemonize(){
  int devnullfd = -1;
  umask(~0700);
  devnullfd = open("/dev/null", 0);
  dup2(devnullfd, STDIN_FILENO);
  dup2(devnullfd, STDOUT_FILENO);
  close(devnullfd);
  switch(fork()) {
    case -1:
      perror("fork");
      exit(1);
      break;
    case 0:
      break;
    default:
      exit(0);
      break;
  }
}

void *receive_thread(void *ptr){
  int udpsock, i;
  struct _peventmsg *pmsg;
  struct sockaddr_in serv;
  socklen_t servlen;
  ssize_t len;
  time_t tm;
  char sign1[SHA_DIGEST_LENGTH], sign2[SHA_DIGEST_LENGTH];
  udpsock=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpsock==-1)
    return NULL;
  i=32*1024*1024;
  setsockopt(udpsock, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&i, sizeof(i));
  memset(&serv, 0, sizeof(serv));
  serv.sin_family = AF_INET;
  serv.sin_port = htons(TSPORT);
  serv.sin_addr.s_addr = inet_addr(TSIP);
  if (bind(udpsock, (struct sockaddr *)&serv, sizeof(struct sockaddr_in))){
    close(udpsock);
    return NULL;
  }
  while (1){
    if (rotatelog){
      rotatelog=0;
      //fflush(hfl_log); fclose() should be enough
      fclose(hfl_log);
      hfl_log=fopen(HM_LOGFILE, "a");
      //do we really tail -f that much on this log that we need it line buffered? It requires 1 write per incoming packet
      setlinebuf(hfl_log);
    }
    pmsg=(struct _peventmsg *)malloc(sizeof(struct _peventmsg));
    servlen=sizeof(serv);
    len=recvfrom(udpsock, &pmsg->msg, sizeof(pmsg->msg), 0, (struct sockaddr *)&serv, &servlen);
    if (len!=sizeof(pmsg->msg)){
      free(pmsg);
      continue;
    }
    time(&tm);
    if (pmsg->msg.tm+20<tm || pmsg->msg.tm-20>tm){
      free(pmsg);
      continue;
    }
    memcpy(sign1, pmsg->msg.sign, sizeof(sign1));
    memset(pmsg->msg.sign, 0, sizeof(pmsg->msg.sign));
    strcpy(pmsg->msg.sign, SECRET);
    SHA1((unsigned char *)&pmsg->msg, sizeof(pmsg->msg), (unsigned char *)sign2);
    if (memcmp(sign1, sign2, sizeof(sign1))){
      free(pmsg);
      continue;
    }
    strcpy(pmsg->ip, inet_ntoa(serv.sin_addr));
    fprintf(hfl_log, "%ld %s %lu %u %u %u %u %s %u %u\n", pmsg->msg.tm, pmsg->msg.ip, pmsg->msg.bytessent, pmsg->msg.seconds, pmsg->msg.premium, pmsg->msg.tcpi_total_retrans, pmsg->msg.tcpi_snd_mss, pmsg->ip, pmsg->msg.userid, pmsg->msg.jobid);

    free(pmsg);
  }
}

void sighup_hdl(int signal)
{
  rotatelog=1;
}
