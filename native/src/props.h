/* Room props: the bench's own 37 meshes (props/props_v02/, ported into
   propkit.c + props_gen.c), placed by solving rather than by typing
   coordinates onto a drawing that is not the ship.  See props.c. */
#ifndef PROPS_H
#define PROPS_H

/* the 37, by the index props_gen.c builds them at -- the handful the rest
   of the game names directly.  PK_NAME[] is the full list. */
enum { PROP_SUPPLY_CRATE = 1,
       PROP_ENERGY_CORE  = 33,
       PROP_PACK_SLOTS   = 34,   /* Energy Pack Interface: the gallery console */
       PROP_CAPSULE      = 35,   /* Escape Capsule Hatch */
       PROP_BAY_PLATFORM = 36 };

/* build all 37 meshes and hand their material groups to prim.c.  Called by
   propsInit; separate only because primInit has to have run first. */
void propMeshInit(void);

/* call AFTER the doors exist and the items have settled: the placer asks
   both where they are, and refuses any spot that would stand furniture in
   a doorway or on top of a pickup. */
void propsInit(void);
void propKeepClear(float x,float z);      /* a point the story steers by */
void propStoryReset(void);                /* ... and storyInit re-runs */
void propsDraw(float px,float pz);        /* into the entity buffer */
/* the contact shadow the bake could not know about -- call it after
   the props are placed and BEFORE renderUpload(). */
void propShadowBake(void);
int  propBoxSolid(float,float,float,float,float,float);
/* delete every prop overlapping this walking cylinder.  storyClearRoutes
   calls it along every route the story sends the player down. */
int  propClearAt(float x,float y,float z,float r);

/* the story's own fixtures: placed where the director says, never deleted
   by the route walk, and carrying no collision box because the player has
   to be able to stand on the point they mark. */
void propAddFixed(int prop,float x,float y,float z,float yaw);
/* and one anywhere, at any size -- the pickups are drawn with this */
float propSpan(int prop);                 /* the longest side */
void  propCardLight(float k);              /* -prop: the bench key */
void propDrawAt(int prop,float x,float y,float z,float yaw,float scale,
                float emisK);
float propHeight(int prop);
float propWidth (int prop);
float propDepth (int prop);

#endif
