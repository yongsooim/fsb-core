// Headless check that the browser host actually runs: load the page, press
// start, let the engine tick, then read the canvas back and report whether it
// drew anything. Usage: node verify.js <url> <out.png> [seconds]
const { chromium } = require('playwright');

(async () => {
  const [url, out, seconds = '8'] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 900, height: 760 } });
  const problems = [];
  page.on('console', m => { if (m.type() === 'error') problems.push(m.text()); });
  page.on('pageerror', e => problems.push(String(e)));

  await page.goto(url, { waitUntil: 'load' });
  // Wait on the filesystem, not on the button: an earlier version offered the
  // button before the asset package mounted, and this check would have caught
  // it instead of passing whenever the click happened to land late enough.
  await page.waitForFunction(
    () => window.fsbReady && FS.analyzePath('/assets/FLYINGSB.EXE').exists,
    null, { timeout: 180000 });
  await page.click('#start');
  await page.waitForTimeout(Number(seconds) * 1000);

  // Hold-to-fast-forward. Logical milliseconds per real second is the only
  // measure that separates a faster engine from a faster screen: presents stay
  // on rAF and the mixer stays on the device clock at either rate.
  const rate = async hold => {
    const logical = () => page.evaluate(() => Module.ccall('fsb_logical_ms', 'number', [], []));
    if (hold) await page.keyboard.down('`');
    const before = await logical(), started = Date.now();
    await page.waitForTimeout(1500);
    const advanced = (await logical()) - before, real = Date.now() - started;
    if (hold) await page.keyboard.up('`');
    return advanced / real;
  };
  const playback = { normal: await rate(false), held: await rate(true), released: await rate(false) };

  // A blank canvas and a drawn one are easy to confuse in a screenshot, so ask
  // the page how many distinct colours it actually put on screen.
  const drawn = await page.evaluate(() => {
    const canvas = document.getElementById('screen');
    const data = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    const seen = new Set();
    let lit = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (data[i] | data[i + 1] | data[i + 2]) ++lit;
      seen.add((data[i] << 16) | (data[i + 1] << 8) | data[i + 2]);
    }
    return { width: canvas.width, height: canvas.height, lit, colours: seen.size,
             status: document.getElementById('status').textContent };
  });

  await page.screenshot({ path: out });
  await browser.close();
  console.log(JSON.stringify({ ...drawn, playback, problems }, null, 1));
  // A machine that cannot execute 16x still has to be clearly faster than 1x,
  // and releasing the key has to give the original rate back.
  const paced = playback.held > playback.normal * 4 && playback.released < playback.held / 2;
  process.exit(drawn.colours > 1 && paced && problems.length === 0 ? 0 : 1);
})();
