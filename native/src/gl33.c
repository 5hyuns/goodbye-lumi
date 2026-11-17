/* The loader.  WGL HANDS BACK NULL FOR 1.1 ENTRY POINTS: wglGetProcAddress
   is only obliged to know about extensions and post-1.1 core, so anything
   the ICD considers "already exported" comes back null and has to be
   resolved out of opengl32.dll by name instead.  Both paths, every time,
   is the only version of this that does not fail on one vendor. */
#include "gl33.h"

#define X(r,n,a) PFN_##n p_##n;
GL_FUNCS
#undef X

static char missing[64];

static PROC getproc(HMODULE lib, const char *name){
  PROC p = wglGetProcAddress(name);
  /* the three sentinels drivers have historically returned for "no" */
  if(p==0 || p==(PROC)1 || p==(PROC)2 || p==(PROC)3 || p==(PROC)-1)
    p = GetProcAddress(lib, name);
  return p;
}

int glLoad(void){
  HMODULE lib = LoadLibraryA("opengl32.dll");
  if(!lib) { lstrcpyA(missing,"opengl32.dll"); return 0; }
  missing[0] = 0;
#define X(r,n,a) p_##n = (PFN_##n)getproc(lib, #n); \
  if(!p_##n && !missing[0]) lstrcpynA(missing, #n, sizeof missing);
  GL_FUNCS
#undef X
  return missing[0] ? 0 : 1;
}

const char *glLoadMissing(void){ return missing; }
