/* The wall builders, so the registry in kitparts.c can name them. */
#ifndef KITWALLS_H
#define KITWALLS_H
#include "kit.h"
void mkWall1(float a,PB *P);   int clWall1(float a,float *o);   int emWall1(float a,float *o);
void mkWall2(float a,PB *P);   int clWall2(float a,float *o);   int emWall2(float a,float *o);
void mkWall3(float a,PB *P);   int clWall3(float a,float *o);   int emWall3(float a,float *o);
void mkWall4(float a,PB *P);   int clWall4(float a,float *o);   int emWall4(float a,float *o);
void mkFillet1(float a,PB *P); int clFillet1(float a,float *o);
void mkCorner1(float a,PB *P); int clCorner1(float a,float *o); int emCorner1(float a,float *o);
#endif
