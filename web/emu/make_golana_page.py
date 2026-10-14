#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gadlele, based on Felucca's web/emu (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Golana in the browser as ONE self-contained page: web/emu/index.html's panel, the firmware's WebAssembly
(build/emu/felucca.wasm, web/emu/build_wasm.sh) and its audio code inlined, so the page needs no server.
The audio runs in an AudioWorklet loaded from a blob: URL; where that is refused, on the page's own thread
(a ScriptProcessorNode). Saves (WRITE) are kept in the browser's localStorage.
  web/emu/make_golana_page.py build/emu/felucca.wasm OUT.html"""
import base64
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# the device side: worklet.js's processor as a plain class, used by the worklet and by the fallback
CORE = r"""
const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000]];
function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}
const HOLD_MS = 24;
class GolanaCore {
  constructor(post) {
    this.post = post; this.ex = null; this.sentDraws = -1; this.sentWrites = -1; this.sentLeds = "";
    this.lastFrame = 0; this.period = 1 / 30; this.queue = []; this.nextOk = 0;
  }
  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const ex = instance.exports;
        if (ex._initialize) ex._initialize();
        ex.web_nor_erase();
        restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
        ex.web_master(m.master ?? 700);
        ex.web_boot();
        this.ex = ex;
        this.sentWrites = ex.web_flash_writes_count();
        this.notesR = ex.web_notes_w ? ex.web_notes_w() >>> 0 : 0;
        this.post({ type: "ready", rate: ex.web_sample_rate() });
      } catch (err) {
        this.post({ type: "error", message: String(err && err.message || err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
    else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
    else if (m.type === "master") ex.web_master(m.value | 0);
    else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
    else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
  }
  publish() {
    const ex = this.ex, mem = ex.memory.buffer, msg = { type: "frame" };
    const leds = [ex.web_lit_buttons(), ex.web_lit_keys(), ex.web_dim_buttons(), ex.web_dim_keys(), ex.web_dim_level(),
                  ex.web_breath_buttons(), ex.web_breath_keys(), ex.web_mid_keys ? ex.web_mid_keys() : 0];
    const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;
    const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
    const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
    if (key !== this.sentLeds) { this.sentLeds = key; msg.leds = leds; msg.anim = anim; }
    const transfer = [];
    const draws = ex.web_screen_draws();
    if (draws !== this.sentDraws) {
      this.sentDraws = draws;
      msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    const writes = ex.web_flash_writes_count();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
    }
    if (ex.web_notes_w) {                 // the notes started since the last frame (the Biosphere link; the same
      const w = ex.web_notes_w() >>> 0, notes = [];   // note twice in a row, UNISON's voices, sent once)
      if (w - this.notesR > 16) this.notesR = (w - 16) >>> 0;
      for (; this.notesR !== w; this.notesR = (this.notesR + 1) >>> 0) {
        const n = ex.web_note_at(this.notesR);
        if (notes[notes.length - 1] !== n) notes.push(n);
      }
      if (notes.length) msg.notes = notes;
    }
    if (this.lvN) { msg.level = Math.sqrt(this.lvSum / this.lvN); this.lvSum = this.lvN = 0; }   // RMS of the left
    if (msg.leds || msg.fb || msg.sectors || msg.notes || msg.level !== undefined) this.post(msg, transfer);
  }
  input() {
    const ex = this.ex, now = ex.web_now_ms();
    while (this.queue.length && now - this.nextOk >= 0) {
      const m = this.queue.shift();
      if (m.type === "buttons") ex.web_buttons(m.mask >>> 0); else ex.web_keys(m.mask >>> 0);
      this.nextOk = now + HOLD_MS;
    }
  }
  render(l, r, n, time) {               /* n <= 1024 frames into l, r (Float32Array) */
    const ex = this.ex;
    if (!ex) return;
    if (this.queue.length) this.input();
    ex.web_render(n);
    const left = new Float32Array(ex.memory.buffer, ex.web_out_l(), n);
    l.set(left);
    for (let i = 0; i < n; i += 4) this.lvSum = (this.lvSum || 0) + left[i] * left[i];
    this.lvN = (this.lvN || 0) + Math.ceil(n / 4);
    if (r) r.set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
    if (time - this.lastFrame >= this.period) { this.lastFrame = time; this.publish(); }
  }
}
"""

WORKLET = CORE + r"""
class GolanaProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.core = new GolanaCore((m, t) => this.port.postMessage(m, t || []));
    this.port.onmessage = (e) => this.core.onMessage(e.data);
  }
  process(inputs, outputs) {
    const out = outputs[0];
    this.core.render(out[0], out[1], out[0].length, currentTime);
    return true;
  }
}
registerProcessor("felucca", GolanaProcessor);
"""

# the page's loader: the worklet from a blob, else the page's own thread
LOADER = r"""
function b64buf(s) { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u.buffer; }
async function golanaNode(ac) {
  try {
    const url = URL.createObjectURL(new Blob([document.getElementById("golana-worklet").textContent], { type: "text/javascript" }));
    await ac.audioWorklet.addModule(url);
    return new AudioWorkletNode(ac, "felucca", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
  } catch (e) {
    console.warn("Golana: the audio runs on the page's thread (" + (e && e.message || e) + ")");
    const GolanaCore = (0, eval)(document.getElementById("golana-core").textContent + ";GolanaCore");
    const port = { onmessage: null, postMessage(m) { core.onMessage(m); } };
    const core = new GolanaCore((m) => setTimeout(() => port.onmessage && port.onmessage({ data: m }), 0));
    const sp = ac.createScriptProcessor(1024, 0, 2);
    sp.onaudioprocess = (ev) => {
      const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
      for (let k = 0; k < L.length; k += 128)
        core.render(L.subarray(k, k + 128), R.subarray(k, k + 128), Math.min(128, L.length - k), ac.currentTime);
    };
    return { port, connect(d) { sp.connect(d); } };
  }
}
"""


def main(wasm_path, out_path):
    page = (HERE / "index.html").read_text()
    wasm = base64.b64encode(Path(wasm_path).read_bytes()).decode()
    woff = base64.b64encode((HERE / "fonts" / "DotGothic16-subset.woff").read_bytes()).decode()
    # the skeleton the Artifact host adds: no doctype / html / head / body of our own
    page = re.sub(r"<!doctype html>\s*", "", page, flags=re.I)
    page = re.sub(r"</?(html|head|body)[^>]*>\s*", "", page)
    page = re.sub(r'<meta (charset|name="viewport")[^>]*>\s*', "", page)
    page = re.sub(r'<link rel="icon"[^>]*>\s*', "", page)
    page = re.sub(r'<meta name="description"[^>]*>\s*', "", page)
    page = page.replace("<title>Felucca</title>", "<title>Golana</title>")
    page = page.replace('url("fonts/DotGothic16-subset.woff")', f'url("data:font/woff;base64,{woff}")')
    page = page.replace("color-scheme: light; }", "color-scheme: light; }\nbody { background: var(--bg); }", 1)
    page = page.replace('const STORE = "felucca-emu-flash";', 'const STORE = "golana-emu-flash";')
    page = page.replace(
        'fetch("felucca.wasm").then((r) => { if (!r.ok) throw new Error(`felucca.wasm: ${r.status}`); return r.arrayBuffer(); }),\n'
        '      ac.audioWorklet.addModule("worklet.js")]);\n'
        '    node = new AudioWorkletNode(ac, "felucca", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });',
        'Promise.resolve(b64buf(GOLANA_WASM))]);\n    node = await golanaNode(ac);')
    assert "golanaNode(ac)" in page, "index.html's loader changed: update make_golana_page.py"
    page = page.replace("Felucca: this browser", "Golana: this browser").replace("Felucca: the browser", "Golana: the browser")
    page = re.sub(r"<footer>.*?</footer>", """<footer>
  <strong>Golana</strong> by Gadlele, based on Felucca by Leo Kuroshita (Hügelton Instruments) ·
  <a href="https://github.com/Gadlele/Golana" target="_blank" rel="noopener">Source</a> ·
  <a href="https://www.gnu.org/licenses/gpl-3.0.html" target="_blank" rel="noopener">GPL-3.0</a><br>
  <small>The same code the FM-1 runs, on a simulated FM-1. M-VAVE and FM-1 are trademarks of their owners;
  microKORG is a trademark of Korg. Golana is not affiliated with them.</small>
</footer>""", page, flags=re.S)
    # a quick guide under the panel
    page = page.replace('<p class="keys">', """<section class="guide">
  <p><b>SELECT</b> section · <b>KNOB 1–4</b> parameters · <b>ALGORITHM</b> cutoff · <b>PRESETS</b> program 1–32</p>
  <p><b>HOME</b> perform · <b>EDIT</b> oscillators · <b>SCL</b> filter · <b>ENV</b> envelopes · <b>LFO</b> LFOs ·
     <b>GLO</b> amp, voice, global · <b>REC</b> patch · <b>SEQ</b> arp pages · <b>FX</b> effects</p>
  <p><b>ARP</b> arp on/off · <b>PLAY</b> latch · <b>SAVE</b> write a program · <b>OCT</b> octave</p>
</section>
<p class="keys">""", 1)
    page = page.replace("footer small { font-size: 11px; opacity: .8; }",
                        "footer small { font-size: 11px; opacity: .8; }\n"
                        ".guide { margin: 14px auto 0; max-width: 46em; color: var(--fg); text-align: center; }\n"
                        ".guide p { margin: 2px 0; }")
    scripts = (f'<script id="golana-worklet" type="text/plain">{WORKLET}</script>\n'
               f'<script id="golana-core" type="text/plain">{CORE}</script>\n'
               f'<script>const GOLANA_WASM = "{wasm}";\n{LOADER}</script>\n')
    page = page.replace('<script type="module">', scripts + '<script type="module">', 1)
    Path(out_path).write_text(page)
    print(f"{out_path}: {len(page)} bytes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
