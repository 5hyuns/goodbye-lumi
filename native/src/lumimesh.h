/* ============================================================
   LUMI'S SHAPE, WHICH IS NOT THIS FILE'S TO DECIDE.

   Her design is frozen, and props/lumi_v02/lumi_geometry.js is
   the generator that froze her: 35 parts, 9,627 vertices, 14,772
   triangles, every transform baked into the points.  She is not
   to be rebuilt out of spheres and cylinders -- thirty primitives
   standing in for a shape that already exists.

   So this is the generator ported, statement for statement, and
   the checksum is how the port says so.  It reports the same
   three numbers the JS does, and `playlumi.exe -lumi -log`
   prints them beside the per-part table.

   The parts are grouped by MATERIAL into primitive ranges, not
   kept as 35 separate solids, because a group shares one colour
   and one surface and the entity pass draws a range in one
   instanced call.  Thirteen ranges, thirteen instances, and the
   whole drone costs thirteen draws.
   ============================================================ */
#ifndef LUMIMESH_H
#define LUMIMESH_H

/* the generator's material groups, in the order they are built */
enum { LMP_PAINT=0, LMP_BAND, LMP_STEEL, LMP_DARK, LMP_PANEL, LMP_SCREEN,
       LMP_DECAL, LMP_MARK, LMP_CYAN, LMP_DIM, LMP_GLOW, LMP_BEAM0,
       LMP_BEAM1, LMP_N };

/* the generator's own numbers, for anything that has to place something
   ON her -- the display sits on the screen patch's sphere, not on a plate */
#define LM_R        0.222f          /* body radius; lumi.c's V() divides by it */
#define LM_RSCREEN  0.2236f         /* visor_screen's radius */
#define LM_VTILT   -0.26f           /* the visor group's X rotation */
extern float LM_EMIT_Y;             /* spherePt(A5).y - 0.055 */

extern int LUMI_PRIM[LMP_N];        /* primAdd()'s answer, per group */

void lumiMeshInit(void);            /* build, and hand the groups to prim.c */
void lumiMeshCheck(int *parts,int *verts,int *tris);   /* the generator's checksum */

/* visor_screen once more, as a triangle soup of (x,y,z,u,v) with the visor
   tilt already baked in -- face.c's display surface.  The caller owns the
   buffer and frees it; the return is the vertex count. */
int  lumiMeshScreen(float **out);

#endif
