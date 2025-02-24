/*
  Fvwm command input interface.
 
  Copyright 1996, Toshi Isogai. No guarantees or warantees or anything
  are provided. Use this program at your own risk. Permission to use 
  this program for any purpose is given,
  as long as the copyright is kept intact. 
*/

#include "FvwmConsole.h"

char *MyName;

int fd[2];  /* pipe to fvwm */
FILE *sp;
int  s,ns;             /* socket handles */
int  fd_width;
char name[24]; /* name of this program in executable format */
int  pid;      /* server routine child process id */

void server(int *fd);
void GetResponse(); 
void DeadPipe();
void CloseSocket();
void ErrMsg( char *msg );
void SigHandler();

#define XARGS (sizeof(xterm_a)/sizeof(char *))

void main(int argc, char **argv){
  char *tmp, *s;
  static char client[120];
  char **eargv;
  int i,j;
  char *xterm_a[] = {"-title", name,"-name",name, "-e",client,NULL };

  /* initially no child */
  pid = 0;

  /* Save the program name - its used for error messages and option parsing */
  tmp = argv[0];

  s=strrchr(argv[0], '/');
  if (s != NULL)
    tmp = s + 1;

  strcpy( name, tmp );

  MyName = safemalloc(strlen(tmp)+2);
  strcpy(MyName,"*");
  strcat(MyName, tmp);

  /* construct client's name */
  strcpy( client, argv[0] );
  strcat( client, "C" );

  if(argc < FARGS)    {
	fprintf(stderr,"%s Version %s should only be executed by fvwm!",MyName,
			VERSION);
	exit(1);
  }

  if( ( eargv =(char **)safemalloc((argc-FARGS+XARGS)*sizeof(char *)) ) == NULL ) {
	ErrMsg( "allocation" );
  }

  /* copy arguments */
  eargv[0] = XTERM;
  j= 1;
  for ( i=FARGS ; i<argc; i++,j++ ) {
	eargv[j] = argv[i];
  }

  for ( i=0 ; xterm_a[i] != NULL ; j++, i++ ) {
	eargv[j] = xterm_a[i];
  }
  eargv[j] = NULL;

  fd_width = GetFdWidth();

  /* Dead pipes mean fvwm died */
  signal (SIGPIPE, DeadPipe);  
  signal (SIGKILL, SigHandler);  
  signal (SIGINT, SigHandler);  
  signal (SIGQUIT, SigHandler);  

  fd[0] = atoi(argv[1]);
  fd[1] = atoi(argv[2]);

  /* launch xterm with client */
  /*
  if( fork() == 0 ) {
	execvp( *eargv, eargv );
	ErrMsg("exec");
  }
  */
  server(fd);
}

/***********************************************************************
 *	signal handler
 ***********************************************************************/
void DeadPipe() {
  fprintf(stderr,"%s: dead pipe\n", name);
  CloseSocket();
  exit(0);
}

void SigHandler() {
  CloseSocket();
  exit(1);
}

/*********************************************************/
/* close sockets and spawned process                     */
/*********************************************************/
void CloseSocket() {
  if( pid ) {
	kill( pid, SIGQUIT );
  }
  close(ns);     /* remove the socket */
}

/*********************************************************/
/* setup server and communicate with fvwm and the client */
/*********************************************************/
void server (int *fd) {
  struct sockaddr_un sas, csas;
  int  len, clen;     /* length of sockaddr */
  char buf[BUFSIZE];      /*  command line buffer */

  /* make a socket  */
  if( (s = socket(AF_UNIX, SOCK_STREAM, 0 )) < 0  ) {
	ErrMsg( "socket");
	exit(1);
  }

  /* name the socket */
  sas.sun_family = AF_UNIX;
  strcpy( sas.sun_path, S_NAME );

  /* bind the above name to the socket */
  /* first, erase the old socket */
  unlink( S_NAME ); 
  len = sizeof( sas.sun_family) + strlen( sas.sun_path );

  if( bind(s, &sas,len) < 0 ) {
	ErrMsg( "bind" );
	exit(1);
  }

  /* listen to the socket */
  /* set backlog to 5 */
  if ( listen(s,5) < 0 ) {
    ErrMsg( "listen" );
	exit(1);
  }

  /* accept connections */
  clen = sizeof(csas);

  while(1) {
  if(( ns = accept(s, &csas, &clen)) < 0 ) {
	ErrMsg( "accept");
	exit(1);
  }

  if( fork() ) {
	continue;
  }
  /* get command from client and return result */
  sp = fdopen( ns, "r" );
  pid = fork();
  if( pid == -1 ) {
	ErrMsg(  "fork");
	exit(1);
  }
  if( pid != 0 ) {
	while(fgets( buf, BUFSIZE, sp )) {

	  /* check if client is terminated */
	  if( buf == NULL ) {
		break;
	  }
	  SendInfo(fd[0], buf, 0); /* send command */
	}
	CloseSocket();
	exit(0);
  }
  while(1) {
	GetResponse();
  }
  }
}

/**********************************************/
/* read fvwm packet and pass it to the client */
/**********************************************/
void GetResponse() {
  fd_set in_fdset;
  unsigned long *body;
  unsigned long header[HEADER_SIZE];

  FD_ZERO(&in_fdset);
  FD_SET(fd[1],&in_fdset);

  /* ignore anything but error message */
  if( ReadFvwmPacket(fd[1],header,&body) > 0)	 {
	if(header[1] == M_PASS)	     { 
	  send( ns, (char *)&body[3], strlen((char *)&body[3]), 0 ); 
	} 
	free(body);
  }
}

/************************************/
/* print error message on stderr */
/************************************/
void ErrMsg( char *msg ) {
  fprintf( stderr, "%s server error in %s\n", name, msg );
  CloseSocket();
  exit(1);
}
