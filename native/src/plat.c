/* ============================================================
   PLATFORM  --  Win32, WGL, one window, no libraries.

   A MODERN CONTEXT COSTS TWO WINDOWS.  The pixel format of a
   window can be set exactly once, and the ARB entry points that
   pick a multisampled format only exist once some context is
   current -- so the format has to be chosen through a throwaway
   window with a throwaway 1.1 context, and only then does the
   real window get created with the answer.  Every Windows GL
   program does this dance; it is not optional and it is not a
   bug.

   Mouse look uses WM_INPUT rather than the cursor position:
   pointer-lock in the browser reported raw device deltas, and
   re-centring a cursor instead adds the desktop's acceleration
   curve to every turn.
   ============================================================ */
#include "gl33.h"
#include "app.h"
#include <stdio.h>
#include <stdarg.h>
#include <process.h>

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

/* --- WGL extension tokens ------------------------------------------ */
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_FLAGS_ARB            0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
#define WGL_CONTEXT_DEBUG_BIT_ARB        0x0001
#define WGL_DRAW_TO_WINDOW_ARB           0x2001
#define WGL_SUPPORT_OPENGL_ARB           0x2010
#define WGL_DOUBLE_BUFFER_ARB            0x2011
#define WGL_PIXEL_TYPE_ARB               0x2013
#define WGL_TYPE_RGBA_ARB                0x202B
#define WGL_COLOR_BITS_ARB               0x2014
#define WGL_DEPTH_BITS_ARB               0x2022
#define WGL_STENCIL_BITS_ARB             0x2023
#define WGL_SAMPLE_BUFFERS_ARB           0x2041
#define WGL_SAMPLES_ARB                  0x2042
#define WGL_ACCELERATION_ARB             0x2003
#define WGL_FULL_ACCELERATION_ARB        0x2027

typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC,HGLRC,const int*);
typedef BOOL  (WINAPI *PFN_wglChoosePixelFormatARB)(HDC,const int*,const FLOAT*,UINT,int*,UINT*);
typedef BOOL  (WINAPI *PFN_wglSwapIntervalEXT)(int);

int   platW=1600, platH=900;
int   platLocked=0;
char  platFail[512]={0};

static HWND   g_hwnd;
static HDC    g_dc;
static HGLRC  g_rc;
static int    g_quit=0;
static double g_freq;
static LARGE_INTEGER g_t0;
static PFN_wglSwapIntervalEXT p_wglSwapIntervalEXT;
static char   g_cmdline[1024];
static int    g_cores=1;
static FILE  *g_log;

/* ---- small services ------------------------------------------------ */
double platTime(void){
  LARGE_INTEGER t; QueryPerformanceCounter(&t);
  return (double)(t.QuadPart-g_t0.QuadPart)/g_freq;
}
void platQuit(void){ g_quit=1; }
void platTitle(const char *s){ if(g_hwnd) SetWindowTextA(g_hwnd,s); }
int  platCores(void){ return g_cores; }

void platLog(const char *fmt,...){
  char b[1024]; va_list ap; va_start(ap,fmt);
  _vsnprintf(b,sizeof b-2,fmt,ap); va_end(ap);
  b[sizeof b-2]=0;
  OutputDebugStringA(b); OutputDebugStringA("\n");
  if(g_log){ fputs(b,g_log); fputc('\n',g_log); fflush(g_log); }
}

const char *platCmdline(void){ return g_cmdline; }
int platArg(const char *flag){
  return strstr(g_cmdline,flag)!=0;
}

void platSetLock(int on){
  if(on==platLocked) return;
  platLocked=on;
  if(on){
    RECT r; GetClientRect(g_hwnd,&r);
    POINT a={r.left,r.top}, b={r.right,r.bottom};
    ClientToScreen(g_hwnd,&a); ClientToScreen(g_hwnd,&b);
    RECT s={a.x,a.y,b.x,b.y};
    ClipCursor(&s); SetCapture(g_hwnd);
    while(ShowCursor(FALSE)>=0){}
  }else{
    ClipCursor(0); ReleaseCapture();
    while(ShowCursor(TRUE)<0){}
  }
}

/* ---- a work crew, for the bake ------------------------------------- */
typedef struct { void (*fn)(int,void*); void *ctx; volatile LONG next; int n; } Crew;
static unsigned __stdcall crewRun(void *p){
  Crew *c=(Crew*)p;
  for(;;){
    LONG i=InterlockedIncrement(&c->next)-1;
    if(i>=c->n) break;
    c->fn((int)i,c->ctx);
  }
  return 0;
}
void platParallel(void (*fn)(int,void*), int n, void *ctx){
  if(n<=0) return;
  Crew c; c.fn=fn; c.ctx=ctx; c.next=0; c.n=n;
  int nt=g_cores; if(nt>n) nt=n; if(nt>64) nt=64;
  if(nt<=1){ for(int i=0;i<n;i++) fn(i,ctx); return; }
  HANDLE h[64];
  int made=0;
  for(int i=0;i<nt-1;i++){
    h[made]=(HANDLE)_beginthreadex(0,0,crewRun,&c,0,0);
    if(h[made]) made++;
  }
  crewRun(&c);                                   /* this thread pulls too */
  if(made){ WaitForMultipleObjects(made,h,TRUE,INFINITE);
            for(int i=0;i<made;i++) CloseHandle(h[i]); }
}

/* ---- window -------------------------------------------------------- */
static LRESULT CALLBACK wndProc(HWND h,UINT m,WPARAM w,LPARAM l){
  switch(m){
  case WM_CLOSE: case WM_DESTROY: g_quit=1; return 0;
  case WM_SIZE:
    platW=LOWORD(l); platH=HIWORD(l);
    if(platW<1) platW=1; if(platH<1) platH=1;
    appResize(platW,platH);
    if(platLocked) platSetLock(0), platSetLock(1);
    return 0;
  case WM_KEYDOWN: case WM_SYSKEYDOWN:
    if(!(l&(1<<30))) appKey((int)w,1);
    if(w==VK_F4 && (GetKeyState(VK_MENU)&0x8000)) g_quit=1;
    return 0;
  case WM_KEYUP: case WM_SYSKEYUP: appKey((int)w,0); return 0;
  case WM_CHAR: appChar((int)w); return 0;
  case WM_LBUTTONDOWN: appButton(0,1); return 0;
  case WM_LBUTTONUP:   appButton(0,0); return 0;
  case WM_RBUTTONDOWN: appButton(1,1); return 0;
  case WM_RBUTTONUP:   appButton(1,0); return 0;
  case WM_MBUTTONDOWN: appButton(2,1); return 0;
  case WM_MBUTTONUP:   appButton(2,0); return 0;
  case WM_MOUSEWHEEL:  appWheel((float)GET_WHEEL_DELTA_WPARAM(w)/120.0f); return 0;
  /* THE TITLE CARD NEEDS AN ABSOLUTE CURSOR AND THE GAME NEEDS A RELATIVE
     ONE.  Raw input, above, is deliberately ignored while the mouse is
     free -- a look delta from an unlocked mouse is nonsense.  A start
     button that cannot be clicked is worse, so the client-space position
     goes through as well, and only the menu reads it. */
  case WM_MOUSEMOVE:
    if(!platLocked) appCursor((float)(short)LOWORD(l),(float)(short)HIWORD(l));
    return 0;
  case WM_INPUT: {
    if(!platLocked) break;
    UINT sz=0;
    GetRawInputData((HRAWINPUT)l,RID_INPUT,0,&sz,sizeof(RAWINPUTHEADER));
    if(sz>0 && sz<=256){
      BYTE buf[256];
      if(GetRawInputData((HRAWINPUT)l,RID_INPUT,buf,&sz,sizeof(RAWINPUTHEADER))==sz){
        RAWINPUT *ri=(RAWINPUT*)buf;
        if(ri->header.dwType==RIM_TYPEMOUSE &&
           !(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
          appMouse((float)ri->data.mouse.lLastX,(float)ri->data.mouse.lLastY);
      }
    }
    break;
  }
  case WM_SETCURSOR:
    if(platLocked && LOWORD(l)==HTCLIENT){ SetCursor(0); return 1; }
    break;
  case WM_ACTIVATE:
    if(LOWORD(w)==WA_INACTIVE && platLocked) platSetLock(0);
    return 0;
  }
  return DefWindowProcA(h,m,w,l);
}

/* Pick the pixel format through a throwaway context, then throw it away. */
static int probeFormat(HINSTANCE hi,int *fmt,int samples,
                       PFN_wglCreateContextAttribsARB *ccaOut){
  WNDCLASSA wc={0};
  wc.lpfnWndProc=DefWindowProcA; wc.hInstance=hi;
  wc.lpszClassName="dsProbe"; wc.style=CS_OWNDC;
  RegisterClassA(&wc);
  HWND h=CreateWindowA("dsProbe","",WS_OVERLAPPEDWINDOW,0,0,32,32,0,0,hi,0);
  if(!h) return 0;
  HDC dc=GetDC(h);
  PIXELFORMATDESCRIPTOR pfd={sizeof pfd,1,
    PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32};
  pfd.cDepthBits=24;
  int f=ChoosePixelFormat(dc,&pfd);
  SetPixelFormat(dc,f,&pfd);
  HGLRC rc=wglCreateContext(dc);
  wglMakeCurrent(dc,rc);

  PFN_wglChoosePixelFormatARB cpf =
    (PFN_wglChoosePixelFormatARB)wglGetProcAddress("wglChoosePixelFormatARB");
  *ccaOut = (PFN_wglCreateContextAttribsARB)
    wglGetProcAddress("wglCreateContextAttribsARB");
  p_wglSwapIntervalEXT = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");

  *fmt = f;                                     /* the plain one, as a floor */
  if(cpf){
    for(int s=samples; s>=0; s>>=1){
      int at[]={ WGL_DRAW_TO_WINDOW_ARB,1, WGL_SUPPORT_OPENGL_ARB,1,
                 WGL_DOUBLE_BUFFER_ARB,1, WGL_PIXEL_TYPE_ARB,WGL_TYPE_RGBA_ARB,
                 WGL_ACCELERATION_ARB,WGL_FULL_ACCELERATION_ARB,
                 WGL_COLOR_BITS_ARB,32, WGL_DEPTH_BITS_ARB,24, WGL_STENCIL_BITS_ARB,8,
                 WGL_SAMPLE_BUFFERS_ARB,s?1:0, WGL_SAMPLES_ARB,s, 0 };
      int got; UINT n=0;
      if(cpf(dc,at,0,1,&got,&n) && n>0){ *fmt=got; break; }
      if(s==0) break;
    }
  }
  wglMakeCurrent(0,0); wglDeleteContext(rc);
  ReleaseDC(h,dc); DestroyWindow(h);
  UnregisterClassA("dsProbe",hi);
  return 1;
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE prev,LPSTR cmd,int show){
  (void)prev;(void)show;
  lstrcpynA(g_cmdline,cmd?cmd:"",sizeof g_cmdline);

  { SYSTEM_INFO si; GetSystemInfo(&si);
    g_cores=(int)si.dwNumberOfProcessors; if(g_cores<1) g_cores=1; }

  LARGE_INTEGER f; QueryPerformanceFrequency(&f);
  g_freq=(double)f.QuadPart; QueryPerformanceCounter(&g_t0);

  if(platArg("-log")) g_log=fopen("derelict.log","w");

  PFN_wglCreateContextAttribsARB cca=0;
  int fmt=0;
  if(!probeFormat(hi,&fmt,4,&cca)){
    MessageBoxA(0,"No OpenGL pixel format.","Play Lumi",MB_ICONERROR); return 1;
  }

  WNDCLASSA wc={0};
  wc.lpfnWndProc=wndProc; wc.hInstance=hi; wc.lpszClassName="playlumi";
  wc.style=CS_OWNDC; wc.hCursor=LoadCursorA(0,IDC_ARROW);
  /* HER FACE ON THE WINDOW TOO.  Resource 1 is build/icon.ico, which
     tools/mkicon.py draws and windres wraps; Explorer reads it off the
     file without being asked, and this is what puts the same picture in
     alt-tab and on the taskbar button. */
  wc.hIcon=LoadIconA(hi,MAKEINTRESOURCEA(1));
  wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClassA(&wc);

  RECT r={0,0,platW,platH};
  DWORD st=WS_OVERLAPPEDWINDOW;
  AdjustWindowRect(&r,st,FALSE);
  g_hwnd=CreateWindowA("playlumi","Play Lumi",st,
    CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,0,0,hi,0);
  if(!g_hwnd){ MessageBoxA(0,"No window.","Play Lumi",MB_ICONERROR); return 1; }
  /* AND THE 16 PX ONE, WHICH IS A DIFFERENT DRAWING.  LoadIcon takes
     the 32 and the title bar then shrinks it, which loses the eyes;
     LoadImage at SM_CXSMICON picks the 16 mkicon.py laid out for that
     grid.  WNDCLASSA has no hIconSm field -- that is WNDCLASSEX -- so
     it is set on the window instead of the class. */
  { HICON sm=(HICON)LoadImageA(hi,MAKEINTRESOURCEA(1),IMAGE_ICON,
                               GetSystemMetrics(SM_CXSMICON),
                               GetSystemMetrics(SM_CYSMICON),0);
    if(sm) SendMessageA(g_hwnd,WM_SETICON,ICON_SMALL,(LPARAM)sm); }
  g_dc=GetDC(g_hwnd);

  PIXELFORMATDESCRIPTOR pfd={0};
  DescribePixelFormat(g_dc,fmt,sizeof pfd,&pfd);
  SetPixelFormat(g_dc,fmt,&pfd);

  if(cca){
    int at[]={ WGL_CONTEXT_MAJOR_VERSION_ARB,3, WGL_CONTEXT_MINOR_VERSION_ARB,3,
               WGL_CONTEXT_PROFILE_MASK_ARB,WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    g_rc=cca(g_dc,0,at);
  }
  if(!g_rc) g_rc=wglCreateContext(g_dc);
  if(!g_rc){ MessageBoxA(0,"No OpenGL 3.3 context.\nUpdate your graphics driver.",
                         "Play Lumi",MB_ICONERROR); return 1; }
  wglMakeCurrent(g_dc,g_rc);

  if(!glLoad()){
    char b[256];
    wsprintfA(b,"OpenGL entry point missing: %s",glLoadMissing());
    MessageBoxA(0,b,"Play Lumi",MB_ICONERROR); return 1;
  }
  if(p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(platArg("-bench")?0:1);

  { RAWINPUTDEVICE rid={0x01,0x02,0,g_hwnd};     /* generic desktop / mouse */
    RegisterRawInputDevices(&rid,1,sizeof rid); }

  platLog("GL  %s",(const char*)glGetString(GL_VERSION));
  platLog("GPU %s",(const char*)glGetString(GL_RENDERER));
  platLog("cores %d",g_cores);

  ShowWindow(g_hwnd,SW_SHOW);
  { RECT c; GetClientRect(g_hwnd,&c); platW=c.right; platH=c.bottom; }

  if(!appInit()){
    MessageBoxA(0,platFail[0]?platFail:"Startup failed.","Play Lumi",MB_ICONERROR);
    return 1;
  }

  double tPrev=platTime();
  while(!g_quit){
    MSG msg;
    while(PeekMessageA(&msg,0,0,0,PM_REMOVE)){
      if(msg.message==WM_QUIT){ g_quit=1; break; }
      TranslateMessage(&msg); DispatchMessageA(&msg);
    }
    if(g_quit) break;
    double now=platTime(), dt=now-tPrev; tPrev=now;
    if(dt>0.1) dt=0.1;                           /* a stall is not a teleport */
    appFrame(now,dt);
    SwapBuffers(g_dc);
  }

  platSetLock(0);
  wglMakeCurrent(0,0); wglDeleteContext(g_rc);
  ReleaseDC(g_hwnd,g_dc); DestroyWindow(g_hwnd);
  if(g_log) fclose(g_log);
  return 0;
}
