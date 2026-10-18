// SPDX-License-Identifier: GPL-3.0-or-later
// Smoke test for the WebAssembly build: loads bc101.js under Node, plays one second of every
// scene and fails if a scene draws nothing or a frame takes longer than the Pi's 20 ms budget
// (which on a workstation means something is badly wrong).
//   node web/check.js build/site/bc101.js
const createBC101 = require(require("path").resolve(process.argv[2]));
createBC101().then(m => {
  const name = m.cwrap("web_scene_name", "string", ["number"]), start = m.cwrap("web_start", null, ["number", "number"]);
  const step = m.cwrap("web_step", "number", ["number"]), W = m._web_w(), H = m._web_h();
  if (m._web_init() !== 0) throw new Error("fonts did not load");
  let bad = 0;
  for (let i = 0; i < m._web_scene_count(); i++) {
    start(i, 1);
    let p = 0, worst = 0;
    for (let k = 0; k < 50; k++) {
      const t0 = process.hrtime.bigint();
      p = step(0.02);
      worst = Math.max(worst, Number(process.hrtime.bigint() - t0) / 1e6);
    }
    const px = m.HEAPU8.subarray(p, p + W * H * 4);
    let lit = 0;
    for (let j = 0; j < px.length; j += 4) if (px[j] | px[j + 1] | px[j + 2]) lit++;
    const ok = lit > 0 && worst < 20;
    if (!ok) bad++;
    console.log(`${ok ? "ok  " : "FAIL"} ${name(i).padEnd(9)} worst ${worst.toFixed(2).padStart(6)} ms  ${String(Math.round(100 * lit / (W * H))).padStart(3)} % lit`);
  }
  process.exit(bad ? 1 : 0);
});
