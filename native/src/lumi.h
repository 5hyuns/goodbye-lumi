/* ============================================================
   LUMI.  A 0.35 m maintenance drone, and the three things the
   player cannot do without her: she is the light, she is the
   health readout, and she is the only thing in the ship that
   opens a door.

   All three go at once when she docks, and that loss IS the
   final act's difficulty, deliberately.
   ============================================================ */
#ifndef LUMI_H
#define LUMI_H

enum { LM_LEAD=0, LM_HUD, LM_PANEL, LM_DOCKED, LM_OFF };
enum { LD_EYES_IDLE=0, LD_HEARTBEAT, LD_VITALS, LD_EYES_TALK, LD_SCAN,
       LD_OPEN, LD_COUNT, LD_WARN, LD_SPEAKER, LD_RADIO, LD_OFF };

void  lumiInit(void);
void  lumiStep(float dt,const float *eye,float yaw,float pitch,int combat);
void  lumiDraw(void);
void  lumiMode(int m);
int   lumiModeNow(void);
void  lumiDisplay(int d);
int   lumiDisplayNow(void);
void  lumiPanelAt(float x,float y,float z);   /* fly to this and hold */
void  lumiLeadTo(float x,float y,float z,int valid); /* a point ON the route */
void  lumiCard(float dist,int disp);          /* -lumi: the bench pose */
void  lumiDock(float x,float y,float z);      /* S9: and never move again */
void  lumiSetCount(int n);                    /* the countdown face */
void  lumiSetVitals(float hpFrac,float bpm);
void  lumiSetOpen(float u);                   /* the OPEN gauge, 0..1 */
void  lumiSetTalk(float u);                   /* syllable energy, 0..1 */
void  lumiPos(float *o);
void  lumiAt(float x,float y,float z);        /* put her somewhere, now */
int   lumiReadyAt(float x,float z,float r);   /* is she close enough to work */
void  lumiTrace(void);                        /* -log: pin / inside / stall */
void  lumiLight(void *frameState);            /* her spotlight, into a slot */

#endif
