#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <inttypes.h>

#include <gtk/gtk.h>

#define MAXDATASIZE 256

/* Variables globales */
int damier[8][8]; // tableau associe au damier
int couleur;	  // 0 : pour noir, 1 : pour blanc
typedef struct Direction Direction;
struct Direction
{
	int dirX;
	int dirY;
};
Direction BAS = {0, 1};
Direction HAUT = {0, -1};
Direction DROITE = {1, 0};
Direction GAUCHE = {-1, 0};
Direction BAS_DROITE = {1, 1};
Direction BAS_GAUCHE = {-1, 1};
Direction HAUT_DROITE = {1, -1};
Direction HAUT_GAUCHE = {-1, -1};

int coord_jouables[100][2];
int taille_coord = 0;

int pions_captures[100][2];
int nb_pions_captures = 0;

int scoreJ1 = 2;
int scoreJ2 = 2;

int port; // numero port passe a l'appel

char *addr_j2, *port_j2; // Info sur adversaire

pthread_t thr_id; // Id du thread fils gerant connexion socket

int rv, taille = 0;
int canPlay; // Variable d'autorisation de jouer
uint16_t taille_message;
int sockfd, newsockfd = -1;  // descripteurs de socket
int addr_size;				 // taille adresse
struct sockaddr their_addr; // structure pour stocker adresse adversaire
struct addrinfo s_init, *servinfo, *p, hints;
socklen_t s_taille;
char head[2], msg[50], *incomming_line, *incomming_col;
char* saveptr = msg;

fd_set master, read_fds, write_fds; // ensembles de socket pour toutes les sockets actives avec select
int fdmax;							// utilise pour select

/* Variables globales associées à l'interface graphique */
GtkBuilder *p_builder = NULL;
GError *p_err = NULL;

// Structure qui représente un pion
typedef struct {
	int col;
	int lig;
} pion;

// Entetes des fonctions

/* Calcul le board après un coup et change les couleurs si besoin */
void compute_board(int col, int lig);

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j);

/* Fonction permettant changer nom joueur blanc dans cadre Score */
void set_label_J1(char *texte);

/* Fonction permettant de changer nom joueur noir dans cadre Score */
void set_label_J2(char *texte);

/* Fonction permettant de changer score joueur blanc dans cadre Score */
void set_score_J1(int score);

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J1(void);

/* Fonction permettant de changer score joueur noir dans cadre Score */
void set_score_J2(int score);

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J2(void);

/* Fonction transformant coordonnees du damier graphique en indexes pour matrice du damier */
void coord_to_indexes(const gchar *coord, int *col, int *lig);

/* Fonction appelee lors du clique sur une case du damier */
static void coup_joueur(GtkWidget *p_case);

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *lecture_addr_serveur(void);

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *lecture_port_serveur(void);

/* Fonction retournant texte du champs login de l'interface graphique */
char *lecture_login(void);

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *lecture_addr_adversaire(void);

/* Fonction retournant texte du champs port du cadre Joueurs de l'interface graphique */
char *lecture_port_adversaire(void);

/* Fonction affichant boite de dialogue si partie gagnee */
void affiche_fenetre_gagne(void);

/* Fonction affichant boite de dialogue si partie perdue */
void affiche_fenetre_perdu(void);

/* Fonction appelee lors du clique du bouton Se connecter */
static void clique_connect_serveur(GtkWidget *b);

/* Fonction desactivant bouton demarrer partie */
void disable_button_start(void);

/* Fonction appelee lors du clique du bouton Demarrer partie */
static void clique_connect_adversaire(GtkWidget *b);

/* Fonction desactivant les cases du damier */
void gele_damier(void);

/* Fonction activant les cases du damier */
void degele_damier(void);

/* Fonction permettant d'initialiser le plateau de jeu */
void init_interface_jeu(void);

/* Fonction reinitialisant la liste des joueurs sur l'interface graphique */
void reset_liste_joueurs(void);

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
void affich_joueur(char *login, char *adresse, char *port);

int dans_le_damier(int col, int lig);

void verifier_direction(Direction dir, int col, int lig);

void get_coord_jouables();

void capturer_direction(Direction dir, int col, int lig, int coul);

void capture_pions(int* coordClic, int coul);

int dans_le_damier(int col, int lig)
{
	if(col >= 0 && lig >= 0 && col <= 7 && lig <= 7)
		return 1;
	else
		return 0;
}

void verifier_direction(Direction dir, int col, int lig)
{
	// On déduit la couleur de l'adversaire d'après celle du joueur actuel
	int adversaire = (couleur - 1) * (couleur - 1);
	// variable permettant d'agrandir la ligne de vérification
	int dist = 2;
	// On s'assure que les coordonnées sont bien sur le damier
	if(dans_le_damier(col+dir.dirX, lig+dir.dirY))
	{
		// Si l'on tombe sur on pion adverse dans cette direction on peu commencer à vérifier
		if(damier[col+dir.dirX][lig+dir.dirY] == adversaire){
			if(dans_le_damier(col+dir.dirX*dist, lig+dir.dirY*dist)){
				// On continue tant que les pions reste ceux de l'adversaire
				while(damier[col+dir.dirX*dist][lig+dir.dirY*dist] == adversaire){
					dist++;
					if(!dans_le_damier(col+dir.dirX*dist, lig+dir.dirY*dist))
						return;
				}
			}
			// Si la boucle s'arrete sur une case vide la coordonnées est jouable
			if(damier[col+dir.dirX*dist][lig+dir.dirY*dist] == -1){
				// printf("dir: [%d,%d]\n", col+dir.dirX*dist, lig+dir.dirY*dist);
				coord_jouables[taille_coord][0] = col+dir.dirX*dist;
				coord_jouables[taille_coord][1] = lig+dir.dirY*dist;
				taille_coord++;
			}
		}
	}
}

void get_coord_jouables()
{
	taille_coord = 0;

	// On parcours tout le damier
	for(int i=0; i<8; i++)
	{
		for(int j=0; j<8; j++)
		{
			// Si on tombe sur un pion qui appartient au joueur
			// On verifie les coordonnées jouables (permettant une capture)
			// dans toutes les directions
			if(damier[j][i] == couleur)
			{
				// HAUT
				verifier_direction(HAUT, j, i);
				// BAS
				verifier_direction(BAS, j, i);
				// DROITE
				verifier_direction(DROITE, j, i);
				// GAUCHE
				verifier_direction(GAUCHE, j, i);
				// HAUT DROITE
				verifier_direction(HAUT_DROITE, j, i);
				// HAUT GAUCHE
				verifier_direction(HAUT_GAUCHE, j, i);
				// BAS DROITE
				verifier_direction(BAS_DROITE, j, i);
				// BAS GAUCHE
				verifier_direction(BAS_GAUCHE, j, i);
			}
		}
	}
}

// Cette fonction reprend beaucoup de mécanismes de la fonction get_coord_jouables()
void capturer_direction(Direction dir, int col, int lig, int coul)
{
	int adversaire = (coul - 1) * (coul - 1);
	int dist = 1;
	nb_pions_captures = 0;

	if(dans_le_damier(col+dir.dirX, lig+dir.dirY))
	{
		if(damier[col+dir.dirX][lig+dir.dirY] == adversaire)
		{
			while(damier[col+dir.dirX*dist][lig+dir.dirY*dist] == adversaire)
			{
				pions_captures[nb_pions_captures][0] = col+dir.dirX*dist;
				pions_captures[nb_pions_captures][1] = lig+dir.dirY*dist;
				nb_pions_captures++;
				dist++;
				if(!dans_le_damier(col+dir.dirX*dist, lig+dir.dirY*dist))
					return;
			}
			if(damier[col+dir.dirX*dist][lig+dir.dirY*dist] == coul)
			{
				for(int k=0; k<nb_pions_captures; k++)
				{
					damier[pions_captures[k][0]][pions_captures[k][1]] = coul;
					change_img_case(pions_captures[k][0], pions_captures[k][1], coul);

					if(couleur == coul){
						scoreJ1++;
						scoreJ2--;
					}
					else{
						scoreJ2++;
						scoreJ1--;
					}
				}
			}
		}
	}
	
}

void capture_pions(int* coordClic, int coul){
	int col = coordClic[0];
	int lig = coordClic[1];

	// Check dans toutes les directions
	// HAUT
	capturer_direction(HAUT, col, lig, coul);

	// BAS
	capturer_direction(BAS, col, lig, coul);

	// DROITE
	capturer_direction(DROITE, col, lig, coul);

	// GAUCHE
	capturer_direction(GAUCHE, col, lig, coul);

	// HAUT DROITE
	capturer_direction(HAUT_DROITE, col, lig, coul);

	// HAUT GAUCHE
	capturer_direction(HAUT_GAUCHE, col, lig, coul);

	// BAS DROITE
	capturer_direction(BAS_DROITE, col, lig, coul);

	// BAS GAUCHE
	capturer_direction(BAS_GAUCHE, col, lig, coul);
}

/* Fonction transforme coordonnees du damier graphique en indexes pour matrice du damier */
void coord_to_indexes(const gchar *coord, int *col, int *lig)
{
	char *c;

	c = malloc(3 * sizeof(char));

	c = strncpy(c, coord, 1);
	c[1] = '\0';

	if (strcmp(c, "A") == 0)
	{
		*col = 0;
	}
	if (strcmp(c, "B") == 0)
	{
		*col = 1;
	}
	if (strcmp(c, "C") == 0)
	{
		*col = 2;
	}
	if (strcmp(c, "D") == 0)
	{
		*col = 3;
	}
	if (strcmp(c, "E") == 0)
	{
		*col = 4;
	}
	if (strcmp(c, "F") == 0)
	{
		*col = 5;
	}
	if (strcmp(c, "G") == 0)
	{
		*col = 6;
	}
	if (strcmp(c, "H") == 0)
	{
		*col = 7;
	}

	*lig = atoi(coord + 1) - 1;
}

/* Fonction transforme coordonnees du damier graphique en indexes pour matrice du damier */
void indexes_to_coord(int col, int lig, char *coord)
{
	char c;

	if (col == 0)
	{
		c = 'A';
	}
	if (col == 1)
	{
		c = 'B';
	}
	if (col == 2)
	{
		c = 'C';
	}
	if (col == 3)
	{
		c = 'D';
	}
	if (col == 4)
	{
		c = 'E';
	}
	if (col == 5)
	{
		c = 'F';
	}
	if (col == 6)
	{
		c = 'G';
	}
	if (col == 7)
	{
		c = 'H';
	}

	sprintf(coord, "%c%d\0", c, lig + 1);
}

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j)
{
	char *coord;

	coord = malloc(3 * sizeof(char));

	indexes_to_coord(col, lig, coord);

	if (couleur_j)
	{ // image pion blanc
		gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "assets/UI_Glade/case_blanc.png");
	}
	else
	{ // image pion noir
		gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "assets/UI_Glade/case_noir.png");
	}
}

/* Fonction permettant changer nom joueur blanc dans cadre Score */
void set_label_J1(char *texte)
{
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_J1")), texte);
}

/* Fonction permettant de changer nom joueur noir dans cadre Score */
void set_label_J2(char *texte)
{
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_J2")), texte);
}

/* Fonction permettant de changer score joueur blanc dans cadre Score */
void set_score_J1(int score)
{
	char *s;

	s = malloc(5 * sizeof(char));
	sprintf(s, "%d\0", score);

	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_ScoreJ1")), s);
}

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J1(void)
{
	const gchar *c;

	c = gtk_label_get_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_ScoreJ1")));

	return atoi(c);
}

/* Fonction permettant de changer score joueur noir dans cadre Score */
void set_score_J2(int score)
{
	char *s;

	s = malloc(5 * sizeof(char));
	sprintf(s, "%d\0", score);

	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_ScoreJ2")), s);
}

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J2(void)
{
	const gchar *c;

	c = gtk_label_get_text(GTK_LABEL(gtk_builder_get_object(p_builder, "label_ScoreJ2")));

	return atoi(c);
}

/* Fonction appelee lors du clique sur une case du damier */
static void coup_joueur(GtkWidget *p_case)
{

	int col, lig, type_msg, nb_piece, score;
	// char buf[MAXDATASIZE];


	// Traduction coordonnees damier en indexes matrice damier
	coord_to_indexes(gtk_buildable_get_name(GTK_BUILDABLE(gtk_bin_get_child(GTK_BIN(p_case)))), &col, &lig);

	/***** TO DO *****/
	if (damier[col][lig] != -1) return;

	int coup_jouable = 0;
	for(int n=0; n<taille_coord; n++){
		if(coord_jouables[n][0] == col && coord_jouables[n][1] == lig){
			coup_jouable = 1;
		}
	}

	if(coup_jouable != 1) return;

	// printf("Coord to indexs for col: %d, lig: %d: %s\n", col, lig, coordToIndexes);

	change_img_case(col, lig, couleur);
	damier[col][lig] = couleur;
	scoreJ1++;

	int coordClic[2];
	coordClic[0] = col;
	coordClic[1] = lig;
	capture_pions(coordClic, couleur);
	// compute_board(col, lig);

	if (newsockfd > fdmax) {
		fdmax = newsockfd;
	}

	if(couleur == 1){
		set_score_J1(scoreJ1);
		set_score_J2(scoreJ2);
	} else {
		set_score_J1(scoreJ2);
		set_score_J2(scoreJ1);
	}

	// BLoque le jeu tant que l'autre n'a pas joué
	gele_damier();

	// Envoi message à adverssaire
	snprintf(msg, 50, ",%u,%u,", htons((uint16_t)col), htons((uint16_t)lig)); // ushort ok pour envoyer des coordonées (0, 65535);
	taille_message = htons((uint16_t) strlen(msg));
	memcpy(head, &taille_message, 2);

	// Envoi de la taille du message d'abord
	if(send(newsockfd, head, 2, 0) == -1) {
		perror("send");
	}

	// Envoi des coordonnées
	if(send(newsockfd, msg, strlen(msg), 0) == -1) {
		perror("send");
	}

	// fflush(stdout);
}

/* Calcul le board après un coup */
	/*
     * None = -1, Black = 0, White = 1
	 *
	 *      0  1  2  3  4  5  6  7
	 *    ________________________
	 * 0 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 1 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 2 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 3 | -1 -1 -1  1  0 -1 -1 -1
	 * 4 | -1 -1 -1  0  1 -1 -1 -1
	 * 5 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 6 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 7 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 
	 *  
	 */
// void compute_board(int col, int lig) {

// 	pion detecteds[64];
// 	int number_detected = 0;

// 	// BUG: Détecte des pions blancs ?

// 	// Détecte la présence des pions noirs déjà existants sur la colonne
// 	for (int i = 0; i < 8; i++) {
// 		if (lig == i) continue; // skip the new

// 		if (damier[col][i] == 0) {
// 			pion p = { .col = col, .lig = i };
// 			detecteds[number_detected] = p;
// 			number_detected += 1;
// 		}
// 	}

// 	// Détecte la présence des pions noirs déjà existants sur la ligne
// 	for (int i = 0; i < 8; i++) {
// 		if (col == i) continue;

// 		if (damier[i][lig] == 0) {
// 			pion p = { .col = i, .lig = lig };
// 			detecteds[number_detected] = p;
// 			number_detected += 1;
// 		}
// 	}

// 	printf("Pions noir détectés : \n");
// 	for (int i = 0; i < number_detected; i++) {
// 		printf("# {%d,%d}\n", detecteds[i].col, detecteds[i].lig);
// 	}
// 	printf("----------------------\n");

// }

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *lecture_addr_serveur(void)
{
	GtkWidget *entry_addr_srv;

	entry_addr_srv = (GtkWidget *)gtk_builder_get_object(p_builder, "entry_adr");

	return (char *)gtk_entry_get_text(GTK_ENTRY(entry_addr_srv));
}

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *lecture_port_serveur(void)
{
	GtkWidget *entry_port_srv;

	entry_port_srv = (GtkWidget *)gtk_builder_get_object(p_builder, "entry_port");

	return (char *)gtk_entry_get_text(GTK_ENTRY(entry_port_srv));
}

/* Fonction retournant texte du champs login de l'interface graphique */
char *lecture_login(void)
{
	GtkWidget *entry_login;

	entry_login = (GtkWidget *)gtk_builder_get_object(p_builder, "entry_login");

	return (char *)gtk_entry_get_text(GTK_ENTRY(entry_login));
}

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *lecture_addr_adversaire(void)
{
	GtkWidget *entry_addr_j2;

	entry_addr_j2 = (GtkWidget *)gtk_builder_get_object(p_builder, "entry_addr_j2");

	return (char *)gtk_entry_get_text(GTK_ENTRY(entry_addr_j2));
}

/* Fonction retournant texte du champs port du cadre Joueurs de l'interface graphique */
char *lecture_port_adversaire(void)
{
	GtkWidget *entry_port_j2;

	entry_port_j2 = (GtkWidget *)gtk_builder_get_object(p_builder, "entry_port_j2");

	return (char *)gtk_entry_get_text(GTK_ENTRY(entry_port_j2));
}

/* Fonction affichant boite de dialogue si partie gagnee */
void affiche_fenetre_gagne(void)
{
	GtkWidget *dialog;

	GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;

	dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_builder_get_object(p_builder, "window1")), flags, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Fin de la partie.\n\n Vous avez gagné!!!", NULL);
	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
}

/* Fonction affichant boite de dialogue si partie perdue */
void affiche_fenetre_perdu(void)
{
	GtkWidget *dialog;

	GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;

	dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_builder_get_object(p_builder, "window1")), flags, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Fin de la partie.\n\n Vous avez perdu!", NULL);
	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
}

/* Fonction appelee lors du clique du bouton Se connecter */
static void clique_connect_serveur(GtkWidget *b)
{
	/***** TO DO *****/
	printf(
		"Connection to server %s, "
	 	"Port: %s, "
		"Login: %s\n", lecture_addr_adversaire(), lecture_port_serveur(), lecture_login()
	);

	
}

/* Fonction desactivant bouton demarrer partie */
void disable_button_start(void)
{
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "button_start"), FALSE);
}

/* Fonction traitement signal bouton Demarrer partie */
static void clique_connect_adversaire(GtkWidget *b)
{
	if (newsockfd == -1)
	{
		// Deactivation bouton demarrer partie
		gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "button_start"), FALSE);

		// Recuperation  adresse et port adversaire au format chaines caracteres
		addr_j2 = lecture_addr_adversaire();
		port_j2 = lecture_port_adversaire();

		printf("[Port joueur : %d] Adresse j2 lue : %s\n", port, addr_j2);
		printf("[Port joueur : %d] Port j2 lu : %s\n", port, port_j2);

		pthread_kill(thr_id, SIGUSR1);
	}
}

/* Fonction desactivant les cases du damier */
void gele_damier(void)
{
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH1"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH2"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH3"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH4"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH5"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH6"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH7"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG8"), FALSE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH8"), FALSE);
}

/* Fonction activant les cases du damier */
void degele_damier(void)
{
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH1"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH2"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH3"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH4"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH5"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH6"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH7"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxA8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxB8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxC8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxD8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxE8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxF8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxG8"), TRUE);
	gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "eventboxH8"), TRUE);
}

/* Fonction permettant d'initialiser le plateau de jeu */
void init_interface_jeu(void)
{
	// Initilisation du damier (D4=blanc, E4=noir, D5=noir, E5=blanc)
	change_img_case(3, 3, 1);
	change_img_case(4, 3, 0);
	change_img_case(3, 4, 0);
	change_img_case(4, 4, 1);

	// Initialisation des scores et des joueurs
	if (couleur == 1)
	{
		set_label_J1("Vous");
		set_label_J2("Adversaire");
	}
	else
	{
		set_label_J1("Adversaire");
		set_label_J2("Vous");
	}

	set_score_J1(scoreJ1);
	set_score_J2(scoreJ2);

	/***** TO DO *****/
	/*
	 *      0  1  2  3  4  5  6  7
	 *    ________________________
	 * 0 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 1 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 2 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 3 | -1 -1 -1  1  0 -1 -1 -1
	 * 4 | -1 -1 -1  0  1 -1 -1 -1
	 * 5 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 6 | -1 -1 -1 -1 -1 -1 -1 -1
	 * 7 | -1 -1 -1 -1 -1 -1 -1 -1
	 */

	for (int i = 0; i < 8; i++) { // Cols
		for (int j = 0; j < 8; j++) { // Rows
			damier[i][j] = -1;
		}
	}

	// Valeurs initiales
	damier[3][3] = 1;
	damier[4][3] = 0;
	damier[3][4] = 0;
	damier[4][4] = 1;
}

/* Fonction reinitialisant la liste des joueurs sur l'interface graphique */
void reset_liste_joueurs(void)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start);
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &end);

	gtk_text_buffer_delete(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start, &end);
}

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
void affich_joueur(char *login, char *adresse, char *port)
{
	const gchar *joueur;

	joueur = g_strconcat(login, " - ", adresse, " : ", port, "\n", NULL);

	gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), joueur, strlen(joueur));
}

/* Fonction exécutée par le thread gérant les communications à travers la socket */
static void *f_com_socket(void *p_arg)
{
	int i, nbytes, col, lig;

	// char buf[MAXDATASIZE], *tmp, *p_parse;
	int len, bytes_sent, t_msg_recu;

	sigset_t signal_mask;
	int fd_signal;

	uint16_t type_msg, col_j2;
	uint16_t ucol, ulig;

	/* Association descripteur au signal SIGUSR1 */
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGUSR1);

	if (sigprocmask(SIG_BLOCK, &signal_mask, NULL) == -1)
	{
		printf("[Pourt joueur %d] Erreur sigprocmask\n", port);
		return 0;
	}

	fd_signal = signalfd(-1, &signal_mask, 0);

	if (fd_signal == -1)
	{
		printf("[port joueur %d] Erreur signalfd\n", port);
		return 0;
	}

	/* Ajout descripteur du signal dans ensemble de descripteur utilisé avec fonction select */
	FD_SET(fd_signal, &master);

	if (fd_signal > fdmax)
	{
		fdmax = fd_signal;
	}

	while (1)
	{
		read_fds = master; // copie des ensembles

		if (select(fdmax + 1, &read_fds, &write_fds, NULL, NULL) == -1)
		{
			perror("Problème avec select");
			exit(4);
		}

		// printf("[Port joueur %d] Entree dans boucle for\n", port);
		for (i = 0; i <= fdmax; i++)
		{
			// printf("[Port joueur %d] newsockfd=%d, iteration %d boucle for\n", port, newsockfd, i);
			
			if (FD_ISSET(i, &read_fds))
			{
				if (i == fd_signal)
				{
					/* Cas où de l'envoie du signal par l'interface graphique pour connexion au joueur adverse */

					/***** TO DO *****/
					memset(&hints, 0, sizeof(hints));
					hints.ai_family = AF_UNSPEC;
					hints.ai_socktype = SOCK_STREAM;
					
					// Ferme socket d'écoute
					close(fd_signal);
					close(sockfd);

					// Remove des fd du fd master
					FD_CLR(fd_signal, &master);
					FD_CLR(sockfd, &master);
					
					// infos joueur adverse avec params de servinfo
					rv = getaddrinfo(addr_j2, port_j2, &hints, &servinfo);

					if(rv != 0) 
					{
						fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
						exit(1);
					}
					
					for(p = servinfo; p != NULL; p = p->ai_next) 
					{
						// Nouveau socket
						if((newsockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
							perror("client: socket");
							continue;
						}

						// Connexion au socket
						if((connect(newsockfd, p->ai_addr, p->ai_addrlen)) == -1) {
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

					// Mise du fd du nouveau socket dans master
					FD_SET(newsockfd, &master);
                    if(newsockfd>fdmax)
                    {
                        fdmax=newsockfd;
                    }

					// Initialisation du client en couleur noir
					couleur = 0;
					// Initialisation de l'interface
					init_interface_jeu();

					printf("Connecté à %s:%s\n", addr_j2, port_j2);

					get_coord_jouables();
				}
				
				if(i == sockfd)
				{ 
				// Acceptation connexion adversaire
				
					/***** TO DO *****/
					s_taille = sizeof(their_addr);
    				newsockfd = accept(sockfd, (struct sockaddr *) &their_addr, &s_taille);

					if (newsockfd == -1) {
						perror("accept");
						continue;
					}

					FD_SET(newsockfd, &master);
                    if(newsockfd>fdmax)
                    {
                        fdmax=newsockfd;
                    }

					// Initialisation du serveur en couleur blanc
					couleur = 1;
					gele_damier();
					// Initialisation de l'interface
					init_interface_jeu();

					printf("Connexion d'un client\n");
					gtk_widget_set_sensitive((GtkWidget *)gtk_builder_get_object(p_builder, "button_start"), FALSE);
					
					get_coord_jouables();
				}

				
			}
			else
			{ // Reception et traitement des messages du joueur adverse

				/***** TO DO *****/

				// Structure d'un message: head(taille du message), "col,lig"
				// Récepetion du head
				recv(newsockfd, head, 2, 0);
				// allocation memoire dela taille conetnue dans le head
				memcpy(&taille_message, head, 2);
				taille = (int) ntohs(taille_message);

				if (taille == 0) continue; // Message de 0 octets lors de l'initialisation

				// Réception des coords
				recv(newsockfd, msg, taille*sizeof(char), 0);

				// Récup lig et col
				incomming_col = strtok_r(msg, ",", &saveptr);
				incomming_line = strtok_r(NULL, ",", &saveptr);

				// Update interface
				int tmp_col, tmp_lig;
				sscanf(incomming_col, "%u", &tmp_col);
				sscanf(incomming_line, "%u", &tmp_lig);

				// Conversion du network vers int
				int col, lig;
				col = (int) ntohs(tmp_col);
				lig = (int) ntohs(tmp_lig);

				int inv_couleur = couleur == 1 ? 0 : 1;
				change_img_case(col, lig, inv_couleur);
				damier[col][lig] = inv_couleur;

				int coord[2] = {col, lig};
				capture_pions(coord, inv_couleur);
				scoreJ2++;

				if(couleur == 1){
					set_score_J1(scoreJ1);
					set_score_J2(scoreJ2);
				} else {
					set_score_J1(scoreJ2);
					set_score_J2(scoreJ1);
				}

				get_coord_jouables();

				degele_damier();

				if (newsockfd > fdmax) {
					fdmax = newsockfd;
				}
			}
		}
	}

	return NULL;
}

int main(int argc, char **argv)
{
	int i, j, ret;

	if (argc != 2)
	{
		printf("\nPrototype : ./othello num_port\n\n");

		exit(1);
	}

	/* Initialisation de GTK+ */
	gtk_init(&argc, &argv);

	/* Creation d'un nouveau GtkBuilder */
	p_builder = gtk_builder_new();

	if (p_builder != NULL)
	{
		/* Chargement du XML dans p_builder */
		gtk_builder_add_from_file(p_builder, "assets/UI_Glade/Othello.glade", &p_err);

		if (p_err == NULL)
		{
			/* Recuparation d'un pointeur sur la fenetre. */
			GtkWidget *p_win = (GtkWidget *)gtk_builder_get_object(p_builder, "window1");

			/* Gestion evenement clic pour chacune des cases du damier */
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH1"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH2"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH3"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH4"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH5"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH6"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH7"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxA8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxB8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxC8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxD8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxE8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxF8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxG8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "eventboxH8"), "button_press_event", G_CALLBACK(coup_joueur), NULL);

			/* Gestion clic boutons interface */
			g_signal_connect(gtk_builder_get_object(p_builder, "button_connect"), "clicked", G_CALLBACK(clique_connect_serveur), NULL);
			g_signal_connect(gtk_builder_get_object(p_builder, "button_start"), "clicked", G_CALLBACK(clique_connect_adversaire), NULL);

			/* Gestion clic bouton fermeture fenetre */
			g_signal_connect_swapped(G_OBJECT(p_win), "destroy", G_CALLBACK(gtk_main_quit), NULL);

			/* Recuperation numero port donne en parametre */
			port = atoi(argv[1]);

			/* Initialisation du damier de jeu */
			for (i = 0; i < 8; i++)
			{
				for (j = 0; j < 8; j++)
				{
					damier[i][j] = -1;
				}
			}

			/***** TO DO *****/

			// Initialisation socket et autres objets, et création thread pour communications avec joueur adverse
			memset(&s_init, 0, sizeof(s_init));
			s_init.ai_family = AF_INET;
			s_init.ai_socktype = SOCK_STREAM;
			s_init.ai_flags = AI_PASSIVE;

			if (getaddrinfo(NULL, argv[1], &s_init, &servinfo) != 0) {
    			  fprintf(stderr, "Erreur getaddrinfo\n");
    			  exit(1);
  			}

			for(p = servinfo; p != NULL; p = p->ai_next) {
    			  if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      				perror("Serveur: socket");
      				continue;
 		   	  }

			  if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
					close(sockfd);
					perror("Serveur: erreur bind");
					continue;
			  }
				break;
			}

			FD_ZERO(&master);
			FD_SET(sockfd, &master);

			if (sockfd > fdmax) {
				fdmax = sockfd;
			}

			if (p == NULL) {
    			fprintf(stderr, "Serveur: echec bind\n");
    			exit(2);
  			}

			freeaddrinfo(servinfo);

			if (listen(sockfd, 5) == -1) {
   			 	perror("listen");
    			        exit(1);
  			}

			// pthread_t thread1;

			// Création du thread de communication, affectation de la fonction f_com_socket
			if(pthread_create(&thr_id, NULL, f_com_socket, NULL) == -1) {
				perror("pthread_create");
				return EXIT_FAILURE;
			}

			// Affichage interface hraphique
			gtk_widget_show_all(p_win);
			gtk_main();
		}
		else
		{
			/* Affichage du message d'erreur de GTK+ */
			g_error("%s", p_err->message);
			g_error_free(p_err);
		}
	}

	return EXIT_SUCCESS;
}
