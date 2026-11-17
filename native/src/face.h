/* ============================================================
   LUMI'S DISPLAY, AS A SCREEN.

   The bench puts a 512 x 290 canvas on visor_screen through a
   MeshBasicMaterial -- an UNLIT texture, redrawn every frame by
   Canvas2D.  An earlier version had no texture unit to put
   it on, so each face was built out of little emissive solids:
   ten boxes for a health bar, eleven for a heartbeat.  That
   reads at a metre and falls apart at three, it cannot draw a
   line that is not axis-aligned, and it cost twenty instances
   a frame to say what one quad says.

   So the canvas becomes a fragment shader on the screen patch
   itself.  Same geometry, same uv, no texture: the display is
   evaluated per pixel from the state and the clock.  It is
   resolution-independent, it antialiases with fwidth, and the
   ink is emissive so the scene's own bloom does the shadowBlur
   the canvas faked.

   Text is the ship's 5x7 stencil (hud.c), packed two ints to a
   glyph and placed on the CPU -- the shader draws cells, it does
   not know what a letter is.
   ============================================================ */
#ifndef FACE_H
#define FACE_H
#include "ent.h"

int  faceInit(void);

/* Called by lumiDraw() while it is building her transform.  M is her body
   frame WITHOUT the visor tilt (the patch carries that), s the uniform
   scale.  disp < 0 means she has no screen this frame. */
void faceSet(const float *M,float s,int disp,
             float hp,float bpm,int count,float gauge,float blink,float t);

/* After entDraw(), before the glass.  The screen patch is drawn HERE and
   not by the entity pass, so there is one surface and no z-fight. */
void faceDraw(const FrameState *F);

#endif
