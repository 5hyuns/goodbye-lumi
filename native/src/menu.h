/* The title card, the wake, and the one flag every string in the game
   answers to.  See menu.c. */
#ifndef MENU_H
#define MENU_H

enum { LANG_KO=0, LANG_EN, LANG_JA, LANG_N };

void  menuInit(void);
void  menuReset(void);      /* the ending puts the card back up */
void  menuStep(float dt);
void  menuDraw(void);                  /* the card AND the lids */
void  menuKey(int vk,int down);
void  menuClick(int down);
void  menuCursor(float x,float y);     /* window pixels, top-left origin */

int   menuActive(void);                /* the card is up: hold the director */
int   menuOver(void);                  /* the eyes are open, play */
int   menuLang(void);
float menuLids(void);                  /* 0 open, 1 shut */
/* the whole of the localisation contract: pick one of three, by the flag.
   A missing translation falls back to the KOREAN, never to blank -- the
   story was written in Korean and the Korean is the one that has been
   timed against SL_CPS. */
const char *menuPick(const char *ko,const char *en,const char *ja);

#endif
