/* ============================================================
   SHOT  --  the exe photographs itself.

   A CLAIM ABOUT HOW SOMETHING LOOKS NEEDS A PICTURE.  The
   browser build had tools/shot.js driving a real Chrome; the
   native build does not need one, because it can read its own
   framebuffer.  `playlumi.exe -shot out.png` boots, draws one
   frame and exits.

   PNG, written by hand.  Deflate has a STORED mode -- literal
   bytes with a five-byte header per 65,535 -- so a valid zlib
   stream costs no compressor at all.  The file is large and
   every viewer opens it, which is the whole requirement.
   ============================================================ */
#include "gl33.h"
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned crcTab[256]; static int crcReady;
static unsigned crc32b(const unsigned char *p,size_t n,unsigned c){
  if(!crcReady){
    for(unsigned i=0;i<256;i++){
      unsigned v=i;
      for(int k=0;k<8;k++) v = (v&1)? 0xEDB88320u^(v>>1) : v>>1;
      crcTab[i]=v;
    }
    crcReady=1;
  }
  c=~c;
  for(size_t i=0;i<n;i++) c=crcTab[(c^p[i])&0xFF]^(c>>8);
  return ~c;
}
static void be32(unsigned char *o,unsigned v){
  o[0]=v>>24; o[1]=v>>16; o[2]=v>>8; o[3]=v;
}
static void chunk(FILE *f,const char *tag,const unsigned char *d,size_t n){
  unsigned char h[8]; be32(h,(unsigned)n); memcpy(h+4,tag,4);
  fwrite(h,1,8,f);
  if(n) fwrite(d,1,n,f);
  unsigned c=crc32b((const unsigned char*)tag,4,0);
  c=crc32b(d,n,c);
  unsigned char cb[4]; be32(cb,c); fwrite(cb,1,4,f);
}

int shotWritePNG(const char *path,int w,int h,const unsigned char *rgb){
  /* one filter byte per row, filter 0 */
  size_t raw=(size_t)(w*3+1)*h;
  unsigned char *lin=(unsigned char*)malloc(raw);
  for(int y=0;y<h;y++){
    unsigned char *o=lin+(size_t)y*(w*3+1);
    o[0]=0;
    memcpy(o+1, rgb+(size_t)(h-1-y)*w*3, (size_t)w*3);  /* GL reads bottom-up */
  }
  /* zlib: 0x78 0x01, stored deflate blocks, adler32 */
  size_t nblk=(raw+65534)/65535;
  size_t zn=2+nblk*5+raw+4;
  unsigned char *z=(unsigned char*)malloc(zn);
  size_t w2=0;
  z[w2++]=0x78; z[w2++]=0x01;
  for(size_t off=0; off<raw; ){
    size_t n=raw-off; if(n>65535) n=65535;
    int last = (off+n>=raw);
    z[w2++]=(unsigned char)last;
    z[w2++]=(unsigned char)(n&0xFF); z[w2++]=(unsigned char)(n>>8);
    z[w2++]=(unsigned char)(~n&0xFF); z[w2++]=(unsigned char)((~n>>8)&0xFF);
    memcpy(z+w2,lin+off,n); w2+=n; off+=n;
  }
  { unsigned a=1,b=0;
    for(size_t i=0;i<raw;i++){ a=(a+lin[i])%65521; b=(b+a)%65521; }
    be32(z+w2,(b<<16)|a); w2+=4; }

  FILE *f=fopen(path,"wb");
  if(!f){ free(lin); free(z); return 0; }
  static const unsigned char sig[8]={137,80,78,71,13,10,26,10};
  fwrite(sig,1,8,f);
  unsigned char ihdr[13];
  be32(ihdr,(unsigned)w); be32(ihdr+4,(unsigned)h);
  ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;   /* 8-bit RGB */
  chunk(f,"IHDR",ihdr,13);
  chunk(f,"IDAT",z,w2);
  chunk(f,"IEND",0,0);
  fclose(f);
  free(lin); free(z);
  return 1;
}

int shotGrab(const char *path){
  int w=platW,h=platH;
  unsigned char *px=(unsigned char*)malloc((size_t)w*h*3);
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
  int ok=shotWritePNG(path,w,h,px);
  free(px);
  platLog("shot   %s  %dx%d  %s",path,w,h,ok?"ok":"FAILED");
  return ok;
}
