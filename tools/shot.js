/* Drive a built page in a real Chrome and photograph it.

   The bake runs in the browser and takes a minute or two, so nothing in this
   project can be judged from a static file -- and the parts that most need
   judging (a chamfer, a Fresnel term, a reveal) are exactly the ones a
   headless mesh dump cannot show.  This launches Chrome on its own profile
   over CDP, waits for `#boot` to clear, runs a script in the page, and takes
   one screenshot per pose.

     node tools/shot.js <url> [<script.js>] [<one.png>] [<timeout-s>]
     POSES=poses.json node tools/shot.js <url> <script.js>

   poses.json is [[x, z, yaw, pitch, "file.png", y?], ...] in world metres --
   the same numbers the bench's own JUMPS table uses.  Each pose is set, given
   a frame to settle and then set AGAIN before the shutter, because a pose out
   over the void has no floor under it and gravity eats a metre a frame.     */
const { spawn } = require('child_process');
const fs = require('fs'), path = require('path'), os = require('os');

const URL_ = process.argv[2];
const SCRIPT = process.argv[3] && process.argv[3] !== '-'
  ? fs.readFileSync(process.argv[3], 'utf8') : null;
const PNG = process.argv[4] && process.argv[4] !== '-' ? process.argv[4] : null;
const TIMEOUT = (Number(process.argv[5]) || 300) * 1000;
const PORT = 9411;
const PROFILE = path.join(os.tmpdir(), 'cdp-profile-' + PORT);

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const args = ['--remote-debugging-port=' + PORT, '--user-data-dir=' + PROFILE,
  '--no-first-run', '--no-default-browser-check', '--disable-extensions',
  '--allow-file-access-from-files', '--window-size=1440,900',
  '--js-flags=--max-old-space-size=8192', 'about:blank'];
const child = spawn(CHROME, args, { stdio: 'ignore', detached: false });

const sleep = ms => new Promise(r => setTimeout(r, ms));
const get = async u => (await fetch(u)).json();

(async () => {
  let list = null;
  for (let i = 0; i < 60; i++) {
    try { list = await get('http://127.0.0.1:' + PORT + '/json/list'); break; }
    catch (e) { await sleep(500); }
  }
  if (!list) throw new Error('chrome did not come up');

  const t = await (await fetch('http://127.0.0.1:' + PORT + '/json/new?'
    + encodeURIComponent(URL_), { method: 'PUT' })).json();
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  let id = 0; const waiting = new Map();
  const logs = [];
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled')
      logs.push('[' + m.params.type + '] ' + m.params.args.map(a =>
        a.value !== undefined ? a.value : a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown')
      logs.push('[EXCEPTION] ' + (m.params.exceptionDetails.exception
        ? m.params.exceptionDetails.exception.description
        : m.params.exceptionDetails.text));
    if (m.method === 'Inspector.targetCrashed') logs.push('[TARGET CRASHED]');
    if (m.method === 'Log.entryAdded')
      logs.push('[log:' + m.params.entry.level + '] ' + m.params.entry.text);
  };
  const send = (method, params) => new Promise((res, rej) => {
    const i = ++id; waiting.set(i, m => m.error ? rej(new Error(method + ': '
      + JSON.stringify(m.error))) : res(m.result));
    ws.send(JSON.stringify({ id: i, method, params: params || {} }));
  });
  await new Promise(r => ws.onopen = r);
  await send('Runtime.enable'); await send('Log.enable');

  const t0 = Date.now();
  let ready = false, lastTxt = '';
  while (Date.now() - t0 < TIMEOUT) {
    await sleep(3000);
    let r;
    try {
      r = await send('Runtime.evaluate', {
        expression: "(()=>{const b=document.getElementById('boot');"
          + "return JSON.stringify({h:b?b.classList.contains('hidden'):null,"
          + "t:(b?b.innerText:'').replace(/\\s+/g,' ').slice(0,150)});})()",
        returnByValue: true });
    } catch (e) { logs.push('[eval failed] ' + e.message); break; }
    if (r.exceptionDetails) { logs.push('[eval exception] '
      + JSON.stringify(r.exceptionDetails).slice(0, 300)); break; }
    const v = JSON.parse(r.result.value);
    if (v.t !== lastTxt) { lastTxt = v.t;
      console.log(((Date.now() - t0) / 1000).toFixed(0) + 's  ' + v.t); }
    if (v.h) { ready = true; break; }
    // A PAGE WITH NO #boot HAS NOTHING TO WAIT FOR.  map.html and kit.html
    // hide a boot banner when they are done; propview.html and the other
    // benches have no banner at all, so `h` comes back null forever and the
    // tool photographed nothing.  Give those a fixed grace period instead --
    // long enough for the prop bench to pack 37 atlases -- and go.
    if (v.h === null && Date.now() - t0 > 15000) { ready = true; break; }
  }
  console.log(ready ? 'READY in ' + ((Date.now() - t0) / 1000).toFixed(0) + 's'
                    : 'NOT READY');

  if (ready && SCRIPT) {
    const r = await send('Runtime.evaluate',
      { expression: SCRIPT, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) console.log('SCRIPT THREW',
      JSON.stringify(r.exceptionDetails).slice(0, 600));
    else console.log('SCRIPT =>', JSON.stringify(r.result.value));
  }
  // POSES: a json file of [[x,z,yaw,pitch,name],...] -- one screenshot each,
  // which is the only way to see a Fresnel term do its job: the same pane
  // square on and at a grazing angle are two different materials.
  const POSES = process.env.POSES ? JSON.parse(fs.readFileSync(process.env.POSES,'utf8')) : null;
  if (ready && POSES) {
    for (const [x,z,yaw,pitch,name,y] of POSES) {
      const put = "(()=>{const P=window.SHIP.P;P.x="+x+";P.z="+z+";P.y="+(y||0)
        + ";P.vy=0;P.yaw="+yaw+";P.pitch="+pitch+";})()";
      // set it, let the frame settle, then set it AGAIN: a pose out over the
      // void has no floor under it and gravity eats a metre a frame
      await send('Runtime.evaluate', { expression: put });
      await sleep(1200);
      await send('Runtime.evaluate', { expression: put });
      await sleep(140);
      const shot = await send('Page.captureScreenshot', { format: 'png' });
      fs.writeFileSync(name, Buffer.from(shot.data, 'base64'));
      console.log('shot -> ' + name);
    }
  } else if (ready && PNG) {
    await sleep(1200);
    const shot = await send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(PNG, Buffer.from(shot.data, 'base64'));
    console.log('shot -> ' + PNG);
  }
  if (logs.length) console.log('--- console ---\n' + logs.slice(-40).join('\n'));
  ws.close();
  try { await fetch('http://127.0.0.1:' + PORT + '/json/close/' + t.id); } catch (e) {}
  child.kill();
  process.exit(0);
})().catch(e => { console.error('FAILED', e); child.kill(); process.exit(1); });
