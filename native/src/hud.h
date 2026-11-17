/* The overlay.  Pixels, origin top-left, so a layout reads the way the CSS
   it replaces did.  Colours are the ship's own: bone, teal, amber, dim. */
#ifndef HUD_H
#define HUD_H

int   hudInit(void);
void  hudBegin(void);
void  hudRect(float x,float y,float w,float h,const float *c,float a);
float hudText(float x,float y,float px,const char *s,const float *c,float a);
float hudWidth(float px,const char *s);
void  hudEnd(void);

/* THE ONE THING IN THE OVERLAY THAT IS STORED RATHER THAN DRAWN.  Hangul is
   24 letters and a rule, Latin is a 5x7 stencil, and Japanese is neither:
   there is no composition rule that builds a kanji.  So kfont.c hands over
   a single-channel sheet of glyph cells at boot and asks for pieces of it
   back.  Every quad in the batch carries uv now, and hudRect points its own
   at a white row of the same sheet -- one vertex format, one draw call, and
   nothing else in the HUD had to change. */
void  hudGlyphSheet(const unsigned char *r8,int w,int h);
void  hudTexRect(float x,float y,float w,float h,
                 float u0,float v0,float u1,float v1,const float *c,float a);

/* --amber, --teal, --bone, --dim, --panel, straight off the previz's :root */
extern const float HUD_BONE[3], HUD_TEAL[3], HUD_AMBER[3],
                   HUD_DIM[3], HUD_INK[3], HUD_CRIT[3];

/* One glyph of the 5x7 stencil: seven rows, five bits each, MSB at 0x10.
   lumimesh.c cuts LUMI's own nameplate out of it -- a native port has
   to replace the decal's canvas text with bitmap glyphs,
   and the ship already has exactly one alphabet. */
const unsigned char *hudGlyph(int c);

#endif
