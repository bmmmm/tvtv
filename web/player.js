// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the WebAssembly build of the show (web/shim.c) on a canvas. The show is stepped in fixed
// 20 ms ticks like on the Pi, whatever the display's refresh rate, so every scene looks the same
// as on the tube, including the ones that build their state frame by frame.
// Expects: #tv (canvas), #scene (select), #play (button), #wide and #crt (checkboxes), #readout, #budget
// and the global createBC101 from bc101.js.
(async () => {
  const $ = id => document.getElementById(id);
  const canvas = $("tv"), ctx = canvas.getContext("2d");
  const m = await createBC101();
  const C = {
    init: m.cwrap("web_init", "number", []),
    count: m.cwrap("web_scene_count", "number", []),
    loop: m.cwrap("web_loop_count", "number", []),
    name: m.cwrap("web_scene_name", "string", ["number"]),
    dur: m.cwrap("web_scene_dur", "number", ["number"]),
    start: m.cwrap("web_start", null, ["number", "number"]),
    current: m.cwrap("web_current", "number", []),
    time: m.cwrap("web_time", "number", []),
    length: m.cwrap("web_length", "number", []),
    step: m.cwrap("web_step", "number", ["number"]),
  };
  if (C.init() !== 0) { $("readout").textContent = "font load failed"; return; }
  const W = m._web_w(), H = m._web_h(), STEP = 0.02;
  canvas.width = W;
  canvas.height = H;
  const img = ctx.createImageData(W, H);

  const picker = $("scene"), play = $("play"), wide = $("wide"), crt = $("crt"), readout = $("readout"), budget = $("budget");
  const all = picker.dataset.all || "whole show";
  picker.add(new Option(all, "-1"));
  for (let i = 0; i < C.count(); i++)
    picker.add(new Option(C.name(i) + (i >= C.loop() ? " ·" : ""), String(i)));

  const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
  let playing = !reduced, last = null, acc = 0, ms = 0;

  function draw(ptr) {
    img.data.set(m.HEAPU8.subarray(ptr, ptr + W * H * 4));
    ctx.putImageData(img, 0, 0);
  }
  function status() {
    const i = C.current();
    readout.textContent = `${C.name(i)}  t = ${C.time().toFixed(1)} s / ${C.length().toFixed(0)} s   ${ms.toFixed(2)} ms`;
    if (budget) budget.style.width = Math.min(100, ms / STEP / 10) + "%";
    if (picker.value !== "-1" && picker.value !== String(i)) picker.value = String(i);
  }
  function tick(now) {
    if (last === null) last = now;
    if (playing) {
      acc += Math.min((now - last) / 1000, 0.25); // a hidden tab must not fast-forward
      let ptr = 0, n = 0;
      const t0 = performance.now();
      while (acc >= STEP && n < 5) { ptr = C.step(STEP); acc -= STEP; n++; }
      if (n === 5) acc = 0;
      if (n) { ms = (performance.now() - t0) / n; draw(ptr); status(); }
    }
    last = now;
    requestAnimationFrame(tick);
  }
  play.addEventListener("click", () => { playing = !playing; play.textContent = playing ? "Pause" : "Play"; });
  picker.addEventListener("change", () => {
    const v = parseInt(picker.value, 10);
    C.start(v < 0 ? 0 : v, v >= 0);
    draw(C.step(0)); status();
  });
  if (wide) wide.addEventListener("change", () => canvas.classList.toggle("square", !wide.checked));
  if (crt) crt.addEventListener("change", () => canvas.parentElement.classList.toggle("flat", !crt.checked));
  document.addEventListener("keydown", e => {
    if (e.key === " " && e.target === document.body) { e.preventDefault(); play.click(); }
    if (e.key === "n") { C.start((C.current() + 1) % C.loop(), false); picker.value = "-1"; }
  });

  C.start(0, false);
  draw(C.step(0));
  status();
  play.textContent = playing ? "Pause" : "Play";
  requestAnimationFrame(tick);
})();
