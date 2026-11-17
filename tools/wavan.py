"""Per-event analysis of playlumi.exe's audio bench.  Stdlib only.

    playlumi.exe -wav mix.wav          # writes mix.wav + mix.wav.events.txt
    python tools/wavan.py mix.wav mix.wav.events.txt

A CLAIM ABOUT SOUND NEEDS A FILE, and the file needs a number beside it.
The bench already said rms/peak/clipped for the whole render, which cannot
tell a gunshot from a dial tone.  This reads each event in its own window
and prints the two things the ear is actually judging.

"""
# from the sidecar the exe writes, so the two cannot drift.
#
#   flat   spectral flatness (geometric / arithmetic mean of the power
#          spectrum), AFTER subtracting the ambient bed measured from a
#          silent window -- otherwise the hull hiss decides the answer for
#          every quiet event.  A bare square wave is ~0.0003.  A piece of
#          concrete being hit is ~0.26.  THIS IS THE NUMBER THAT TELLS A STRIKE
#          FROM A CHIME.
#   ons    onsets: how many separate things arrived.  A blip is 1.
#   cen    spectral centroid, Hz
#   bw     the span holding 80% of the energy, Hz
import sys, wave, struct, math, cmath

def read(path):
    w = wave.open(path,'rb'); n = w.getnframes(); sr = w.getframerate()
    raw = w.readframes(n); w.close()
    d = struct.unpack('<%dh'%(n*2), raw)
    return [d[i*2]/32768.0 for i in range(n)], sr

def fft(a):
    n = len(a)
    if n == 1: return a
    ev = fft(a[0::2]); od = fft(a[1::2])
    out = [0]*n
    for k in range(n//2):
        t = cmath.exp(-2j*math.pi*k/n)*od[k]
        out[k] = ev[k]+t; out[k+n//2] = ev[k]-t
    return out

N = 4096
def spec(seg):
    fr = (list(seg) + [0.0]*N)[:N]
    fr = [fr[i]*(0.5-0.5*math.cos(2*math.pi*i/(N-1))) for i in range(N)]
    S = fft([complex(v,0) for v in fr])
    return [abs(S[k])**2 for k in range(N//2)]

def metrics(x, sr, floor):
    pk = max((abs(v) for v in x), default=0)
    if pk < 1e-4: return None
    thr = pk*0.0056                       # -45 dB of the window peak
    a = 0
    while a < len(x) and abs(x[a]) < thr: a += 1
    b = len(x)-1
    while b > a and abs(x[b]) < thr: b -= 1
    seg = x[a:b+1]
    if len(seg) < 64: return None
    dur = len(seg)/sr
    hop = int(sr*0.005); ons = 0; prev = 1e-9; last = -99
    for i in range(0, max(1,len(seg)-hop), hop):
        e = sum(v*v for v in seg[i:i+hop])/hop + 1e-12
        if e > prev*4.0 and e > (pk*pk)*0.02 and i/sr - last > 0.012:
            ons += 1; last = i/sr
        prev = max(e, prev*0.55)
    P = spec(seg)
    # judge tonality where the EAR judges it.  A deliberate sub -- the
    # shotgun's 62 Hz, a brooder's chest -- holds most of the energy in the
    # file and none of the character, so a full-range bandwidth calls every
    # sound with a thump in it a pure tone.  300 Hz up.
    lo = int(300*N/sr); hi = int(16000*N/sr)
    # take the ambient bed out before judging tonality
    P = [max(P[k]-floor[k]*1.2, P[k]*0.02) + 1e-14 for k in range(lo,hi)]
    am = sum(P)/len(P)
    gm = math.exp(sum(math.log(p) for p in P)/len(P))
    tot = sum(P)
    cen = sum(P[i]*((i+lo)*sr/N) for i in range(len(P)))/tot
    order = sorted(range(len(P)), key=lambda i:-P[i])
    acc = 0; sel = []
    for i in order:
        acc += P[i]; sel.append(i)
        if acc > tot*0.80: break
    # loudest 30 ms, not the whole window: a 10 ms clack averaged over a
    # 350 ms slot reads as silence, and audibility is not an average.
    W = int(sr*0.030); rms = 0.0
    for i in range(0, max(1, len(seg)-W), W//3):
        r = math.sqrt(sum(v*v for v in seg[i:i+W])/W)
        if r > rms: rms = r
    return dict(pk=pk, dur=dur, ons=ons, flat=gm/am, cen=cen, rms=rms,
                bw=(max(sel)-min(sel))*sr/N)

path = sys.argv[1]
x, sr = read(path)
EV = []
for line in open(sys.argv[2]):
    if line.strip():
        t, name = line.rstrip('\n').split('\t')
        EV.append((float(t), name))
# the ambient bed, measured where nothing is firing
fw = x[int(0.02*sr):int(0.02*sr)+N]
floor = spec(fw)
W = int(sr*0.030); froms = 0.0
for i in range(0, len(fw)-W, W//3):
    r = math.sqrt(sum(v*v for v in fw[i:i+W])/W)
    if r > froms: froms = r
print('room tone rms %.5f (%.1f dB) -- every event has to clear it'
      % (froms, 20*math.log10(froms)))
print('%-20s %7s %6s %4s %8s %7s %7s %7s' %
      ('event','peak','dur','ons','flat','cen Hz','bw Hz','S/N dB'))
for i,(t,name) in enumerate(EV):
    end = EV[i+1][0] if i+1 < len(EV) else t+0.6
    w = x[int(t*sr):int(min(end, t+0.95)*sr)]
    m = metrics(w, sr, floor)
    if not m: print('%-20s  SILENT'%name); continue
    snr = 20*math.log10(m['rms']/froms)
    tag = ''
    if m['flat'] < 0.004: tag += ' TONE'
    if snr < 6.0:         tag += ' QUIET'
    print('%-20s %7.4f %6.3f %4d %8.4f %7.0f %7.0f %7.1f %s' %
          (name, m['pk'], m['dur'], m['ons'], m['flat'], m['cen'], m['bw'],
           snr, tag))
