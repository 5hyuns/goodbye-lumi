/* The seam between the man and the gun.  game.c owns the player; vmw.c
   owns everything the weapon is and does.  The two things that cross
   back are the recoil that climbs the view and the shake it throws. */
#ifndef VMW_H
#define VMW_H
#include "ent.h"                       /* FrameState */

typedef struct {
  float eye[3], camR[3], camU[3], camF[3];
  float feetY;                         /* the floor the brass lands on */
  float stepPhase, moveSpeed, elapsed, aspect;
  int   grounded, crouch, flashlight, holdFire;
  float pitch, yaw;                    /* in AND out -- fire banks the climb */
  float shake;                         /* out, this frame's kick */
} VmwCtx;

int  vmwInit(void);
void vmwStep(VmwCtx *C,float dt,float rdt);
void vmwDrawShells(const FrameState *F,const float *proj,const float *view);
/* one of the three guns, in the world, turning -- what a weapon pickup is */
void vmwDrawPickup(const FrameState *F,const float *proj,const float *view,
                   int slot,float x,float y,float z,float spin,float scale);
void vmwDraw(const VmwCtx *C);

void vmwSelect(int slot);              /* 1 pistol, 2 double-barrel, 3 rotary */
void vmwForce(int slot);               /* -gun N: skip the draw animation */
void vmwReload(void);
void vmwTrigger(int down);
void vmwLook(float dx,float dy);       /* mouse motion -> the sway target */

/* what the overlay reads */
const char *vmwName(void);
int   vmwMag(void);
int   vmwReserve(void);
int   vmwUsesMag(void);
float vmwHeat(void);
int   vmwOver(void);
int   vmwReloading(void);
int   vmwArmed(void);                  /* anything at all in your hands? */
int   vmwOwns(int slot);

#endif
