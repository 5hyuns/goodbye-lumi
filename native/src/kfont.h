/* Hangul, drawn rather than stored.  See kfont.c. */
#ifndef KFONT_H
#define KFONT_H

/* The glyph sheet jafont_data.h carries, expanded and handed to hud.c.
   Call it once, AFTER hudInit -- it replaces the 1x1 white placeholder
   every quad in the overlay samples. */
void  krInit (void);

/* px is the em: the width of one Hangul cell and the height of its box.
   All three alphabets take UTF-8 and mix freely.  Hangul is composed from
   strokes here, Latin falls through to hud.c's 5x7 stencil, and Japanese
   is looked up in the sheet -- see kfont.c for why exactly one of the
   three is stored rather than drawn. */
float krText (float x,float y,float px,const char *s,const float *c,float a);
float krWidth(float px,const char *s);

/* Break s into at most maxLines runs of at most cells ems, on spaces where
   there are any and anywhere at all in a Korean word, which has none.
   Returns the number of lines; off[]/len[] are byte offsets into s. */
int   krWrap (const char *s,float cells,int maxLines,int *off,int *len);

/* How many ems long the string is -- what the dialogue clock counts. */
int   krCells(const char *s);

#endif
