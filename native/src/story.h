/* ============================================================
   THE DIRECTOR.  Five systems and a table -- flags, an event
   bus, trigger volumes, a subtitle player and a spawn director
   -- with the story itself in story_data.h rather than in code.
   ============================================================ */
#ifndef STORY_H
#define STORY_H

void  storyInit(void);
void  storyStep(float dt);
void  storyHUD(void);
void  storyEvent(const char *ev);
void  storyInteract(void);              /* the E key */
void  storyFrame(void *frameState);     /* red emergency lighting, fog */

int   storyLockLook(void);
int   storyLockMove(void);
float storyCamY(void);                  /* S0 has you on the floor */
float storyCamRoll(void);               /* the hull, going over */
float storySpeedScale(float moveYawX,float moveYawZ);
int   storyOver(void);
/* the credits are up, her last line has played, and the way back to the
   title card is on screen and waiting for a key or a click */
int   storyEndMenu(void);
int   storyEndHit(float x,float y);                  /* the credits are up */
const char *storyFail(void);
void  storyDrawWorld(void);             /* the items, into the entity buffer */
/* the weapon pickups, which are the viewmodel meshes and need its program */
void  storyDrawGuns(const void *frameState,const float *proj,const float *view);
void  storySetTrace(int on);            /* -log: every event, as it fires */
int   storyAutoTarget(float *outXZ);    /* -auto: where the story is pointing */
int   storyAutoInteract(void);
void  storyProbe(const char *arg);      /* -probe: is that point standable */
/* run after propsInit: walk every route and delete what stands on one */
void  storyClearRoutes(void);
void  storyFreeCam(int on);             /* -cam: leave the camera alone */
void  storyScene(int n);                /* -scene N: start at the head of one */
void  storyDamaged(void);               /* the vignette, when something lands */

#endif
