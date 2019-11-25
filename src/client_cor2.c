
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <inttypes.h>

#define SERVEUR "127.0.0.1"
#define PORTS "55555"


int main(int argc, char **argv)
{
  int sockfd, rv;
  struct addrinfo hints, *servinfo, *p;
  
  char head[2], msg[50], *msg2;
  char *token, *saveptr;
  uint16_t taille_msg, nb;
  int numcli, taille;
  
  
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  rv = getaddrinfo(SERVEUR, PORTS, &hints, &servinfo);

  if(rv != 0) 
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
  }
  
  for(p = servinfo; p != NULL; p = p->ai_next) 
  {
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
    if((connect(sockfd, p->ai_addr, p->ai_addrlen)) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    break;
  }

  if(p == NULL) {
    fprintf(stderr, "server: failed to bind\n");
    exit(2);
  }

  freeaddrinfo(servinfo); 
  
  recv(sockfd, head, 2, 0);

  memcpy(&taille_msg, head, 2);
 
  taille= (int) ntohs(taille_msg);
  
  printf("Reception du message suivant de %d octets :\n", taille);

  recv(sockfd, msg, taille*sizeof(char), 0);

  // utilisation fonction strtok_r pour recuperer premiere sous-chaine jusqu'au seperateur ","
  msg2=strtok_r(msg, ",", &saveptr);
    
  // utilisation fonction strtok_r pour recuperer deuxieme sous-chaine jusqu'au seperateur ","
  token=strtok_r(NULL, ",", &saveptr);
  // stockage dans champ structure requete, utilisation %u pour entiers non signes
  sscanf(token, "%u", &nb);

  numcli = (int) ntohs(nb);
  
  printf("%s %d\n", msg2, numcli);
  
  close(sockfd);
  
  exit(0);
}
