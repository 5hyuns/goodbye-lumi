/* ============================================================
   10b. WHAT THE SOUNDS ARE CALLED

   audio.c's older half is declared where it is used, one extern
   at the top of each caller, and that was fine while every sound
   took a frequency.  These do not: a mechanism is named by the
   PART THAT MOVED, not by a pitch, and a name only means
   something if both sides agree on it.  So the vocabulary lives
   here, the way fx.h holds PK_SPARK.
   ============================================================ */
#ifndef AUDIO_H
#define AUDIO_H

/* ---- a machined part doing the one thing it does --------------------
   `w` is the MECHANISM'S MASS, 1.0 being a service pistol: 0.72 for the
   SMG, 1.30 for the double-barrel, 1.05 for the rotary.  It moves pitch
   and length together and nothing else, for sfxCreature's reason -- a
   heavier part is slower and lower, not merely louder. */
enum {
  MCH_DRY = 0,   /* the striker falling on an empty chamber */
  MCH_MAG_OUT,   /* the catch, and a magazine clearing the well */
  MCH_MAG_IN,    /* ... and the next one arriving */
  MCH_SLIDE,     /* the slide or the bolt going into battery */
  MCH_EJECT,     /* a case leaving, and landing a quarter-second later */
  MCH_CYCLE,     /* the mechanism moving with nothing in it */
  MCH_BREAK,     /* the double-barrel closing: the heaviest in the ship */
  MCH_READY,     /* the reload finished -- the gun saying so, quietly */
  MCH_SEIZE,     /* the rotary's trigger on a cooked chamber */
  MCH_DRAW,      /* a weapon coming up off the sling */
  MCH_N
};
void auMech(int what,float w);

/* ---- picking a thing up ----------------------------------------------
   `kind` is story.c's own SI_KIND, so the call site does not translate:
   0 medical, 1 a weapon off its rack, 2 ammunition, 3 an energy pack,
   4 the respawn crates of the last stand. */
void auPickup(int kind);

/* the player being hit.  `dmg` in hit points, so a scratch and a mauling
   are not the same sound. */
void auHurt(float dmg);

/* the console: 0 the selection moving, 1 taken, 2 backed out of */
enum { UI_MOVE=0, UI_OK, UI_BACK };
void auUI(int kind);

#endif
