/* ============================================================
   THE THING IN THE SHIP.

   One animal, four sizes.  The previz built a six-legged chitin
   hunter out of the creature graph because the entity renderer
   draws rigid parts in one flat colour each -- a poor model of
   skin, an exact model of an arthropod -- and the story asks for
   exactly that animal in four roles: the scuttler that swarms,
   the spitter that sits on the ceiling, the brooder that breaks
   walls, and the cocoon that makes more of them.

   Everything here is the previz's G4, G8, G8b and G15, ported.
   ============================================================ */
#ifndef ZOM_H
#define ZOM_H

enum { Z_SCUTTLER=0, Z_SPITTER, Z_BROODER, Z_COCOON, Z_NTYPE };

void  zomInit(void);                 /* the nav lattice; call after the bake */
void  zomReset(void);
int   zomSpawn(int type,float x,float y,float z,float yaw,int enc);
void  zomStep(float dt);
void  zomDraw(void);                 /* instances, before entDraw */
void  zomTarget(float x,float y,float z,int visible);

int   zomLive(void);                 /* how many are up and hunting */
int   zomLiveIn(int enc);            /* ... belonging to one encounter */
int   zomCount(void);                /* including corpses, for the cap */
void  zomKillAll(void);
int   zomKills(void);
int   zomKillsOf(int type);          /* ... of one of the four */
/* the deaths nobody has looked at yet, oldest first.  Returns type+1 and
   fills x,y,z; 0 when the ring is empty.  One reader only -- story.c. */
int   zomTakeDeath(float *xyz);

/* the hitscan.  Returns the index hit, or -1; t is the distance and head
   says the shot found the cranium (x2.5, the weak point --
   on this animal that is the lit underbelly and the eyes). */
int   zomTrace(float ox,float oy,float oz,float dx,float dy,float dz,
               float range,float *t,int *head);
void  zomHurt(int i,float dmg,float px,float py,float pz,
              float dx,float dy,float dz,int head,int blast);
void  zomPos(int i,float *out);
void  zomGoal(float x,float y,float z);     /* -auto: solve a route to here */
float zomGoalDist(float x,float y,float z);
/* is there a route FROM a TO b -- the lattice is directed, so this is not
   the same question as zomGoalDist and the difference is a dead objective */
int   zomRouteExists(float ax,float ay,float az,float bx,float by,float bz);
float zomTypeSize(int type);
int   zomStepTo(float x,float y,float z,float *dirXZ);
/* the drone's own field: hers is a different goal from the animals' and
   from the quest's, so it is a third solve rather than a shared one */
void  zomFlyGoal(float x,float y,float z,float dt);
int   zomFlyStep(float x,float y,float z,float *dirXZ);
void  zomDump(void);
int   zomNearest(float x,float z,float *out);  /* xyz + distance */
float zomFieldAt(float x,float y,float z);  /* flow-field cost to the player */
void  zomPerch(int i);               /* CEILING mount: it does not fall */

/* the player, as the creature sees it.  game.c owns the real one. */
extern void zomHurtPlayer(float dmg,float sx,float sy,float sz);

/* the spitter's acid, drawn and stepped with the creatures that threw it */
int   zomBoltCount(void);

#endif
