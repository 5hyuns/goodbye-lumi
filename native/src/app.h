/* The seam between the platform and the ship.  plat.c owns the window,
   the context and the clock; everything below this line is the game. */
#ifndef APP_H
#define APP_H

/* implemented by the game ------------------------------------------- */
int  appInit(void);                     /* after the GL context exists; 0 = fail */
void appFrame(double t,double dt);      /* seconds since start, seconds since last */
void appResize(int w,int h);
void appKey(int vk,int down);           /* VK_* */
void appChar(int ch);
void appMouse(float dx,float dy);       /* relative, only while locked */
void appCursor(float x,float y);        /* absolute, only while NOT locked */
void appButton(int btn,int down);       /* 0 L, 1 R, 2 M */
void appWheel(float d);

/* offered by the platform ------------------------------------------- */
extern int   platW, platH;              /* drawable size in pixels */
extern int   platLocked;                /* mouse captured and hidden */
extern char  platFail[512];             /* set before returning 0 from appInit */
void  platSetLock(int on);
void  platQuit(void);
void  platTitle(const char *s);
double platTime(void);                  /* seconds, monotonic */
void  platLog(const char *fmt,...);     /* boot log -> stderr + debugger */
int   platArg(const char *flag);       /* "-fast" style switches on the cmdline */
const char *platCmdline(void);
int   platCores(void);
/* run fn(i, ctx) for i in [0,n) across every core, and wait */
void  platParallel(void (*fn)(int,void*), int n, void *ctx);

#endif
