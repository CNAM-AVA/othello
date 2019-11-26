
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <inttypes.h>

#define PORTS "55555"

int main(int argc, char **argv)
{
  int sock_fd, srvc_fd, num_cli=0;
  struct addrinfo s_init, *servinfo, *p;
  struct sockaddr client_addr;
  socklen_t s_taille;
  char msg[50];
  
  uint16_t taille_msg, nb;
  char head[2];

  memset(&s_init, 0, sizeof(s_init));
  s_init.ai_family = AF_INET;

  s_init.ai_socktype = SOCK_STREAM;
	  
  s_init.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, PORTS, &s_init, &servinfo) != 0) {
    fprintf(stderr, "Erreur getaddrinfo\n");
    exit(1);
  }
	  
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("Serveur: socket");
      continue;
    }

    if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sock_fd);
      perror("Serveur: erreur bind");
      continue;
    }
    break;
  }

  if (p == NULL) {
    fprintf(stderr, "Serveur: echec bind\n");
    exit(2);
  }
	  
  freeaddrinfo(servinfo);

  if (listen(sock_fd, 5) == -1) {
    perror("listen");
    exit(1);
  }

  while(1) {
    s_taille = sizeof(client_addr);
    srvc_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &s_taille);

    if (srvc_fd == -1) {
      perror("accept");
      continue;
    }
		  
    num_cli=num_cli+1;
	  
    printf("Serveur: connexion d'un nouveau client\n");

    snprintf(msg, 50, "Bienvenue client :,%u,", htons((uint16_t) num_cli));
    
    taille_msg=htons((uint16_t) strlen(msg));
    memcpy(head, &taille_msg, 2);

    send(srvc_fd, head, 2, 0);
	  
    if(send(srvc_fd, msg, strlen(msg), 0) == -1) {
      perror("send");
      close(srvc_fd);
      exit(0);
    }
 
 
    close(srvc_fd);
  }

  exit(0);
}
