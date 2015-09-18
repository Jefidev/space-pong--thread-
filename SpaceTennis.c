#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "GrilleSDL.h"
#include "Ressources.h"
#include <sched.h>

// Dimensions de la grille de jeu
#define NB_LIGNES   19
#define NB_COLONNES 19

// Macros utilisees dans le tableau tab
#define VIDE                     0
#define MUR                      1
#define BILLE                    2
#define RAQUETTE                 3

// Autres macros
#define HAUT                     100000
#define BAS                      100001
#define GAUCHE                   100002
#define DROITE                   100003

//Macros utilisee pour savoir de quel raquette on parle
#define RAQHAUT                  0
#define RAQBAS                   1
#define RAQGAUCHE                2
#define RAQDROITE                3

#define NB_MAX_BILLES_ZONE       3  // Nombre maximum de billes dans la zone magnetique

int tab[NB_LIGNES][NB_COLONNES]
={ {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
   {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}};

typedef struct
{
  int L;
  int C;
  int dir;
  int couleur;
} S_BILLE;

typedef struct
{
  int type;
  int L;
  int C;
} S_RAQUETTE;

void initGrille(int); // initialise la grille à 0
int ZoneMagnetique(int l,int c); // renvoie 1 si les coord l et c sont dans la zone magnetique. Sinon 0
int NbBillesZone(); // nbr de billes dans la zone magnétique
void attenteZone(int l, int c); // Met le thread en attente sur une variable de condition pour la zone
void delBille(int color, int dir); // met à jour les compteurs lors du retrait d'une bille
void addBille(int color, int dir);
int scanTab(int L, int C); // scanne le tableau pour savoir quel est la bille la plus proche d'une des raquettes.

void HandlerTrap(int); //Handler du verroux. Dessine un verroux sur la bille , la met en pause, change sa direction
void HandlerPause(int);

//Handler pour le deplacement des raquettes
void HandlerGauche(int);
void HandlerDroit(int);
void HandlerBas(int);
void HandlerHaut(int);

void* threadBille(void* p);
void* threadLanceBille(void *p);
void* threadVerrou(void *p);
void* threadRamasseur(void *p);
void* threadRaquette(void *p);
void* threadEvent(void *p);
void* threadIA(void *p);
void* threadPause(void* p);

/****************MUTEX ET VARIABLE DE CONDITION***********************************/

pthread_mutex_t mutexTableau; // protege la variable globale tableau
pthread_mutex_t mutexLanceBille; // Empeche la modification de la structure bille du thread pere tant qu'elle n'a pas été copiée par le thread bille
pthread_mutex_t mutexLanceRaquette; // Empeche la modification de la structure raquette du thread pere tant que celle-ci n'a pas été copiée
pthread_mutex_t mutexBille; // protege les variables globales de direction couleur et nbr de billes
pthread_mutex_t mutexBillesSorties;//Sert pour bloquer sur une condition le thread ramasseur jusqu'à ce qu'une bille sorte.

pthread_cond_t condZoneMagnetique; //Reveille les billes en attente à l'entrée de la zone.
pthread_cond_t condSortie;// Reveille le thread ramasseur de billes
pthread_cond_t condBille; // Réveille le thread lanceur de bille

/******************CLES*********************/
pthread_key_t CleBille; //Variable specifique billes
pthread_key_t CleRaquette; //Variable spécifique raquette

/**************VARIABLES GLOBALE**********************************************/
pthread_t tabThreadsBilles[12];//tableau de pid des billes en jeux


//tableau des billes sorties + indices pour savoir ou on en est
S_BILLE billesSorties[10];
int indiceI;
int indiceL;

//Infos sur les billes en jeu
int nbBilles;
int nbHorizontal, nbVertical;
int nbRouge, nbJaune, nbBleu, nbMauve, nbVert, nbMagenta;

pthread_t raqVec[4]; // pid des raquettes
int murVec[4];//Quel cote est muré.
///////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc,char* argv[])
{
  S_RAQUETTE raquette;
  pthread_t t, event, EnvoisEvent[2];
  char ok;
  int parc, l, c;

  struct sigaction hand;
  sigset_t masque;
 
  srand((unsigned)time(NULL));

  //On initialises les mutex et variables de condition
  pthread_mutex_init(&mutexTableau, NULL);
  pthread_mutex_init(&mutexLanceBille, NULL);
  pthread_mutex_init(&mutexBille, NULL);
  pthread_mutex_init(&mutexBillesSorties, NULL);
  pthread_mutex_init(&mutexLanceRaquette, NULL);

  pthread_cond_init(&condZoneMagnetique, NULL);
  pthread_cond_init(&condSortie, NULL);
  pthread_cond_init(&condBille, NULL);

  pthread_key_create(&CleBille, NULL);
  pthread_key_create(&CleRaquette, NULL);

  if (OuvertureFenetreGraphique() < 0)
  {
    printf("Erreur de OuvrirGrilleSDL\n");
    fflush(stdout);
    exit(1);
  }

  // Initialisation de la grille de jeu
  initGrille(0);

  //On amorce les signaux
  hand.sa_handler = HandlerTrap;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGTRAP, &hand, NULL);

  hand.sa_handler = HandlerGauche;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGUSR1, &hand, NULL);

  hand.sa_handler = HandlerDroit;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGUSR2, &hand, NULL);

  hand.sa_handler = HandlerHaut;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGHUP, &hand, NULL);

  hand.sa_handler = HandlerBas;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGCONT, &hand, NULL);

  hand.sa_handler = HandlerPause;
  sigemptyset(&hand.sa_mask);
  hand.sa_flags = 0;
  sigaction(SIGINT, &hand, NULL);

  //et on masque tout
  sigfillset(&masque);
  pthread_sigmask(SIG_SETMASK, &masque, NULL);
  

  /******Lancement selon le parametre*******/
  if(argc > 4)
  {
    printf("Nombre de parametre invalide (nbrJoueur nbrHumain position humain");
    exit(0);
  }

  if(argc == 1)//config par defaut avec le joueur en bas et les autres joueurs IA
  {
    // Ouverture de la fenetre graphique
    printf("(THREAD MAIN %d) Ouverture de la fenetre graphique\n",pthread_self()); fflush(stdout);

    //On lance toutes les raquettes
    pthread_mutex_lock(&mutexLanceRaquette);
    raquette.L = 1;
    raquette.C = 9;
    raquette.type = 0;
    pthread_create(&raqVec[RAQHAUT], NULL, threadRaquette, &raquette);

    pthread_mutex_lock(&mutexLanceRaquette);
    raquette.L = 17;
    raquette.C = 9;
    raquette.type = 1; //c'est le joueur humain
    pthread_create(&raqVec[RAQBAS], NULL, threadRaquette, &raquette);

    pthread_mutex_lock(&mutexLanceRaquette);
    raquette.L = 9;
    raquette.C = 1;
    raquette.type = 0;
    pthread_create(&raqVec[RAQGAUCHE], NULL, threadRaquette, &raquette);

    pthread_mutex_lock(&mutexLanceRaquette);
    raquette.L = 9;
    raquette.C = 17;
    raquette.type = 0;
    pthread_create(&raqVec[RAQDROITE], NULL, threadRaquette, &raquette);

    //thread pause
    pthread_create(&EnvoisEvent[1], NULL, threadPause, NULL);
    EnvoisEvent[0] = raqVec[RAQBAS];
    // Thread event
    pthread_create(&event, NULL, threadEvent, &EnvoisEvent);
    // On lance les billes
    pthread_create(&t, NULL, threadLanceBille, NULL);
    //on lance le verrou
    pthread_create(&t, NULL, threadVerrou, NULL);
    //on lance le ramasseur
    pthread_create(&t, NULL, threadRamasseur, NULL);
  }

  if(argc == 2)
  {
    if(*argv[1] == '0')
    {  

      for (l=4 ; l<=16 ; l++) { tab[l][17] = MUR; DessineMur(l,17); } // on mure a droite
      for (l=4 ; l<=16 ; l++) { tab[l][1] = MUR; DessineMur(l,1); } // on mure a gauche
      for (c=4 ; c<=16 ; c++) { tab[1][c] = MUR; DessineMur(1,c); } // on mure en haut
      for (c=4 ; c<=16 ; c++) { tab[17][c] = MUR; DessineMur(17,c); } // on mure le bas

      //thread pause
      pthread_create(&EnvoisEvent[1], NULL, threadPause, NULL);
      EnvoisEvent[0] = raqVec[RAQBAS];
      // Thread event
      pthread_create(&event, NULL, threadEvent, &EnvoisEvent);

      //Lance billle
      pthread_create(&t, NULL, threadLanceBille, NULL);
      //on lance le verrou
      pthread_create(&t, NULL, threadVerrou, NULL);
    }
    else
    {
      printf("Precise le nombre de joueurs humain\n");
      exit(0);
    }

  }

  if(argc == 3)
  {
    if(*argv[2] != '0')
    {
      printf("Precise la position du joueur humain\n");
      exit(0);
    }

    if(atoi(argv[1]) > 4)
    {
      printf("Le nombre de joueur doit etre compris entre 0 et 4\n");
      exit(0);
    }

    // Ouverture de la fenetre graphique
    printf("(THREAD MAIN %d) Ouverture de la fenetre graphique\n",pthread_self()); fflush(stdout);

    for(parc = 0; parc < atoi(argv[1]); parc++)
    {
      if(parc == 0)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 1;
        raquette.C = 9;
        raquette.type = 0;
        pthread_create(&raqVec[RAQHAUT], NULL, threadRaquette, &raquette);
      }

      if(parc == 1)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 17;
        raquette.C = 9;
        raquette.type = 0;
        pthread_create(&raqVec[RAQBAS], NULL, threadRaquette, &raquette);
      }

      if(parc == 2)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 9;
        raquette.C = 1;
        raquette.type = 0;
        pthread_create(&raqVec[RAQGAUCHE], NULL, threadRaquette, &raquette);
      }

      if(parc == 3)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 9;
        raquette.C = 17;
        raquette.type = 0;
        pthread_create(&raqVec[RAQDROITE], NULL, threadRaquette, &raquette);
      }
    }

    while(parc < 4)
    {
      if(parc == 0)
        for (c=4 ; c<=16 ; c++) { tab[1][c] = MUR; DessineMur(1,c); } // on mure en haut

      if(parc == 1)
        for (c=4 ; c<=16 ; c++) { tab[17][c] = MUR; DessineMur(17,c); } // on mure le bas

      if(parc == 2)
        for (l=4 ; l<=16 ; l++) { tab[l][1] = MUR; DessineMur(l,1); } // on mure a gauche

      if(parc == 3)
        for (l=4 ; l<=16 ; l++) { tab[l][17] = MUR; DessineMur(l,17); } // on mure a droite

      parc++;
    }

    parc = 0;

    pthread_create(&event, NULL, threadEvent, &parc);

    //Lance billle
    pthread_create(&t, NULL, threadLanceBille, NULL);

    //on lance le verrou
    pthread_create(&t, NULL, threadVerrou, NULL);
    //on lance le ramasseur
    pthread_create(&t, NULL, threadRamasseur, NULL);
        
  }

  if(argc == 4)
  {
    if(*argv[3] != 'b' && *argv[3] != 'h' && *argv[3] != 'g' && *argv[3] != 'd')
    {
      printf("La position du joueur humain es invalide (valeurs possible b , h, g, d\n");
      exit(0);
    }

    if(atoi(argv[1]) > 4)
    {
      printf("Le nombre de joueur doit etre compris entre 0 et 4\n");
      exit(0);
    }

    for(parc = 0; parc < atoi(argv[1]); parc++)
    {
      if(parc == 0)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 1;
        raquette.C = 9;

        if(*argv[3] == 'h')
          raquette.type = 1;

        else
          raquette.type = 0;

        pthread_create(&raqVec[RAQHAUT], NULL, threadRaquette, &raquette);
      }

      if(parc == 1)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 17;
        raquette.C = 9;
        
        if(*argv[3] == 'b')
          raquette.type = 1;

        else
          raquette.type = 0;

        pthread_create(&raqVec[RAQBAS], NULL, threadRaquette, &raquette);
      }

      if(parc == 2)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 9;
        raquette.C = 1;

        if(*argv[3] == 'g')
          raquette.type = 1;

        else
          raquette.type = 0;

        pthread_create(&raqVec[RAQGAUCHE], NULL, threadRaquette, &raquette);
      }

      if(parc == 3)
      {
        pthread_mutex_lock(&mutexLanceRaquette);
        raquette.L = 9;
        raquette.C = 17;
        
        if(*argv[3] == 'd')
          raquette.type = 1;

        else
          raquette.type = 0;

        pthread_create(&raqVec[RAQDROITE], NULL, threadRaquette, &raquette);
      }
    }

    while(parc < 4)
    {
      if(parc == 0)
        for (c=4 ; c<=16 ; c++) { tab[1][c] = MUR; DessineMur(1,c); } // on mure en haut

      if(parc == 1)
        for (c=4 ; c<=16 ; c++) { tab[17][c] = MUR; DessineMur(17,c); } // on mure le bas

      if(parc == 2)
        for (l=4 ; l<=16 ; l++) { tab[l][1] = MUR; DessineMur(l,1); } // on mure a gauche

      if(parc == 3)
        for (l=4 ; l<=16 ; l++) { tab[l][17] = MUR; DessineMur(l,17); } // on mure a droite

      parc++;
    }

    //thread pause
    pthread_create(&EnvoisEvent[1], NULL, threadPause, NULL);

 
    if(*argv[3] == 'h')
      EnvoisEvent[0] = raqVec[RAQHAUT];

    if(*argv[3] == 'b')
      EnvoisEvent[0] = raqVec[RAQBAS];

    if(*argv[3] == 'g')
      EnvoisEvent[0] = raqVec[RAQGAUCHE];

    if(*argv[3] == 'd')
      EnvoisEvent[0] = raqVec[RAQDROITE];

    //thread event
    pthread_create(&event, NULL, threadEvent, &raqVec[RAQHAUT]);

    //Lance billle
    pthread_create(&t, NULL, threadLanceBille, NULL);

    //on lance le verrou
    pthread_create(&t, NULL, threadVerrou, NULL);
    //on lance le ramasseur
    pthread_create(&t, NULL, threadRamasseur, NULL);
  }
  
  pthread_join(event, NULL);//On join avec la fin du thread event

  printf("(THREAD MAIN %d) Fermeture de la fenetre graphique...",pthread_self()); fflush(stdout);
  FermetureFenetreGraphique();
  printf("OK\n"); fflush(stdout);

  exit(0);
}

/***********************************INIT GRILLE*****************************************
*Parcours le tableau initialisé par défaut et dessine des murs la ou il en faut.
*
*
**********************************************************************************************/
void initGrille(int n)
{
  int l,c;
  for (l=0 ; l<NB_LIGNES ; l++)
    for (c=0 ; c<NB_COLONNES ; c++)
      if (tab[l][c] == 1) DessineMur(l,c);
}



/************************************ZONE MAGNETIQUE*****************************************
*Renvois 1 si les coord [l; c] se trouvent dans la zone magnétique
*
*
**********************************************************************************************/
int ZoneMagnetique(int l,int c)
{
  if ((c < 6) || (c > 12)) return 0;
  if ((l < 6) || (l > 12)) return 0;
  return 1;
}



/************************************NB BILLES ZONE*********************************************
*Renvois le nombre de billes présente dans la zone magnétique au moment de l'appel de la fonction
*
*
************************************************************************************************/
int NbBillesZone()
{
  int l,c,nb=0;
  for (l=6 ; l<=12 ; l++)
    for (c=6 ; c<=12 ; c++) 
      if (tab[l][c] == BILLE) nb++;
  return nb;
}


/******************************************THREAD BILLE*************************************************
*Controle une bille à l'ecran. Se charge d'afficher la bille dans l'interface, de déplacer celle-ci et
*d'effacer l'image de la bille sur la case qu'elle quitte.
*
********************************************************************************************************/
void* threadBille(void* p)
{
  S_BILLE MaBille;
  timespec_t tempsNano;
  int l, c, ok = 0, inZone = 0;
  sigset_t masque, demasque;

  memcpy(&MaBille, p,sizeof(S_BILLE)); // On copie la structure reçue
  pthread_mutex_unlock(&mutexLanceBille);// On lache le mutex pour que le thread lanceur puisse reutiliser sa structure

  pthread_setspecific(CleBille, (void*)&MaBille);//On met un pointeur vers la structure bille en variable spécifique

  //Masque qui accepte le SIGTRAP
  sigfillset(&demasque);
  sigdelset(&demasque, SIGTRAP);

  //Masque qui refuse tous les signaux
  sigfillset(&masque);

  pthread_mutex_lock(&mutexTableau); //Tableau bloqué pour pouvoir choisir l'endroit ou apparaitre et se dessiner.
  do
  {
    l = rand()%11+4;
    
    if(l > 5 && l < 13)
    {
      c = rand()%4+4;
      if(c > 5)
      c+=7;
    }

    else
      c = rand()%11+4;

    if(tab[l][c] == VIDE)
    {
      tab[l][c] = BILLE;
      ok = 1;
    }

  }while(!ok);

  MaBille.L = l;
  MaBille.C = c;
  DessineBille(GRISE, l, c); // On dessine et on met dans le tableau
  tab[l][c] = BILLE;
  pthread_mutex_unlock(&mutexTableau);

  tempsNano.tv_sec = 0;
  tempsNano.tv_nsec = 500000000L;
  nanosleep(&tempsNano, NULL); //La bille reste grise 0,5 sec

  DessineBille(MaBille.couleur, MaBille.L, MaBille.C); //puis on la color

  tempsNano.tv_sec = 0;
  tempsNano.tv_nsec = 350000000L; //init du temps d'attente de la bille

  while(c > 0 && c < 18 && l > 0 && l < 18) // tant qu'on ne sort pas du tableau
  {
    nanosleep(&tempsNano, NULL); //attente de 0,35 sec
    
    pthread_sigmask(SIG_SETMASK, &masque, NULL); // On masque SIGTRAP pour pas être bloqué alors qu'on tient le mutex

    pthread_mutex_lock(&mutexTableau); // et on prend le mutex

    if(MaBille.dir == GAUCHE) // Si ma bille va à gauche
    {
      if(tab[l][c-1] == VIDE) // On vérifie qu'on se deplace sur une cse libre
      {
        if((ZoneMagnetique(l, c-1)) == 1 && inZone == 0) //On verifie si on va rentrer dans la zone magnetique (et si on y est pas deja)
        {
          attenteZone(l, c-1); // On va se mettre sur la variable de condition et rester bloquer si on ne peut pas rentrer
          inZone = 1;// On est autorisé à rentrer dans la zone et on met une sorte de bool à 1 pour indiquer qu'on est dedans.
        }
        else if (inZone == 1 && (ZoneMagnetique(l, c-1)) == 0) // Si on est deja dans la zone et qu'on en sort
        {
          pthread_cond_signal(&condZoneMagnetique);// On envoit un signal pour debloquer une bille en attente
          inZone = 0; // Et on est plus dans la zone
        }


        tab[l][c] = VIDE; //On met l'ancienne case à vide
        EffaceCarre(l,c); //On efface le dessin dela bille sur l'ancienne case
        c -= 1;
        DessineBille(MaBille.couleur, l, c); // On dessine sur la nouvelle case
        tab[l][c] = BILLE; // la nouvelle case contient une bille
      }
      else // La case sur laquelle on va aller est pleine
      {
        MaBille.dir = DROITE; // on change de direction
      }
    }
    else if(MaBille.dir == DROITE)
    {
      if(tab[l][c+1] == VIDE)
      {

        if((ZoneMagnetique(l, c+1)) == 1 && inZone == 0)
        {
          attenteZone(l, c+1);
          inZone = 1;
        }
        else if (inZone == 1 && (ZoneMagnetique(l, c+1)) == 0)
        {
          pthread_cond_signal(&condZoneMagnetique);
          inZone = 0;
        }

        tab[l][c] = VIDE;
        EffaceCarre(l,c);
        c += 1;
        DessineBille(MaBille.couleur, l, c);
        tab[l][c] = BILLE;
      }
      else
      {
        MaBille.dir = GAUCHE;
      }

    }
    else if(MaBille.dir == HAUT)
    {
      if(tab[l+1][c] == VIDE)
      {
        if((ZoneMagnetique(l+1, c)) == 1 && inZone == 0)
        {
          attenteZone(l+1, c);
          inZone = 1;
        }
        else if (inZone == 1 && (ZoneMagnetique(l+1, c)) == 0)
        {
          pthread_cond_signal(&condZoneMagnetique);
          inZone = 0;
        }

        tab[l][c] = VIDE;
        EffaceCarre(l,c);
        l += 1;
        DessineBille(MaBille.couleur, l, c);
        tab[l][c] = BILLE;
      }
      else
      {
          MaBille.dir = BAS;
      }
    }
    else //BAS
    {
      if(tab[l-1][c] == VIDE)
      {

        if((ZoneMagnetique(l-1, c)) == 1 && inZone == 0)
        {
          attenteZone(l-1, c);
          inZone = 1;
        }
        else if (inZone == 1 && (ZoneMagnetique(l-1, c)) == 0)
        {
          pthread_cond_signal(&condZoneMagnetique);
          inZone = 0;
        }

        tab[l][c] = VIDE;

        EffaceCarre(l,c);
        l -= 1;
        DessineBille(MaBille.couleur, l, c);
        tab[l][c] = BILLE;
      }
      else
      {
          MaBille.dir = HAUT;
      }
    }
    pthread_mutex_unlock(&mutexTableau); // On lache la mutex
    MaBille.C = c; 
    MaBille.L = l;
    pthread_sigmask(SIG_SETMASK, &demasque, NULL); //On accepte le signal SIGTRAP
  }

  /******LA BILLE SORT DU TERRAIN***************************/
  nanosleep(&tempsNano, NULL); // derniere attente avant de sortir du tableau


  pthread_mutex_lock(&mutexBillesSorties);// On prend la mutex bille sortie

  //On met dans le vecteur globale des billes sorties notre structure
    billesSorties[indiceI].couleur = MaBille.couleur;
    billesSorties[indiceI].L = MaBille.L;
    billesSorties[indiceI].C = MaBille.C;
    billesSorties[indiceI].dir = MaBille.dir;

    indiceI++;

    if(indiceI >= 10)
      indiceI = 0;

  pthread_cond_signal(&condSortie); //On reveil le ramsaseur 
  pthread_mutex_unlock(&mutexBillesSorties);

  delBille(MaBille.couleur, MaBille.dir);// MAJ des infos sur les billes

  EffaceCarre(l,c); // On efface le carré sur laquelle la bille est morte :(
  tab[l][c] = VIDE;

  pthread_setspecific(CleBille, NULL); // Pointeur de la variable specific == NULL;

  pthread_exit(0);
}

/****************************ATTENTE ZONE***************************************************
*Paradigme d'attente pour les billes qui sont bloquées à l'entree de zone
*
*
********************************************************************************************/
void attenteZone(int l, int c)
{
  while(((NbBillesZone()) >= NB_MAX_BILLES_ZONE) || tab[l][c] != VIDE) // Tant qu'il y a 3billes et que la case à côté n'est pas libre
  {
    pthread_cond_wait(&condZoneMagnetique, &mutexTableau); // on attend le signal
  }
}



/**********************************THREAD LANCE BILLE*****************************************
*Lance d'abord 12 billes puis se met en attente d'une bille sortie. 
*Il relance les billes qui sortent du jeu en tenant compte des variables globale
*
*********************************************************************************************/
void* threadLanceBille(void *p)
{
  int tabCouleur[6] = {ROUGE, BLEUE, JAUNE, MAUVE, VERTE, MAGENTA};
  int tabDir[4] = {HAUT, DROITE, BAS, GAUCHE};
  int pColor = 0, pDir = 0, cpt;
  timespec_t tempsNano;

  S_BILLE BilleCree;

  tempsNano.tv_sec = 4;
  tempsNano.tv_nsec = 0L;

  for(cpt = 0; cpt < 12; cpt++) // On lance les 12 premieres billes billes
  {
    nanosleep(&tempsNano, NULL); // attend 4 sec

    BilleCree.couleur = tabCouleur[pColor]; //remplis la structure bille

    pColor++;
    if(pColor >= 6)
      pColor = 0;

    BilleCree.dir = tabDir[pDir];

    pDir++;
    if(pDir >= 4)
      pDir = 0;

    pthread_mutex_lock(&mutexLanceBille); // bloc le mutex (sera debloquer par le thread bille lance)
    pthread_create(&tabThreadsBilles[cpt], NULL, threadBille, &BilleCree);

    addBille(BilleCree.couleur, BilleCree.dir); // MAJ des variables globale bille
  }

  tempsNano.tv_sec = 2;
  tempsNano.tv_nsec = 0L;
  while(1) 
  {

    nanosleep(&tempsNano, NULL);
    pthread_mutex_lock(&mutexBille);
    while(nbBilles == 12)
    {

      pthread_cond_wait(&condBille, &mutexBille); //On attend qu'une bille sorte 
    }

    pDir = rand()%2;

    //Direction de la nouvelle bille
    if(nbVertical > nbHorizontal)
    {
      if(pDir)
        BilleCree.dir = GAUCHE;

      else
        BilleCree.dir = DROITE;
    }
    else
    {
      if(pDir)
        BilleCree.dir = HAUT;

      else
        BilleCree.dir = BAS;
    }

    //Couleur de la bille
    if(nbRouge < 2)
      BilleCree.couleur = ROUGE;

    else if(nbVert < 2)
      BilleCree.couleur = VERTE;

    else if(nbMauve < 2)
      BilleCree.couleur = MAUVE;

    else if(nbMagenta < 2)
      BilleCree.couleur = MAGENTA;

    else if(nbJaune < 2)
      BilleCree.couleur = JAUNE;

    else
      BilleCree.couleur = BLEUE;

    for(cpt = 0; tabThreadsBilles[cpt] != 0 && cpt < 12; cpt++); // On cherche un emplacement libre

    pthread_mutex_lock(&mutexLanceBille);
    pthread_create(&tabThreadsBilles[cpt], NULL, threadBille, &BilleCree);

    addBille(BilleCree.couleur, BilleCree.dir);
    pthread_mutex_unlock(&mutexBille);
  }
  
}

/********************************THREAD VERROU ********************************
*Thread qui va envoyer un signal pour bloquer une bille
*
*
****************************************************************************/
void* threadVerrou(void *p)
{
  timespec_t tempsNano;
  int victime;

  tempsNano.tv_sec = 10;
  tempsNano.tv_nsec = 0;
  while(1)
  {
    nanosleep(&tempsNano, NULL);// attente de 10 sec

    pthread_mutex_lock(&mutexBille);
      do
      {
        victime = rand()%12; //On choisi aléatoirement une bille dans le vecteur
      }while(!tabThreadsBilles[victime]);

      pthread_kill(tabThreadsBilles[victime], SIGTRAP); // Et on la kill
    pthread_mutex_unlock(&mutexBille);
  }
}


/***********************************HANDLER TRAP********************************************
*Le thread bille passera par ce handler apres reception du signal, dessinera un verroux sur sa bille attendre,
*changera sa direction  et se redessinera sans verrou
*
********************************************************************************************/
void HandlerTrap(int)
{
  S_BILLE* billeTraitee;
  timespec_t tempsNano;
  int randDir;

  billeTraitee = (S_BILLE*)pthread_getspecific(CleBille); // on recupere le pointeur vers la structure bille du thread qui passe ici
  
  tempsNano.tv_sec = rand()%4+4; // random pour le temps d'attente
  tempsNano.tv_nsec = 0;

  randDir = rand()%2;

  // On change la direction
  if(billeTraitee->dir == HAUT || billeTraitee->dir == BAS) 
  {
    if(randDir)
      billeTraitee->dir = GAUCHE;

    else
      billeTraitee->dir = DROITE;

    pthread_mutex_lock(&mutexBille);
      nbVertical--;
      nbHorizontal++;
    pthread_mutex_unlock(&mutexBille);
  }

  else
  {
    if(randDir)
      billeTraitee->dir = HAUT;

    else
      billeTraitee->dir = BAS;

    pthread_mutex_lock(&mutexBille);
      nbHorizontal--;
      nbVertical++;
    pthread_mutex_unlock(&mutexBille);
  }


  DessineVerrou(billeTraitee->L, billeTraitee->C); // dessin du verrou
  nanosleep(&tempsNano, NULL); // attente

  DessineBille(billeTraitee->couleur, billeTraitee->L, billeTraitee->C); // on se redessine comme il faut et on continue
}



/****************************************THREAD RAMASSEUR*******************************************************
*Quand une bille sort il va 
*
*
***************************************************************************************************************/
void* threadRamasseur(void *p)
{
  int coteMur = 0 , parc, l, c;
  int gauche = 0, droite = 0, bas = 0, haut = 0;

  for(parc = 0; parc < 4; parc++)
  {
    if(raqVec[parc] != 0)
      coteMur++;
  }

  pthread_mutex_lock(&mutexBillesSorties);
  while(coteMur > 1)
  {
    while(indiceI == indiceL)
    {
      pthread_cond_wait(&condSortie, &mutexBillesSorties);//Attend qu'une bille sorte
    }

    if(billesSorties[indiceL].L == 18 && !bas) // Si c'est sortis en bas et que ce n'est pas muré
    {
        parc = 1;
        while(tab[18][parc] != VIDE && parc < 3) // on test si y'a de la place pour ranger la bille à gauche
        {
          parc++;
        }

        if(tab[18][parc] == VIDE) 
        {
          DessineBille(billesSorties[indiceL].couleur, 18, parc); //Si c'est le cas on dessine la bille la
          tab[18][parc] = BILLE;
        }
        else //Sinon on cherche à droite
        {
          parc = 17;
          while(tab[18][parc] != VIDE && parc > 15)
          {
            parc--;
          }

          if(tab[18][parc] == VIDE)
          {
            DessineBille(billesSorties[indiceL].couleur, 18, parc);
            tab[18][parc] = BILLE;
          }
          else //Si toujours pas de place à gauche on mure
          {
            for (c=4 ; c<=16 ; c++) 
            { 
              while(tab[17][c] == BILLE) // Quand on mur on s'assure qu'il n'y a pas de billes qui se trouvent pile a la position du mur à ce moment
              {
                sched_yield();//Si y'a une bille mal placée (pas de chance) on lache la main en esperant que la bille la reprenne et bouge de la
              }

              pthread_mutex_lock(&mutexTableau);
                tab[17][c] = MUR; 
                DessineMur(17,c);
              pthread_mutex_unlock(&mutexTableau);
            }
            

            DessineChiffre(coteMur, 17, 9); // On dessine le chiffre
            coteMur --; //On dit que ce côté est muré
            bas++;
            murVec[RAQBAS] = 1; 
          }
        }
    }

    else if(billesSorties[indiceL].L == 0 && !haut)
    {
        parc = 1;
        while(tab[0][parc] != VIDE && parc < 3)
        {
          parc++;
        }

        if(tab[0][parc] == VIDE)
        {
          DessineBille(billesSorties[indiceL].couleur, 0, parc);
          tab[0][parc] = BILLE;
        }
        else
        {
          parc = 17;
          while(tab[0][parc] != VIDE && parc > 15)
          {
            parc--;
          }

          if(tab[0][parc] == VIDE)
          {
            DessineBille(billesSorties[indiceL].couleur, 0, parc);
            tab[0][parc] = BILLE;
          }
          else
          {

            for (c=4 ; c<=16 ; c++) 
            { 
              while(tab[1][c] == BILLE)
              {
                sched_yield();
              }

              pthread_mutex_lock(&mutexTableau);
              tab[1][c] = MUR; 
              DessineMur(1,c);
              pthread_mutex_unlock(&mutexTableau);
            }

            DessineChiffre(coteMur, 1, 9);
            coteMur --;
            haut++;
            murVec[RAQHAUT] = 1;
          }
        }
    }

    else if(billesSorties[indiceL].C == 0 && !gauche)
    {
        parc = 1;
        while(tab[parc][0] != VIDE && parc < 3)
        {
          parc++;
        }

        if(tab[parc][0] == VIDE)
        {
          DessineBille(billesSorties[indiceL].couleur, parc, 0);
          tab[parc][0] = BILLE;
        }
        else
        {
          parc = 17;
          while(tab[parc][0] != VIDE && parc > 15)
          {
            parc--;
          }

          if(tab[parc][0] == VIDE)
          {
            DessineBille(billesSorties[indiceL].couleur, parc, 0);
            tab[parc][0] = BILLE;
          }
          else
          {

            for (l=4 ; l<=16 ; l++) 
            { 

              while(tab[l][1] == BILLE)
              {
                sched_yield();
              }

              pthread_mutex_lock(&mutexTableau);
              tab[l][1] = MUR; 
              DessineMur(l,1);
              pthread_mutex_unlock(&mutexTableau);
            }

            DessineChiffre(coteMur, 9, 1);
            
            coteMur --;
            gauche++;
            murVec[RAQGAUCHE] = 1;
          }
        }
    }
    else if(billesSorties[indiceL].C == 18 && !droite)
    {
        parc = 1;
        while(tab[parc][18] != VIDE && parc < 3)
        {
          parc++;
        }

        if(tab[parc][18] == VIDE)
        {
          DessineBille(billesSorties[indiceL].couleur, parc, 18);
          tab[parc][18] = BILLE;
        }
        else
        {
          parc = 17;
          while(tab[parc][18] != VIDE && parc > 15)
          {
            parc--;
          }

          if(tab[parc][18] == VIDE)
          {
            DessineBille(billesSorties[indiceL].couleur, parc, 18);
            tab[parc][18] = BILLE;
          }
          else
          {
            for (l=4 ; l<=16 ; l++) 
            { 
              while(tab[l][17] == BILLE)
              {
                sched_yield();
              }

              pthread_mutex_lock(&mutexTableau);
              tab[l][17] = MUR; 
              DessineMur(l,17);
              pthread_mutex_unlock(&mutexTableau);
            }


            DessineChiffre(coteMur, 9, 17);
            
            coteMur --;
            droite++;
            murVec[RAQDROITE] = 1;
          }
        }
    }

    indiceL++; // On change l'indice des tableaux

    if(indiceL >= 10)
        indiceL = 0;
  }

  if(murVec[RAQDROITE] != 1)
  {
    for (l=4 ; l<=16 ; l++) 
    { 
      while(tab[l][17] == BILLE)
      {
        sched_yield();
      }

      pthread_mutex_lock(&mutexTableau);
      tab[l][17] = MUR; 
      DessineMur(l,17);
      pthread_mutex_unlock(&mutexTableau);
    }

    DessineChiffre(1, 9, 17);

    murVec[RAQDROITE] = 1;
  }

  else if(murVec[RAQGAUCHE] != 1)
  {
    for (l=4 ; l<=16 ; l++) 
    { 

      while(tab[l][1] == BILLE)
      {
        sched_yield();
      }

      pthread_mutex_lock(&mutexTableau);
      tab[l][1] = MUR; 
      DessineMur(l,1);
        pthread_mutex_unlock(&mutexTableau);
      }

      DessineChiffre(1, 9, 1);
            
      murVec[RAQGAUCHE] = 1;
  }

  else if(murVec[RAQHAUT] != 1)
  {
    for (c=4 ; c<=16 ; c++) 
    { 
      while(tab[1][c] == BILLE)
      {
        sched_yield();
      }

      pthread_mutex_lock(&mutexTableau);
      tab[1][c] = MUR; 
      DessineMur(1,c);
      pthread_mutex_unlock(&mutexTableau);
    }

    DessineChiffre(1, 1, 9);
    murVec[RAQHAUT] = 1;
  }

  else if(murVec[RAQBAS] != 1)
  {
    for (c=4 ; c<=16 ; c++) 
    { 
      while(tab[17][c] == BILLE) // Quand on mur on s'assure qu'il n'y a pas de billes qui se trouvent pile a la position du mur à ce moment
      {
        sched_yield();//Si y'a une bille mal placée (pas de chance) on lache la main en esperant que la bille la reprenne et bouge de la
      }

      pthread_mutex_lock(&mutexTableau);
      tab[17][c] = MUR; 
      DessineMur(17,c);
      pthread_mutex_unlock(&mutexTableau);
    }
            
    DessineChiffre(1, 17, 9); // On dessine le chiffre
    coteMur --; //On dit que ce côté est muré
    bas++;
    murVec[RAQBAS] = 1; 
  }

  pthread_mutex_unlock(&mutexBillesSorties);
}


/**********************************DEL BILLE *************************************/
//Maj des variables gloables liées aux billes
void delBille(int color, int dir)
{
  pthread_mutex_lock(&mutexBille);
  int cpt;

  nbBilles--;

  if(color == ROUGE)
    nbRouge--;

  else if(color == VERTE)
    nbVert--;

  else if(color == BLEUE)
    nbBleu--;

  else if(color == JAUNE)
    nbJaune--;

  else if(color == MAUVE)
    nbMauve--;

  else if (color == MAGENTA)
    nbMagenta--;

  if(dir == HAUT || dir == BAS)
    nbVertical--;

  else if(dir == DROITE || dir == GAUCHE)
    nbHorizontal--;

  for(cpt = 0; tabThreadsBilles[cpt] != pthread_self(); cpt++);

  tabThreadsBilles[cpt] = 0;
  
  pthread_cond_signal(&condBille);
  pthread_mutex_unlock(&mutexBille);
}

/***********************************ADD BILLE ***************************/
//Mise a jour des variables de la bille
void addBille(int color, int dir)
{

  nbBilles++;

  if(color == ROUGE)
    nbRouge++;

  else if(color == VERTE)
    nbVert++;

  else if(color == BLEUE)
    nbBleu++;

  else if(color == JAUNE)
    nbJaune++;

  else if(color == MAUVE)
    nbMauve++;

  else if (color == MAGENTA)
    nbMagenta++;

  if(dir == HAUT || dir == BAS)
    nbVertical++;

  else if(dir == DROITE || dir == GAUCHE)
    nbHorizontal++;
}

/*********************************THREAD RAQUETTE **************************************
*Determine quel raquette il est (quel cote) se dessine, lance un thread IA si y'a besoin
*Masque les signaux inutiles et demasque les utiles. Se metsur une boucle infinie en attente de signaux
*
*****************************************************************************************/
void* threadRaquette(void *p)
{
  S_RAQUETTE MaRaquette;
  int dir = 0, parc, ia;
  sigset_t masque;
  pthread_t t;

  memcpy(&MaRaquette, p,sizeof(S_RAQUETTE));
  pthread_mutex_unlock(&mutexLanceRaquette);

  if(MaRaquette.L == 1)// si raquette du haut
  {
    DessineRaquetteHaut(MaRaquette.L, MaRaquette.C); // se dessine
    dir = 1;
    ia = RAQHAUT; 

    if(!MaRaquette.type) // si c'est pas une humaine
      pthread_create(&t, NULL, threadIA, &ia); // lance une IA
  }
  else if(MaRaquette.L == 17)
  {
    DessineRaquetteBas(MaRaquette.L, MaRaquette.C);
    dir = 1;

    ia = RAQBAS;

    if(!MaRaquette.type)
      pthread_create(&t, NULL, threadIA, &ia);
  }
  else if(MaRaquette.C == 1)
  {
    DessineRaquetteGauche(MaRaquette.L, MaRaquette.C);

    ia = RAQGAUCHE;

    if(!MaRaquette.type)
      pthread_create(&t, NULL, threadIA, &ia);
  }
  else
  {
    DessineRaquetteDroite(MaRaquette.L, MaRaquette.C);

    ia = RAQDROITE;

    if(!MaRaquette.type)
      pthread_create(&t, NULL, threadIA, &ia);
  }

  pthread_mutex_lock(&mutexTableau);// Bloque le tableau pour bloquer les cases qui sont raquette
    if(dir)
    {
      for(parc = (MaRaquette.C)-2; parc != (MaRaquette.C)+3; parc++)
        tab[MaRaquette.L][parc] = RAQUETTE;
    }
    else
    {
      for(parc = (MaRaquette.L)-2; parc != (MaRaquette.L)+3; parc++)
        tab[parc][MaRaquette.C] = RAQUETTE;
    }
  pthread_mutex_unlock(&mutexTableau);

  if(dir) // si elle est horizontal on demasque ces signaux
  {
    sigfillset(&masque);
    sigdelset(&masque, SIGUSR1);
    sigdelset(&masque, SIGUSR2);
    pthread_sigmask(SIG_SETMASK, &masque, NULL);
  }
  else // si verticale on demasque les autres
  {
    sigfillset(&masque);
    sigdelset(&masque, SIGHUP);
    sigdelset(&masque, SIGCONT);
    pthread_sigmask(SIG_SETMASK, &masque, NULL);
  }

  pthread_setspecific(CleRaquette, (void*)&MaRaquette);

  while(1)
    pause();
}


/************************HANDLER DE DIRECTION*******************************/
//Les raquetes vont passer dedans pour bouger dans la direction voulue
void HandlerGauche(int)
{
  S_RAQUETTE* raquetteTraitee;
  int parc;

  raquetteTraitee = (S_RAQUETTE*)pthread_getspecific(CleRaquette);//On recupere la structure de la raquette qui passe ici

  pthread_mutex_lock(&mutexTableau);//lock le tableau pour la modif

  if(tab[raquetteTraitee->L][raquetteTraitee->C-3] == VIDE)//Si c'est vide a gauche (pas de mur ou de bille) on peut bouger
  {
    raquetteTraitee->C--;
    if(raquetteTraitee->L == 1)//si en haut
      DessineRaquetteHaut(raquetteTraitee->L, raquetteTraitee->C);

    else//Si en bas
      DessineRaquetteBas(raquetteTraitee->L, raquetteTraitee->C); 

    for(parc = (raquetteTraitee->C)-2; parc != (raquetteTraitee->C)+3; parc++) // On boucle pour ecrire l'emplacement des raquettes dans le tableau
        tab[raquetteTraitee->L][parc] = RAQUETTE;

    EffaceCarre(raquetteTraitee->L, raquetteTraitee->C+3); //On efface l'ancienne raquette
    tab[raquetteTraitee->L][raquetteTraitee->C+3] = VIDE;
  }
  pthread_mutex_unlock(&mutexTableau);
}

void HandlerDroit(int)
{
  S_RAQUETTE* raquetteTraitee;
  int parc;

  raquetteTraitee = (S_RAQUETTE*)pthread_getspecific(CleRaquette);

  pthread_mutex_lock(&mutexTableau);
  if(tab[raquetteTraitee->L][raquetteTraitee->C+3] == VIDE)
  {
    raquetteTraitee->C++;
    if(raquetteTraitee->L == 1)
      DessineRaquetteHaut(raquetteTraitee->L, raquetteTraitee->C);

    else
      DessineRaquetteBas(raquetteTraitee->L, raquetteTraitee->C);

    for(parc = (raquetteTraitee->C)-2; parc != (raquetteTraitee->C)+3; parc++)
        tab[raquetteTraitee->L][parc] = RAQUETTE;

    EffaceCarre(raquetteTraitee->L, raquetteTraitee->C-3);
    tab[raquetteTraitee->L][raquetteTraitee->C-3] = VIDE;
  }
  pthread_mutex_unlock(&mutexTableau);
}

void HandlerBas(int)
{
  S_RAQUETTE* raquetteTraitee;
  int parc;

  raquetteTraitee = (S_RAQUETTE*)pthread_getspecific(CleRaquette);

  pthread_mutex_lock(&mutexTableau);
  if(tab[raquetteTraitee->L+3][raquetteTraitee->C] == VIDE)
  {
    raquetteTraitee->L++;
    if(raquetteTraitee->C == 1)
      DessineRaquetteGauche(raquetteTraitee->L, raquetteTraitee->C);

    else
      DessineRaquetteDroite(raquetteTraitee->L, raquetteTraitee->C);

    for(parc = (raquetteTraitee->L)-2; parc != (raquetteTraitee->L)+3; parc++)
        tab[parc][raquetteTraitee->C] = RAQUETTE;

    EffaceCarre(raquetteTraitee->L-3, raquetteTraitee->C);
    tab[raquetteTraitee->L-3][raquetteTraitee->C] = VIDE;
    
  }
  pthread_mutex_unlock(&mutexTableau);
}

void HandlerPause(int)
{
  fflush(stdout);
  pthread_mutex_unlock(&mutexTableau);
}

void HandlerHaut(int)
{
  S_RAQUETTE* raquetteTraitee;
  int parc;

  raquetteTraitee = (S_RAQUETTE*)pthread_getspecific(CleRaquette);

  pthread_mutex_lock(&mutexTableau);
  if(tab[raquetteTraitee->L-3][raquetteTraitee->C] == VIDE)
  {
    raquetteTraitee->L--;
    if(raquetteTraitee->C == 1)
      DessineRaquetteGauche(raquetteTraitee->L, raquetteTraitee->C);

    else
      DessineRaquetteDroite(raquetteTraitee->L, raquetteTraitee->C);

    for(parc = (raquetteTraitee->L)-2; parc != (raquetteTraitee->L)+3; parc++)
        tab[parc][raquetteTraitee->C] = RAQUETTE;

    EffaceCarre(raquetteTraitee->L+3, raquetteTraitee->C);
    tab[raquetteTraitee->L+3][raquetteTraitee->C] = VIDE;
    
  }
  pthread_mutex_unlock(&mutexTableau);
}



/********************THREAD EVENT**************************************************/
//Se met en attente d'une entree clavier
void* threadEvent(void *p)
{ 
  int ok;
  pthread_t raq[2], t;
  EVENT_GRILLE_SDL event;

  memcpy(&raq, p,2*sizeof(pthread_t));

  while(1)
  {
    event = ReadEvent();
    if (event.type == CROIX) pthread_exit(0);// click sur la croix de la fenêtre
    if (event.type == CLAVIER && (event.touche == 'q' || event.touche == 'Q')) pthread_exit(0);
    if (event.type == CLAVIER && (event.touche == 'p' || event.touche == 'P')) pthread_kill(raq[1], SIGINT);
    if (event.type == CLAVIER && event.touche == KEY_UP) pthread_kill(raq[0], SIGHUP); //fleche du haut
    if (event.type == CLAVIER && event.touche == KEY_DOWN) pthread_kill(raq[0], SIGCONT); // fleche bas
    if (event.type == CLAVIER && event.touche == KEY_RIGHT) pthread_kill(raq[0], SIGUSR2); //fleche droite
    if (event.type == CLAVIER && event.touche == KEY_LEFT) pthread_kill(raq[0], SIGUSR1); //fleche gauche
  }
}


/*********************************THREAD IA*******************************************/
//Fait se deplacer les raquettes non humaine vers la bille la plus proche
void* threadIA(void *p)
{
  int dir, centre = 9, bProche;
  timespec_t tempsNano;

  memcpy(&dir, p,sizeof(pthread_t)); // On recupere le pid de la raquette controlée

  tempsNano.tv_sec = 0;
  tempsNano.tv_nsec = 400000000L;

  while(murVec[dir] != 1)
  {
    nanosleep(&tempsNano, NULL); // attente de 0,4 sec avant chaque mouvement

    if(dir == RAQHAUT)// Si on est en haut
    {
      pthread_mutex_lock(&mutexTableau);//lock du tableau
      bProche = scanTab(1, centre); //On scan pour savoir ou se trouve la bille la plus proche
      pthread_mutex_unlock(&mutexTableau);
    }

    else if(dir == RAQBAS)
    {
      pthread_mutex_lock(&mutexTableau);
      bProche = scanTab(17, centre);
      pthread_mutex_unlock(&mutexTableau);
    }

    else if(dir == RAQGAUCHE)
    {
      pthread_mutex_lock(&mutexTableau);
      bProche = scanTab(centre, 1);
      pthread_mutex_unlock(&mutexTableau);
    }

    else
    {
      pthread_mutex_lock(&mutexTableau);
      bProche = scanTab(centre, 17);
      pthread_mutex_unlock(&mutexTableau);
    }

    if(bProche) // Si la bille la plus proche n'est pas déjà au centre de la raquette
    {
      if(bProche > centre && centre <= 14) // Si l'indice de la bille est plus grand que celui du centre
      {
        pthread_kill(raqVec[dir], SIGUSR2); // On descend ou on va a droite
        pthread_kill(raqVec[dir], SIGCONT);
        centre ++;
      }
      else if(bProche < centre && centre >= 4)
      {
        pthread_kill(raqVec[dir], SIGUSR1); //Sinon on monte ou on va a gauche
        pthread_kill(raqVec[dir], SIGHUP);
        centre --; //MAJ du centre de la raquette
      }
    }

  }
}

/********************THREAD PAUSE**************************************************/
//Le jeu se met en pause
void* threadPause(void *p)
{
  sigset_t masque;
  int pa = 0, rec;

  //Masque qui accepte le SIGINT
  sigfillset(&masque);
  sigdelset(&masque, SIGINT);
  pthread_sigmask(SIG_SETMASK, &masque, NULL);

  while(1)
  {
    sigwait(&masque, &rec);

    if(!pa)
    {
      pthread_mutex_lock(&mutexTableau);
      pa = 1;
    }
    else
      pa = 0;
  }
}

/**********************SCAN TAB*******************************************/
//Renvois l'indice dans le tableau de la bille la plus proche des coord L et C
int scanTab(int L, int C)
{
  int bille, parcL, parcC;

  if(L == 1)
  {
    for(parcL = 2; parcL < 18; parcL++)
    {
      for(parcC = 4; parcC < 15; parcC++)
      {
        if(tab[parcL][parcC] == BILLE)
          return parcC;
      }
    }
  }


  else if(L == 17)
  {
    for(parcL = 17; parcL > 1; parcL--)
    {
      for(parcC = 4; parcC < 15; parcC++)
      {
        if(tab[parcL][parcC] == BILLE)
          return parcC;
      }
    }
  }

  else if(C == 1)
  {
    for(parcC = 2; parcC < 18; parcC++)
    {
      for(parcL = 4; parcL < 15; parcL++)
      {
        if(tab[parcL][parcC] == BILLE)
          return parcL;
      }
    }
  }
  else
  {
    for(parcC = 17; parcC > 1; parcC--)
    {
      for(parcL = 4; parcL < 15; parcL++)
      {
        if(tab[parcL][parcC] == BILLE)
          return parcL;
      }
    }
  }

  return 0;
}

