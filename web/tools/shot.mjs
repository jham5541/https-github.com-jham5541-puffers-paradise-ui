// Screenshot + smoke-test harness. Usage:
//   node tools/shot.mjs                 # all shots
//   node tools/shot.mjs --only night    # one shot
//   node tools/shot.mjs --fast          # shorter settle times
// Writes PNGs to tools/shots/ and errors to tools/shots/errors.json.
// Exit code 1 if any console/page error occurred.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const { chromium } = await import(path.join(root, 'node_modules', 'playwright-core', 'index.mjs'));

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p === '/') p = '/index.html';
  const f = path.join(root, p);
  if (!f.startsWith(root) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, r));
const port = server.address().port;

const fast = process.argv.includes('--fast');
const only = process.argv.includes('--only') ? process.argv[process.argv.indexOf('--only') + 1] : null;
const settle = (s) => fast ? Math.min(s, 3000) : s;

const SHOTS = [
  { name: 'menu',        url: '?tod=19.2&seed=1337', wait: 9000 },
  { name: 'street-dusk', url: '?autostart=purp&tod=17.6&cam=street&seed=1337', wait: 9000 },
  { name: 'street-noon', url: '?autostart=preme&tod=12.5&cam=street&seed=1337', wait: 8000 },
  { name: 'night',       url: '?autostart=smoke&tod=22.5&cam=street&seed=1337', wait: 8000 },
  { name: 'high-noon',   url: '?autostart=dime&tod=14&cam=high&seed=1337', wait: 8000 },
  { name: 'vista-dusk',  url: '?autostart=nug&tod=18.1&cam=vista&seed=1337', wait: 8000 },
  { name: 'orbit-night', url: '?autostart=purp&tod=23&cam=orbit&seed=1337', wait: 8000 },
  { name: 'gameplay',    url: '?autostart=purp&tod=17.6&seed=1337', wait: 9000 },
  { name: 'menu-all',    url: '?tod=19.2&seed=1337&unlock=all', wait: 9000 },
  { name: 'rain-night',  url: '?autostart=smoke&tod=21.5&cam=street&weather=rain&seed=1337', wait: 8000 },
];

const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium', args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-sandbox'] });
const outDir = path.join(root, 'tools', 'shots');
fs.mkdirSync(outDir, { recursive: true });
const allErrors = {};
let failed = false;

for (const s of SHOTS) {
  if (only && s.name !== only) continue;
  const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
  const errs = [];
  page.on('console', (m) => { if (m.type() === 'error') errs.push(m.text()); });
  page.on('pageerror', (e) => errs.push(String(e)));
  try {
    await page.goto(`http://127.0.0.1:${port}/index.html${s.url}`, { waitUntil: 'load', timeout: 30000 });
    await page.waitForTimeout(settle(s.wait));
    await page.screenshot({ path: path.join(outDir, `${s.name}.png`) });
    console.log(`shot: ${s.name}.png ${errs.length ? `(${errs.length} ERRORS)` : 'ok'}`);
  } catch (e) {
    errs.push('HARNESS: ' + e.message);
    console.log(`shot: ${s.name} FAILED ${e.message}`);
  }
  if (errs.length) { allErrors[s.name] = [...new Set(errs)].slice(0, 20); failed = true; }
  await page.close();
}
fs.writeFileSync(path.join(outDir, 'errors.json'), JSON.stringify(allErrors, null, 2));
await browser.close();
server.close();
if (failed) { console.error('ERRORS:\n' + JSON.stringify(allErrors, null, 2)); process.exit(1); }
console.log('all shots clean');
