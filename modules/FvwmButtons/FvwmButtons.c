/*
   FvwmButtons v2.0.41-plural-Z-alpha, copyright 1996, Jarl Totland

 * This module, and the entire GoodStuff program, and the concept for
 * interfacing this module to the Window Manager, are all original work
 * by Robert Nation
 *
 * Copyright 1993, Robert Nation. No guarantees or warantees or anything
 * are provided or implied in any way whatsoever. Use this program at your
 * own risk. Permission to use this program for any purpose is given,
 * as long as the copyright is kept intact.

*/

/* ------------------------------- includes -------------------------------- */

#ifdef ISC
#include <sys/bsdtypes.h> /* Saul */
#endif

#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#if defined ___AIX || defined _AIX || defined __QNX__ || defined ___AIXV3 || defined AIXV3 || defined _SEQUENT_
#include <sys/select.h>
#endif

#ifdef I18N
#include <X11/Xlocale.h>
#endif
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#ifdef XPM
#include <X11/xpm.h>
#endif

#include <FVWMconfig.h>
#include "../../fvwm/module.h"
#include <fvwm/version.h>
#include <fvwm/fvwmlib.h>
#include "FvwmButtons.h"
#include "misc.h" /* ConstrainSize() */
#include "parse.h" /* ParseOptions() */
#include "icons.h" /* CreateIconWindow(), ConfigureIconWindow() */
#include "draw.h"


#define MW_EVENTS   (ExposureMask |\
		     StructureNotifyMask |\
		     ButtonReleaseMask | ButtonPressMask |\
		     KeyReleaseMask | KeyPressMask)
/* SW_EVENTS are for swallowed windows... */
#define SW_EVENTS   (PropertyChangeMask | StructureNotifyMask |\
		     ResizeRedirectMask | SubstructureNotifyMask)

/* --------------------------- external functions -------------------------- */
extern void DumpButtons(button_info*);
extern void SaveButtons(button_info*);

/* ------------------------------ prototypes ------------------------------- */

void DeadPipe(int nonsense);
void SetButtonSize(button_info*,int,int);
/* main */
void Loop(void);
void RedrawWindow(button_info*);
void RecursiveLoadData(button_info*,int*,int*);
void CreateWindow(button_info*,int,int);
Pixel GetShadow(Pixel background);
Pixel GetHilite(Pixel background);
void nocolor(char *a, char *b);
Pixel GetColor(char *name);
int My_XNextEvent(Display *dpy, XEvent *event);
void process_message(unsigned long type,unsigned long *body);
void send_clientmessage (Window w, Atom a, Time timestamp);
void CheckForHangon(unsigned long*);
Window GetRealGeometry(Display*,Window,int*,int*,ushort*,ushort*,
		       ushort*,ushort*);
void swallow(unsigned long*);
void AddButtonAction(button_info*,int,char*);
char *GetButtonAction(button_info*,int);

void DebugEvents(XEvent*);

/* -------------------------------- globals ---------------------------------*/

Display *Dpy;
Window Root;
Window MyWindow;
char *MyName;
XFontStruct *font;
int screen;
int d_depth;

int x_fd,fd_width;

char *config_file = NULL;

static Atom _XA_WM_DEL_WIN;
Atom _XA_WM_PROTOCOLS;
Atom _XA_WM_NORMAL_HINTS;
Atom _XA_WM_NAME;

char *iconPath = NULL;
char *pixmapPath = NULL;

Pixel hilite_pix, back_pix, shadow_pix, fore_pix;
GC  NormalGC;
int Width,Height;

int x= -30000,y= -30000,w= -1,h= -1,gravity = NorthWestGravity;
int new_desk = 0;
int ready = 0;
int xneg = 0, yneg = 0;

button_info *CurrentButton = NULL;
int fd[2];

button_info *UberButton=NULL;

/* ------------------------------ Misc functions ----------------------------*/

#ifdef DEBUG
char *mymalloc(int length)
{
  int i=length;
  char *p=safemalloc(length);
  while(i)
    p[--i]=255;
  return p;
}
#endif

#ifdef I18N
/*
 * fake XFetchName() function
 */
static Status MyXFetchName(dpy, win, winname)
Display *dpy;
Window win;
char **winname;
{
  XTextProperty text;
  char **list;
  int nitems;

  if (XGetWMName(dpy, win, &text)) {
    if (text.value)
      text.nitems = strlen(text.value);
    if (XmbTextPropertyToTextList(dpy, &text, &list, &nitems) >= Success &&
      *list) {
      *winname = *list;
      return 0;
    }
    return 1;
  }
  return 0; /* Unsuccessful */
}
#define XFetchName(x,y,z) MyXFetchName(x,y,z)
#endif

/**
*** Some fancy routines straight out of the manual :-) Used in DeadPipe.
**/
Bool DestroyedWindow(Display *d,XEvent *e,char *a)
{
  if(e->xany.window == (Window)a)
    if((e->type == DestroyNotify && e->xdestroywindow.window == (Window)a)||
       (e->type == UnmapNotify && e->xunmap.window == (Window)a))
      return True;
  return False;
}
int IsThereADestroyEvent(button_info *b)
{
  XEvent event;
  Bool DestroyedWindow();
  return XCheckIfEvent(Dpy,&event,DestroyedWindow,(char*)b->IconWin);
}

/**
*** DeadPipe()
*** Dead pipe handler
**/
void DeadPipe(int whatever)
{
  button_info *b,*ub=UberButton;
  int button=-1;

  signal(SIGPIPE, SIG_IGN);/* Xsync may cause SIGPIPE */

  XSync(Dpy,0); /* Wait for thing to settle down a bit */
  XGrabServer(Dpy); /* We don't want interference right now */
  while(NextButton(&ub,&b,&button,0))
    {
      /* delete swallowed windows */
      if((buttonSwallowCount(b)==3) && b->IconWin)
	{
#         ifdef DEBUG_HANGON
	  fprintf(stderr,"%s: Button 0x%06x window 0x%x (\"%s\") is ",
		  MyName,(ushort)b,(ushort)b->IconWin,b->hangon);
#         endif
	  if(!IsThereADestroyEvent(b)) /* Has someone destroyed it? */
            {
	    if(!(buttonSwallow(b)&b_NoClose))
	      {
		if(buttonSwallow(b)&b_Kill)
		  {
		    XKillClient(Dpy,b->IconWin);
#                   ifdef DEBUG_HANGON
		    fprintf(stderr,"now killed\n");
#                   endif
		  }
		else
		  {
		    send_clientmessage(b->IconWin,_XA_WM_DEL_WIN,
				       CurrentTime);
#                   ifdef DEBUG_HANGON
		    fprintf(stderr,"now deleted\n");
#                   endif
		  }
	      }
	    else
	      {
#               ifdef DEBUG_HANGON
		fprintf(stderr,"now unswallowed\n");
#               endif
		XReparentWindow(Dpy,b->IconWin,Root,b->x,b->y);
		XMoveWindow(Dpy,b->IconWin,b->x,b->y);
		XResizeWindow(Dpy,b->IconWin,b->w,b->h);
		XSetWindowBorderWidth(Dpy,b->IconWin,b->bw);
	      }
            }
#         ifdef DEBUG_HANGON
	  else
	    fprintf(stderr,"already handled\n");
#         endif
	}
    }
  XUngrabServer(Dpy); /* We're through */
  XSync(Dpy,0); /* Let it all die down again so we can catch our X errors... */

  /* Hey, we have to free the pictures too! */
  button=-1;ub=UberButton;
  while(NextButton(&ub,&b,&button,1))
    {
      if(b->flags&b_Icon)
	DestroyPicture(Dpy,b->icon);
/*
      if(b->flags&b_IconBack)
	DestroyPicture(Dpy,b->backicon);
      if(b->flags&b_Container && b->c->flags&b_IconBack)
	DestroyPicture(Dpy,b->c->backicon);
*/
    }

  exit(0);
}

/**
*** SetButtonSize()
*** Propagates global geometry down through the buttonhierarchy.
**/
void SetButtonSize(button_info *ub,int w,int h)
{
  int i=0,dx,dy;
  if(!ub || !(ub->flags&b_Container))
    {
      fprintf(stderr,"%s: BUG: Tried to set size of noncontainer\n",MyName);
      exit(2);
    }
  if(ub->c->num_rows==0 || ub->c->num_columns==0)
    {
      fprintf(stderr,"%s: BUG: Set size when rows/cols was unset\n",MyName);
      exit(2);
    } 
  w*=ub->BWidth;
  h*=ub->BHeight;

  if(ub->parent)
    {
      i=buttonNum(ub);
      ub->c->xpos=buttonXPos(ub,i);
      ub->c->ypos=buttonYPos(ub,i);
    }
  dx=buttonXPad(ub)+buttonFrame(ub);
  dy=buttonYPad(ub)+buttonFrame(ub);
  ub->c->xpos+=dx;
  ub->c->ypos+=dy;
  w-=2*dx;
  h-=2*dy;
  ub->c->ButtonWidth=w/ub->c->num_columns;
  ub->c->ButtonHeight=h/ub->c->num_rows;

  i=0;
  while(i<ub->c->num_buttons)
    {
      if(ub->c->buttons[i] && ub->c->buttons[i]->flags&b_Container)
	SetButtonSize(ub->c->buttons[i],
		      ub->c->ButtonWidth,ub->c->ButtonHeight);
      i++;
    }
}


/**
*** AddButtonAction()
**/
void AddButtonAction(button_info *b,int n,char *action)
{
  if(!b || n<0 || n>3 || !action)
    {
      fprintf(stderr,"%s: BUG: AddButtonAction failed\n",MyName);
      exit(2);
    }
  if(b->flags&b_Action)
    {
      if(b->action[n])
	free(b->action[n]);
      b->action[n]=action;
    }
  else
    {
      int i;
      b->action=(char**)mymalloc(4*sizeof(char*));
      for(i=0;i<4;b->action[i++]=NULL);
      b->action[n]=action;
      b->flags|=b_Action;
    }
}

/**
*** GetButtonAction()
**/
char *GetButtonAction(button_info *b,int n)
{
  if(!b || !(b->flags&b_Action) || !(b->action) || n<0 || n>3)
    return NULL;
  return b->action[n];
}

/**
*** myErrorHandler()
*** Shows X errors made by FvwmButtons.
*** Code copied from FvwmIconBox.
**/
XErrorHandler oldErrorHandler=NULL;
XErrorHandler myErrorHandler(Display *dpy, XErrorEvent *event)
{
  fprintf(stderr,"%s: Cause of next X Error.\n",MyName);
  (*oldErrorHandler)(dpy,event);
  return 0;
}

/* ---------------------------------- main ----------------------------------*/

/**
*** main()
**/
void main(int argc, char **argv)
{
  char *display_name = NULL;
  int i;
  Window root;
  int x,y,maxx,maxy,border_width,depth;
  char *temp, *s;
  button_info *b,*ub;

#ifdef I18N
  setlocale(LC_CTYPE, "");
#endif
  temp=argv[0];
  s=strrchr(argv[0],'/');
  if(s) temp=s+1;
  MyName=mymalloc(strlen(temp)+1);
  strcpy(MyName,temp);

  signal(SIGPIPE,DeadPipe);
  signal(SIGINT,DeadPipe);
  signal(SIGHUP,DeadPipe);
  signal(SIGQUIT,DeadPipe);
  signal(SIGTERM,DeadPipe);

  if(argc<6 || argc>8)
    {
      fprintf(stderr,"%s v%s should only be executed by fvwm!\n",MyName,
	      VERSION);
      exit(1);
    }

  if(argc>6) /* There is a naming argument here! */
    {
      free(MyName);
      MyName=strdup(argv[6]);
    }

  if(argc>7) /* There is a config file here! */
    {
      config_file=strdup(argv[7]);
    }

  fd[0]=atoi(argv[1]);
  fd[1]=atoi(argv[2]);
  if (!(Dpy = XOpenDisplay(display_name))) 
    {
      fprintf(stderr,"%s: Can't open display %s", MyName,
	      XDisplayName(display_name));
      exit (1);
    }
  x_fd=XConnectionNumber(Dpy);
  fd_width=GetFdWidth();

  screen=DefaultScreen(Dpy);
  Root=RootWindow(Dpy, screen);
  if(Root==None) 
    {
      fprintf(stderr,"%s: Screen %d is not valid\n",MyName,screen);
      exit(1);
    }
  d_depth = DefaultDepth(Dpy, screen);

  oldErrorHandler=XSetErrorHandler((XErrorHandler)myErrorHandler);

  UberButton=(button_info*)mymalloc(sizeof(button_info));
  UberButton->flags=0;
  UberButton->parent=NULL;
  UberButton->BWidth=1;
  UberButton->BHeight=1;
  MakeContainer(UberButton);

# ifdef DEBUG_INIT
  fprintf(stderr,"%s: Parsing...",MyName);
# endif

  ParseOptions(UberButton);
  if(UberButton->c->num_buttons==0)
    {
      fprintf(stderr,"%s: No buttons defined. Quitting\n", MyName);
      exit(0);
    }

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Shuffling...",MyName);
# endif

  ShuffleButtons(UberButton);
  NumberButtons(UberButton);

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Loading data...\n",MyName);
# endif

  /* Load fonts and icons, calculate max buttonsize */
  maxx=0;maxy=0;
  InitPictureCMap(Dpy,Root); /* store the root cmap */
  RecursiveLoadData(UberButton,&maxx,&maxy);

# ifdef DEBUG_INIT
  fprintf(stderr,"%s: Creating main window...",MyName);
# endif

  CreateWindow(UberButton,maxx,maxy);

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Creating icon windows...",MyName);
# endif

  i=-1;ub=UberButton;
  while(NextButton(&ub,&b,&i,0))
    if(b->flags&b_Icon) 
      {
#ifdef DEBUG_INIT
	fprintf(stderr,"0x%06x...",(ushort)b);
#endif
	CreateIconWindow(b);
      }

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Configuring windows...",MyName);
# endif

  XGetGeometry(Dpy,MyWindow,&root,&x,&y,(ushort*)&Width,(ushort*)&Height,
	       (ushort*)&border_width,(ushort*)&depth);
  SetButtonSize(UberButton,Width,Height);
  i=-1;ub=UberButton;
  while(NextButton(&ub,&b,&i,0))
    ConfigureIconWindow(b);

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Mapping windows...",MyName);
# endif

  XMapSubwindows(Dpy,MyWindow);
  XMapWindow(Dpy,MyWindow);

  SetMessageMask(fd, M_NEW_DESK | M_END_WINDOWLIST | M_MAP | M_WINDOW_NAME |
		 M_RES_CLASS | M_CONFIG_INFO | M_END_CONFIG_INFO | M_RES_NAME);

  /* request a window list, since this triggers a response which
   * will tell us the current desktop and paging status, needed to
   * indent buttons correctly */
  SendInfo(fd[0], "Send_WindowList", 0);

# ifdef DEBUG_INIT
  fprintf(stderr,"OK\n%s: Startup complete\n",MyName);
# endif

  Loop();
}

/* -------------------------------- Main Loop -------------------------------*/

/**
*** Loop
**/
void Loop(void)
{
  XEvent Event;
  Window root;
  KeySym keysym;
  char buffer[10],*tmp,*act;
  int i,i2,x,y,tw,th,border_width,depth,button;
  button_info *ub,*b;
#ifndef OLD_EXPOSE
  int ex=10000,ey=10000,ex2=0,ey2=0;
#endif

  while(1)
    {
      if(My_XNextEvent(Dpy,&Event))
	{
	switch(Event.type)
	  {
	  case Expose:
	    if(Event.xany.window==MyWindow)
	      {
#ifdef OLD_EXPOSE
		if(Event.xexpose.count == 0)
		  {
		    button=-1;ub=UberButton;
		    while(NextButton(&ub,&b,&button,1))
		      {
			if(!ready && !(b->flags&b_Container))
			  MakeButton(b);
			RedrawButton(b,1);
		      }
		    if(!ready)
		      ready++;
		  }
#else
		ex=min(ex,Event.xexpose.x);
		ey=min(ey,Event.xexpose.y);
		ex2=max(ex2,Event.xexpose.x+Event.xexpose.width);
		ey2=max(ey2,Event.xexpose.y+Event.xexpose.height);

		if(Event.xexpose.count==0)
		  {
		    button=-1;ub=UberButton;
		    while(NextButton(&ub,&b,&button,1))
		      {
			if(b->flags&b_Container)
			  {
			    x=buttonXPos(b,buttonNum(b));
			    y=buttonYPos(b,buttonNum(b));
			  } 
			else
			  {
			    x=buttonXPos(b,button);
			    y=buttonYPos(b,button);
			  }
			if(!(ex > x + buttonWidth(b) || ex2 < x ||
			     ey > y + buttonHeight(b) || ey2 < y))
			  {
			    if(ready<1 && !(b->flags&b_Container))
			      MakeButton(b);
			    RedrawButton(b,1);
			  }
		      }
		    if(ready<1)
		      ready++;

		    ex=ey=10000;ex2=ey2=0;
		  }
#endif
              }
	    break;

	  case ConfigureNotify:
	    XGetGeometry(Dpy,MyWindow,&root,&x,&y,
			 (ushort*)&tw,(ushort*)&th,
			 (ushort*)&border_width,(ushort*)&depth);
	    if(tw!=Width || th!=Height)
	      {
		Width=tw;
		Height=th;
		SetButtonSize(UberButton,Width,Height);
		button=-1;ub=UberButton;
		while(NextButton(&ub,&b,&button,0))
		  MakeButton(b);
                RedrawWindow(NULL);
	      }
	    break;

	  case KeyPress:
	    XLookupString(&Event.xkey,buffer,10,&keysym,0);
	    if(keysym!=XK_Return && keysym!=XK_KP_Enter && keysym!=XK_Linefeed)
	      break;	                /* fall through to ButtonPress */
	  case ButtonPress:
	    CurrentButton = b = 
	      select_button(UberButton,Event.xbutton.x,Event.xbutton.y);
	    if(!b || !(b->flags&b_Action))
	      {
		CurrentButton=NULL;
		break;
	      }
	    RedrawButton(b,0);
	    if(!(act=GetButtonAction(b,Event.xbutton.button)))
	      act=GetButtonAction(b,0);
	    if(strncasecmp(act,"popup",5)!=0)
	      break;
	    else /* i.e. action is Popup */
	      XUngrabPointer(Dpy,CurrentTime); /* And fall through */

	  case KeyRelease:
	  case ButtonRelease:
	    b=select_button(UberButton,Event.xbutton.x,Event.xbutton.y);
	    if(!(act=GetButtonAction(b,Event.xbutton.button)))
	      act=GetButtonAction(b,0);
	    if(b && b==CurrentButton && act)
	      {
		if(strncasecmp(act,"Exec",4)==0)
		  {
		    /* Look for Exec "identifier", in which case the button
		       stays down until window "identifier" materializes */
		    i=4;
		    while(act[i]!=0 && act[i]!='"' &&
			  isspace((unsigned char)act[i]))
		      i++;
		    if(act[i] == '"')
		      {
			i2=i+1;
			while(act[i2]!=0 && act[i2]!='"')
			  i2++;

			if(i2-i>1)
			  {
			    b->flags|=b_Hangon;
			    b->hangon = mymalloc(i2-i);
			    strncpy(b->hangon,&act[i+1],i2-i-1);
			    b->hangon[i2-i-1] = 0;
			  }
			i2++;
		      }
		    else
		      i2=i;
		    
		    tmp=mymalloc(strlen(act));
		    strcpy(tmp,"Exec ");
		    while(act[i2]!=0 && isspace((unsigned char)act[i2]))
		      i2++;
		    strcat(tmp,&act[i2]);
		    SendInfo(fd[0], tmp, 0);
		    free(tmp);
		  }
		else if(strncasecmp(act,"DumpButtons",11)==0)
		  DumpButtons(UberButton);
		else if(strncasecmp(act,"SaveButtons",11)==0)
		  SaveButtons(UberButton);
		else
			SendInfo(fd[0], act, 0);
	      }
	    b=CurrentButton;
	    CurrentButton=NULL;
	    if(b)
	      RedrawButton(b,0);
	    break;

	  case ClientMessage:
	    if(Event.xclient.format==32 && 
	       Event.xclient.data.l[0]==_XA_WM_DEL_WIN)
	      DeadPipe(1);
	    break;

	  case PropertyNotify:
	    if(Event.xany.window==None)
	      break;
	    ub=UberButton;button=-1;
	    while(NextButton(&ub,&b,&button,0))
	      if((buttonSwallowCount(b)==3) && Event.xany.window==b->IconWin)
		{
		  if(Event.xproperty.atom==XA_WM_NAME && 
		     buttonSwallow(b)&b_UseTitle)
		    {
		      if(b->flags&b_Title) 
			free(b->title);
		      b->flags|=b_Title;
		      XFetchName(Dpy,b->IconWin,&tmp);
		      CopyString(&b->title,tmp);
		      XFree(tmp);
		      MakeButton(b);
		    }
		  else if((Event.xproperty.atom==XA_WM_NORMAL_HINTS) &&
		     (!(buttonSwallow(b)&b_NoHints)))
		    {
		      long supp;
		      if(!XGetWMNormalHints(Dpy,b->IconWin,b->hints,&supp))
			b->hints->flags = 0;
		      MakeButton(b);
		    }
		  RedrawButton(b,1);
		}
	    break;

	  case UnmapNotify: /* Not really sure if this is abandon all hope.. */
	  case DestroyNotify:
	    ub=UberButton;button=-1;
	    while(NextButton(&ub,&b,&button,0))
	      if((buttonSwallowCount(b)==3) && Event.xany.window==b->IconWin)
		{
#                 ifdef DEBUG_HANGON
		  fprintf(stderr,
			  "%s: Button 0x%06x lost its window 0x%x (\"%s\")",
			  MyName,(ushort)b,(ushort)b->IconWin,b->hangon);
#                 endif
		  b->swallow&=~b_Count;
		  b->IconWin=None;
		  if(buttonSwallow(b)&b_Respawn && b->hangon && b->spawn)
		    {
#                     ifdef DEBUG_HANGON
		      fprintf(stderr,", respawning\n");
#                     endif
		      b->swallow|=1;
		      b->flags|=b_Swallow|b_Hangon;
		      SendInfo(fd[0], b->spawn, 0);
		    }
		  else
		    {
		      b->flags&=~b_Swallow;
#                     ifdef DEBUG_HANGON
		      fprintf(stderr,"\n");
#                     endif
		    }
		  break;
		}
	    break;

	  default:
#           ifdef DEBUG_EVENTS
	    fprintf(stderr,"%s: Event fell through unhandled\n",MyName);
#           endif
	    break;
	  }
      }
    }
}

/**
*** RedrawWindow()
*** Draws the window by traversing the button tree, draws all if NULL is given,
*** otherwise only the given button.
**/
void RedrawWindow(button_info *b)
{
  int button;
  XEvent dummy;
  button_info *ub;

  if(ready<1)
    return;

  /* Flush expose events */
  while (XCheckTypedWindowEvent (Dpy, MyWindow, Expose, &dummy));

  if(b)
    {
      RedrawButton(b,0);
      return;
    }

  button=-1;ub=UberButton;
  while(NextButton(&ub,&b,&button,1))
    RedrawButton(b,1);
}

/**
*** LoadIconFile()
**/
int LoadIconFile(char *s,Picture **p)
{
  *p=CachePicture(Dpy,Root,iconPath,pixmapPath,s);
  if(*p)
    return 1;
  return 0;
}

/**
*** RecursiveLoadData()
*** Loads colors, fonts and icons, and calculates buttonsizes
**/
void RecursiveLoadData(button_info *b,int *maxx,int *maxy)
{
  int i,j,x=0,y=0;
  XFontStruct *font;
#ifdef I18N
  char **ml;
  int mc;
  char *ds;
  XFontStruct **fs_list;
  XFontSet fontset;
#endif

  if(!b) return;

#ifdef DEBUG_LOADDATA
  fprintf(stderr,"%s: Loading: Button 0x%06x: colors",MyName,(ushort)b);
#endif

  /* Load colors */
  if(b->flags&b_Fore)
    b->fc=GetColor(b->fore);
  if(b->flags&b_Back)
    {
      if(b->flags&b_IconBack)
	{
	  if(!LoadIconFile(b->back,&b->backicon))
	    b->flags&=~b_Back;
	}
      else
	{
	  b->bc=GetColor(b->back);
	  b->hc=GetHilite(b->bc);
	  b->sc=GetShadow(b->bc);
	}
    }
  if(b->flags&b_Container)
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", colors2");
#     endif
      if(b->c->flags&b_Fore)
	b->c->fc=GetColor(b->c->fore);
      if(b->c->flags&b_Back)
	{
	  if(b->c->flags&b_IconBack)
	    {
	      if(!LoadIconFile(b->c->back,&b->c->backicon))
		b->c->flags&=~b_Back;
	    }
	  else
	    {
	      b->c->bc=GetColor(b->c->back);
	      b->c->hc=GetHilite(b->c->bc);
	      b->c->sc=GetShadow(b->c->bc);
	    }
	}
    }

  /* Load the font */
  if(b->flags&b_Font)
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", font \"%s\"",b->font_string);
#     endif

      if(strncasecmp(b->font_string,"none",4)==0)
	b->font=NULL;
#ifdef I18N
      else if(!(b->fontset=GetFontSetOrFixed(Dpy,b->font_string)))
	{
	  b->font = NULL;
	  b->flags&=~b_Font;
	  fprintf(stderr,"%s: Couldn't load fontset %s\n",MyName,
		  b->font_string);
	}
      else
	{
	  /* fontset found */
	  XFontsOfFontSet(b->fontset, &fs_list, &ml);
	  b->font = fs_list[0];
	}
#else
      else if(!(b->font=XLoadQueryFont(Dpy,b->font_string)))
	{
	  b->flags&=~b_Font;
	  fprintf(stderr,"%s: Couldn't load font %s\n",MyName,
		  b->font_string);
	}
#endif
    }

  if(b->flags&b_Container && b->c->flags&b_Font)
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", font2 \"%s\"",b->c->font_string);
#     endif
      if(strncasecmp(b->c->font_string,"none",4)==0)
	b->c->font=NULL;
#ifdef I18N
      else if(!(b->c->fontset=GetFontSetOrFixed(Dpy,b->c->font_string)))
	{
	  fprintf(stderr,"%s: Couldn't load fontset %s\n",MyName,
		  b->c->font_string);
	  if(b==UberButton)
	    {
	      if(!(b->c->fontset=XCreateFontSet(Dpy,"fixed",&ml,&mc,&ds))) {
		fprintf(stderr,"%s: Couldn't load fontset fixed\n",MyName);
		b->c->font = NULL;
	      }
	    }
	  else {
	    b->c->font = NULL;
	    b->c->flags&=~b_Font;
	  }
	}
      else
	{
	  /* fontset found */
	  XFontsOfFontSet(b->c->fontset, &fs_list, &ml);
	  b->c->font = fs_list[0];
	}
#else
      else if(!(b->c->font=XLoadQueryFont(Dpy,b->c->font_string)))
	{
	  fprintf(stderr,"%s: Couldn't load font %s\n",MyName,
		  b->c->font_string);
	  if(b==UberButton)
	    {
	      if(!(b->c->font=XLoadQueryFont(Dpy,"fixed")))
		fprintf(stderr,"%s: Couldn't load font fixed\n",MyName);
	    }
	  else
	    b->c->flags&=~b_Font;
	}
#endif
    }



  /* Calculate subbutton sizes */
  if(b->flags&b_Container && b->c->num_buttons)
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", entering container\n");
#     endif
      for(i=0;i<b->c->num_buttons;i++)
	if(b->c->buttons[i])
	  RecursiveLoadData(b->c->buttons[i],&x,&y);

      if(b->c->flags&b_Size)
	{
	  x=b->c->minx;
	  y=b->c->miny;
	}
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,"%s: Loading: Back to container 0x%06x",MyName,(ushort)b);
#     endif

      b->c->ButtonWidth=x;
      b->c->ButtonHeight=y;
      x*=b->c->num_columns;
      y*=b->c->num_rows;
    }


  
  i=0;j=0;

  /* Load the icon */
  if(b->flags&b_Icon && LoadIconFile(b->icon_file,&b->icon))
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", icon \"%s\"",b->icon_file);
#     endif
      i=b->icon->width;
      j=b->icon->height;
    }
  else
    b->flags&=~b_Icon;

#ifdef I18N
  if(b->flags&b_Title && (fontset = buttonFontSet(b)) && (font=buttonFont(b)))
#else
  if(b->flags&b_Title && (font=buttonFont(b)))
#endif
    {
#     ifdef DEBUG_LOADDATA
      fprintf(stderr,", title \"%s\"",b->title);
#     endif
      if(buttonJustify(b)&b_Horizontal)
	{
	  i+=buttonXPad(b)+XTextWidth(font,b->title,strlen(b->title));
	  j=max(j,font->ascent+font->descent);
	}
      else
	{
	  i=max(i,XTextWidth(font,b->title,strlen(b->title)));
	  j+=font->ascent+font->descent;
	}
    }

  x+=i;
  y+=j;

  if(b->flags&b_Size)
    {
      x=b->minx;
      y=b->miny;
    }

  x+=2*(buttonFrame(b)+buttonXPad(b));
  y+=2*(buttonFrame(b)+buttonYPad(b));
  
  x/=b->BWidth;
  y/=b->BHeight;

  *maxx=max(x,*maxx);
  *maxy=max(y,*maxy);
# ifdef DEBUG_LOADDATA
  fprintf(stderr,", size %ux%u, done\n",x,y);
# endif
}

/**
*** CreateWindow()
*** Sizes and creates the window 
**/
void CreateWindow(button_info *ub,int maxx,int maxy)
{
  XSizeHints mysizehints;
  XGCValues gcv;
  unsigned long gcm;
  XClassHint myclasshints;

  if(maxx<16)
    maxx=16;
  if(maxy<16)
    maxy=16;

# ifdef DEBUG_INIT
  fprintf(stderr,"making atoms...");
# endif

  _XA_WM_DEL_WIN = XInternAtom(Dpy,"WM_DELETE_WINDOW",0);
  _XA_WM_PROTOCOLS = XInternAtom (Dpy, "WM_PROTOCOLS",0);

# ifdef DEBUG_INIT
  fprintf(stderr,"sizing...");
# endif

  mysizehints.flags = PWinGravity | PResizeInc | PBaseSize;

  mysizehints.base_width=mysizehints.base_height=0;

/* This should never be executed anyway, let's remove it.
  if(ub->flags&b_Frame)
    {
      mysizehints.base_width+=2*abs(ub->framew);
      mysizehints.base_height+=2*abs(ub->framew);
    }
  if(ub->flags&b_Padding)
    {
      mysizehints.base_width+=2*ub->xpad;
      mysizehints.base_height+=2*ub->ypad;
    }
*/
  mysizehints.width=mysizehints.base_width+maxx;
  mysizehints.height=mysizehints.base_height+maxy;
  mysizehints.width_inc=ub->c->num_columns;
  mysizehints.height_inc=ub->c->num_rows;
  mysizehints.base_height+=ub->c->num_rows*2;
  mysizehints.base_width+=ub->c->num_columns*2;

  if(w>-1) /* from geometry */
    {
# ifdef DEBUG_INIT
  fprintf(stderr,"constraining (w=%i)...",w);
# endif
      ConstrainSize(&mysizehints,&w,&h);
      mysizehints.width = w;
      mysizehints.height = h;
      mysizehints.flags |= USSize;
    }

# ifdef DEBUG_INIT
  fprintf(stderr,"gravity...");
# endif
  mysizehints.x=0;
  mysizehints.y=0;
  if(x > -30000)
    {
      if (xneg)
	{
	  mysizehints.x = DisplayWidth(Dpy,screen) + x - mysizehints.width;
	  gravity = NorthEastGravity;
	}
      else 
	mysizehints.x = x;
      if (yneg)
	{
	  mysizehints.y = DisplayHeight(Dpy,screen) + y - mysizehints.height;
	  gravity = SouthWestGravity;
	}
      else 
	mysizehints.y = y;
      if(xneg && yneg)
	gravity = SouthEastGravity;	
      mysizehints.flags |= USPosition;
    }
  mysizehints.win_gravity = gravity;

# ifdef DEBUG_INIT
  fprintf(stderr,"colors...");
# endif
  if(d_depth < 2)
    {
      back_pix = GetColor("white");
      fore_pix = GetColor("black");
      hilite_pix = back_pix;
      shadow_pix = fore_pix;
    }
  else
    {
      back_pix = GetColor(ub->c->back);
      fore_pix = GetColor(ub->c->fore);
      hilite_pix = GetHilite(back_pix);
      shadow_pix = GetShadow(back_pix);
    }
  
# ifdef DEBUG_INIT
  if(mysizehints.flags&USPosition)
    fprintf(stderr,"create(%i,%i,%u,%u,1,%u,%u)...",
	    mysizehints.x,mysizehints.y,
	    mysizehints.width,mysizehints.height,
	    (ushort)fore_pix,(ushort)back_pix);
  else
    fprintf(stderr,"create(-,-,%u,%u,1,%u,%u)...",
	    mysizehints.width,mysizehints.height,
	    (ushort)fore_pix,(ushort)back_pix);
# endif

  MyWindow = XCreateSimpleWindow(Dpy,Root,mysizehints.x,mysizehints.y,
				 mysizehints.width,mysizehints.height,
				 1,fore_pix,back_pix);
# ifdef DEBUG_INIT
  fprintf(stderr,"properties...");
# endif
  XSetWMProtocols(Dpy,MyWindow,&_XA_WM_DEL_WIN,1);

  myclasshints.res_name=strdup(MyName);
  myclasshints.res_class=strdup("FvwmButtons");

  {
    XTextProperty mynametext;
    char *list[]={NULL,NULL};
    list[0]=MyName;
    if(!XStringListToTextProperty(list,1,&mynametext))
      {
	fprintf(stderr,"%s: Failed to convert name to XText\n",MyName);
	exit(1);
      }
    XSetWMProperties(Dpy,MyWindow,&mynametext,&mynametext,
		     NULL,0,&mysizehints,NULL,&myclasshints);
    XFree(mynametext.value);
  }

  XSelectInput(Dpy,MyWindow,MW_EVENTS);

# ifdef DEBUG_INIT
  fprintf(stderr,"GC...");
# endif
  gcm = GCForeground|GCBackground;
  gcv.foreground = fore_pix;
  gcv.background = back_pix;
  if(ub->font)
    {
      gcv.font = ub->c->font->fid;
      gcm |= GCFont;
    }
  NormalGC = XCreateGC(Dpy, Root, gcm, &gcv);  
}

/* ----------------------------- color functions --------------------------- */

#ifdef XPM
#  define MyAllocColor(a,b,c) PleaseAllocColor(c)
#else
#  define MyAllocColor(a,b,c) XAllocColor(a,b,c)
#endif

/**
*** PleaseAllocColor()
*** Allocs a color through XPM, which does it's closeness thing if out of
*** space.
**/
#ifdef XPM
int PleaseAllocColor(XColor *color)
{
  char *xpm[] = {"1 1 1 1",NULL,"x"};
  XpmAttributes attr;
  XImage *dummy1=None,*dummy2=None;
  static char buf[20];

  sprintf(buf,"x c #%04x%04x%04x",color->red,color->green,color->blue);
  xpm[1]=buf;
  attr.valuemask=XpmCloseness;
  attr.closeness=40000; /* value used by fvwm and fvwmlib */

  if(XpmCreateImageFromData(Dpy,xpm,&dummy1,&dummy2,&attr)!=XpmSuccess)
    {
      fprintf(stderr,"%s: Unable to get similar color\n",MyName);
      exit(1);
    }
  color->pixel=XGetPixel(dummy1,0,0);
  if(dummy1!=None)XDestroyImage(dummy1);
  if(dummy2!=None)XDestroyImage(dummy2);
  return 1;
}
#endif

/**
*** GetShadow()
*** This routine computes the shadow color from the background color
**/
Pixel GetShadow(Pixel background) 
{
  XColor bg_color;
  XWindowAttributes attributes;
  
  XGetWindowAttributes(Dpy,Root,&attributes);
  
  bg_color.pixel = background;
  XQueryColor(Dpy,attributes.colormap,&bg_color);
  
  bg_color.red = (unsigned short)((bg_color.red*50)/100);
  bg_color.green = (unsigned short)((bg_color.green*50)/100);
  bg_color.blue = (unsigned short)((bg_color.blue*50)/100);
  
  if(!MyAllocColor(Dpy,attributes.colormap,&bg_color))
    nocolor("alloc shadow","");
  
  return bg_color.pixel;
}

/**
*** GetHilite()
*** This routine computes the hilight color from the background color
**/
Pixel GetHilite(Pixel background) 
{
  XColor bg_color, white_p;
  XWindowAttributes attributes;
  
  XGetWindowAttributes(Dpy,Root,&attributes);
  
  bg_color.pixel = background;
  XQueryColor(Dpy,attributes.colormap,&bg_color);

  white_p.pixel = GetColor("white");
  XQueryColor(Dpy,attributes.colormap,&white_p);
  
  bg_color.red = max((white_p.red/5), bg_color.red);
  bg_color.green = max((white_p.green/5), bg_color.green);
  bg_color.blue = max((white_p.blue/5), bg_color.blue);
  
  bg_color.red = min(white_p.red, (bg_color.red*140)/100);
  bg_color.green = min(white_p.green, (bg_color.green*140)/100);
  bg_color.blue = min(white_p.blue, (bg_color.blue*140)/100);
  
  if(!MyAllocColor(Dpy,attributes.colormap,&bg_color))
    nocolor("alloc hilight","");
  
  return bg_color.pixel;
}

/**
*** nocolor()
*** Complain 
**/
void nocolor(char *a, char *b)
{
  fprintf(stderr,"%s: Can't %s %s, quitting, sorry...\n", MyName, a,b);
  exit(1);
}

/**
*** GetColor()
*** Loads a single color
**/
Pixel GetColor(char *name)
{
  XColor color;
  XWindowAttributes attributes;

  XGetWindowAttributes(Dpy,Root,&attributes);
  color.pixel = 0;
  if (!XParseColor (Dpy, attributes.colormap, name, &color)) 
    nocolor("parse",name);
  else if(!MyAllocColor(Dpy,attributes.colormap,&color))
    nocolor("alloc",name);
  return color.pixel;
}


/* --------------------------------------------------------------------------*/

#ifdef DEBUG_EVENTS
void DebugEvents(XEvent *event)
{
  char *event_names[]={NULL,NULL,
			 "KeyPress","KeyRelease","ButtonPress",
			 "ButtonRelease","MotionNotify","EnterNotify",
			 "LeaveNotify","FocusIn","FocusOut",
			 "KeymapNotify","Expose","GraphicsExpose",
			 "NoExpose","VisibilityNotify","CreateNotify",
			 "DestroyNotify","UnmapNotify","MapNotify",
			 "MapRequest","ReparentNotify","ConfigureNotify",
			 "ConfigureRequest","GravityNotify","ResizeRequest",
			 "CirculateNotify","CirculateRequest","PropertyNotify",
			 "SelectionClear","SelectionRequest","SelectionNotify",
			 "ColormapNotify","ClientMessage","MappingNotify"};
  fprintf(stderr,"%s: Received %s event from window 0x%x\n",
	  MyName,event_names[event->type],(ushort)event->xany.window);
}
#endif

#ifdef DEBUG_FVWM
void DebugFvwmEvents(unsigned long type)
{
  char *events[]={
"M_NEW_PAGE",
"M_NEW_DESK",
"M_ADD_WINDOW",
"M_RAISE_WINDOW",
"M_LOWER_WINDOW",
"M_CONFIGURE_WINDOW",
"M_FOCUS_CHANGE",
"M_DESTROY_WINDOW",
"M_ICONIFY",
"M_DEICONIFY",
"M_WINDOW_NAME",
"M_ICON_NAME",
"M_RES_CLASS",
"M_RES_NAME",
"M_END_WINDOWLIST",
"M_ICON_LOCATION",
"M_MAP",
"M_ERROR",
"M_CONFIG_INFO",
"M_END_CONFIG_INFO",
"M_ICON_FILE",
"M_DEFAULTICON",NULL};
  int i=0;
  while(events[i])
    {
      if(type&1<<i)
	fprintf(stderr,"%s: Received %s message from fvwm\n",MyName,events[i]);
      i++;
    }
}
#endif

/**
*** My_XNextEvent()
*** Waits for next X event, or for an auto-raise timeout.
**/
int My_XNextEvent(Display *Dpy, XEvent *event)
{
  fd_set in_fdset;
  unsigned long header[HEADER_SIZE];
  int count;
  static int miss_counter = 0;
  unsigned long *body;

  if(XPending(Dpy))
    {
      XNextEvent(Dpy,event);
#     ifdef DEBUG_EVENTS
      DebugEvents(event);
#     endif
      return 1;
    }

  FD_ZERO(&in_fdset);
  FD_SET(x_fd,&in_fdset);
  FD_SET(fd[1],&in_fdset);

#ifdef __hpux
  select(fd_width,(int *)&in_fdset, 0, 0, NULL);
#else
  select(fd_width,&in_fdset, 0, 0, NULL);
#endif


  if(FD_ISSET(x_fd, &in_fdset))
    {
      if(XPending(Dpy))
	{
	  XNextEvent(Dpy,event);
	  miss_counter = 0;
#         ifdef DEBUG_EVENTS
	  DebugEvents(event);
#         endif
	  return 1;
	}
      else
	miss_counter++;
      if(miss_counter > 100)
	DeadPipe(0);
    }
  
  if(FD_ISSET(fd[1], &in_fdset))
    {
      if((count = ReadFvwmPacket(fd[1], header, &body)) > 0)
	{
	  process_message(header[1],body);
	  free(body);
	}
    }
  return 0;
}

/**
*** SpawnSome()
*** Is run after the windowlist is checked. If any button hangs on UseOld,
*** it has failed, so we try to spawn a window for them.
**/
void SpawnSome()
{
  static char first=1;
  button_info *b,*ub=UberButton;
  int button=-1;
  if(!first)
    return;
  first=0;
  while(NextButton(&ub,&b,&button,0))
    if((buttonSwallowCount(b)==1) && b->flags&b_Hangon && 
       buttonSwallow(b)&b_UseOld)
      if(b->spawn)
	{
#         ifdef DEBUG_HANGON
	  fprintf(stderr,"%s: Button 0x%06x did not find a \"%s\" window, %s",
		  MyName,(ushort)b,b->hangon,"spawning own\n");
#         endif
	  SendInfo(fd[0], b->spawn, 0);
	}
}

/**
*** process_message()
*** Process window list messages 
**/
void process_message(unsigned long type,unsigned long *body)
{
# ifdef DEBUG_FVWM
  DebugFvwmEvents(type);
# endif
  switch(type)
    {
    case M_NEW_DESK:
      new_desk = body[0];
      RedrawWindow(NULL);
      break;
    case M_END_WINDOWLIST:
      SpawnSome();
      RedrawWindow(NULL);
      break;
    case M_MAP:
      swallow(body);
      break;
    case M_RES_NAME:
    case M_RES_CLASS:
    case M_WINDOW_NAME:
      CheckForHangon(body);
      break;
    default:
      break;
    }
}


/**
*** send_clientmessage()
***
*** ICCCM Client Messages - Section 4.2.8 of the ICCCM dictates that all
*** client messages will have the following form:
***
***     event type	ClientMessage
***     message type	_XA_WM_PROTOCOLS
***     window		w
***     format		32
***     data[0]		message atom
***     data[1]		time stamp
**/
void send_clientmessage (Window w, Atom a, Time timestamp)
{
  XClientMessageEvent ev;
  
  ev.type = ClientMessage;
  ev.window = w;
  ev.message_type = _XA_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = a;
  ev.data.l[1] = timestamp;
  XSendEvent (Dpy, w, 0, 0L, (XEvent *) &ev);
}


/* --------------------------------- swallow code -------------------------- */

/**
*** CheckForHangon()
*** Is the window here now?
**/
void CheckForHangon(unsigned long *body)
{
  button_info *b,*ub=UberButton;
  int button=-1;
  ushort d;
  char *cbody;
  cbody = (char *)&body[3];

  while(NextButton(&ub,&b,&button,0))
    if(b->flags&b_Hangon && strcmp(cbody,b->hangon)==0)
      {
	/* Is this a swallowing button in state 1? */
	if(buttonSwallowCount(b)==1)
	  {
	    b->swallow&=~b_Count;
	    b->swallow|=2;
	    b->IconWin=(Window)body[0];
	    b->flags&=~b_Hangon;

            /* We get the parent of the window to compare with later... */
            b->IconWinParent=
	      GetRealGeometry(Dpy,b->IconWin,
			      &b->x,&b->y,&b->w,&b->h,&b->bw,&d);

#           ifdef DEBUG_HANGON
	    fprintf(stderr,"%s: Button 0x%06x %s 0x%lx \"%s\", parent 0x%lx\n",
		    MyName,(ushort)b,"will swallow window",body[0],cbody,
		    b->IconWinParent);
#           endif

	    if(buttonSwallow(b)&b_UseOld)
	      swallow(body);
	  }
	else
	  {
	    /* Else it is an executing button with a confirmed kill */
#           ifdef DEBUG_HANGON
	    fprintf(stderr,"%s: Button 0x%06x %s 0x%lx \"%s\", released\n",
		    MyName,(int)b,"hung on window",body[0],cbody);
#           endif
	    b->flags&=~b_Hangon;
	    free(b->hangon);
	    b->hangon=NULL;
	    RedrawButton(b,0);
  
	  }
	break;
      }
    else if(buttonSwallowCount(b)>=2 && (Window)body[0]==b->IconWin)
      break;      /* This window has already been swallowed by someone else! */
}

/** 
*** GetRealGeometry()
*** Traverses window tree to find the real x,y of a window. Any simpler?
*** Returns parent window, or None if failed.
**/
Window GetRealGeometry(Display *dpy,Window win,int *x,int *y,ushort *w,
		     ushort *h,ushort *bw,ushort *d)
{
  Window root;
  Window rp=None;
  Window *children;
  int n;

  if(!XGetGeometry(dpy,win,&root,x,y,w,h,bw,d))
    return None;

  /* Now, x and y are not correct. They are parent relative, not root... */
  /* Hmm, ever heard of XTranslateCoordinates!? */

  XTranslateCoordinates(dpy,win,root,*x,*y,x,y,&rp);

  XQueryTree(dpy,win,&root,&rp,&children,&n); 
  XFree(children);

  return rp;
}

/**
*** swallow()
*** Executed when swallowed windows get mapped
**/
void swallow(unsigned long *body)
{
  char *temp;
  button_info *ub=UberButton,*b;
  int button=-1;
  ushort d;
  Window p;

  while(NextButton(&ub,&b,&button,0))
    if((b->IconWin==(Window)body[0]) && (buttonSwallowCount(b)==2))
      {
	/* Store the geometry in case we need to unswallow. Get parent */
	p=GetRealGeometry(Dpy,b->IconWin,&b->x,&b->y,&b->w,&b->h,&b->bw,&d);
#       ifdef DEBUG_HANGON
	fprintf(stderr,"%s: Button 0x%06x %s 0x%lx, with parent 0x%lx\n",
		MyName,(ushort)b,"trying to swallow window",body[0],p);
#       endif

	if(p==None) /* This means the window is no more */ /* NO! wrong */
	  {
	    fprintf(stderr,"%s: Window 0x%lx (\"%s\") disappeared %s\n",
		    MyName,b->IconWin,b->hangon,"before swallow complete");
	    /* Now what? Nothing? For now: give up that button */
	    b->flags&=~(b_Hangon|b_Swallow);
	    return;
	  }

	if(p!=b->IconWinParent) /* The window has been reparented */
	  {
	    fprintf(stderr,"%s: Window 0x%lx (\"%s\") was %s (window 0x%lx)\n",
		    MyName,b->IconWin,b->hangon,"grabbed by someone else",p);

	    /* Request a new windowlist, we might have ignored another 
	       matching window.. */
	    SendInfo(fd[0], "Send_WindowList", 0);

	    /* Back one square and lose one turn */
	    b->swallow&=~b_Count;
	    b->swallow|=1;
	    b->flags|=b_Hangon;
	    return;
	  }

#       ifdef DEBUG_HANGON
	fprintf(stderr,"%s: Button 0x%06x swallowed window 0x%lx\n",
		MyName,(ushort)b,body[0]);
#       endif

	b->swallow&=~b_Count;
	b->swallow|=3;
	
	/* "Swallow" the window! Place it in the void so we don't see it
	   until it's MoveResize'd */
	XReparentWindow(Dpy,b->IconWin,MyWindow,-1500,-1500); 
	XSelectInput(Dpy,b->IconWin,SW_EVENTS);
	if(buttonSwallow(b)&b_UseTitle)
	  {
	    if(b->flags&b_Title)
	      free(b->title);
	    b->flags|=b_Title;
	    XFetchName(Dpy,b->IconWin,&temp);
	    CopyString(&b->title,temp);
	    XFree(temp);
	  }
	XMapWindow(Dpy,b->IconWin);
	MakeButton(b);
	RedrawButton(b,1);
	break;
      }
}

