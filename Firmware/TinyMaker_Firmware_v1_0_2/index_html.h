#ifndef INDEX_HTML_H
#define INDEX_HTML_H

// Web UI page served at GET /. Kept in a header so the raw string literal does not
// confuse the Arduino .ino prototype generator (ctags).
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyMaker</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
         background:#111; color:#eee; }
  header { background:#e8720c; color:#111; padding:14px 16px; font-weight:700; font-size:20px; }
  main { max-width:640px; margin:0 auto; padding:16px; }
  .card { background:#1b1b1b; border:1px solid #2c2c2c; border-radius:10px; padding:14px; margin:12px 0; }
  h2 { margin:0 0 10px; font-size:15px; color:#e8720c; text-transform:uppercase; letter-spacing:.05em; }
  .row { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
  button { background:#e8720c; color:#111; border:0; border-radius:8px; padding:10px 14px;
           font-weight:700; font-size:14px; cursor:pointer; }
  button.sec { background:#333; color:#eee; }
  button.stop { background:#c0392b; color:#fff; }
  input[type=text] { background:#111; border:1px solid #333; color:#eee; border-radius:8px;
                     padding:9px; font-size:14px; }
  .filebtn { display:inline-block; background:#333; color:#eee; border:1px solid #444;
             border-radius:8px; padding:10px 14px; font-weight:700; font-size:14px; cursor:pointer; }
  .filebtn input { display:none; }
  .folder { display:flex; justify-content:space-between; align-items:center; gap:8px;
            padding:8px 0; border-bottom:1px solid #262626; }
  .folder:last-child { border-bottom:0; }
  .muted { color:#888; font-size:13px; }
  .warn { background:#3a2100; border:1px solid #a35a00; color:#ffcf99; border-radius:8px;
          padding:10px; font-size:13px; margin-top:8px; }
  #stat { font-size:14px; line-height:1.6; }
  .pill { display:inline-block; padding:2px 8px; border-radius:999px; font-size:12px; background:#333; }
  .pill.on { background:#1f7a1f; color:#fff; }
  progress { width:100%; height:12px; }
  #modal { display:none; position:fixed; inset:0; background:rgba(0,0,0,.65);
           align-items:center; justify-content:center; padding:16px; z-index:20; }
  #modal.show { display:flex; }
  .sheet { background:#1b1b1b; border:1px solid #3a3a3a; border-radius:12px;
           max-width:360px; width:100%; padding:18px; }
  .sheet h2 { margin-top:0; color:#e8720c; }
  #mbody { font-size:15px; line-height:1.7; }
</style>
</head>
<body>
<header>TinyMaker &middot; Remote</header>
<main>
  <div class="card">
    <h2>Status</h2>
    <div id="stat">Loading&hellip;</div>
    <div class="row" style="margin-top:10px">
      <button class="stop" onclick="cmd('/stop')">Stop</button>
      <button class="sec" onclick="cmd('/pause')">Pause</button>
      <button class="sec" onclick="cmd('/resume')">Resume</button>
    </div>
  </div>

  <div class="card">
    <h2>Print Folders</h2>
    <div id="folders" class="muted">Loading&hellip;</div>
    <div class="warn">Starting a print begins UV exposure and Z motion. Make sure resin
      and the build plate are ready and someone can supervise.</div>
  </div>

  <div class="card">
    <h2>Upload a Print</h2>
    <p class="muted">Send the sliced PNG layers (1.png, 2.png &hellip;) to the SD card under
      the folder name below. On a computer use <b>Choose folder</b>; on a phone use
      <b>Choose files</b> and select all the PNGs.</p>
    <div class="row"><input type="text" id="dest" placeholder="folder name (e.g. dragon)" style="flex:1"></div>
    <div class="row" style="margin-top:8px">
      <label class="filebtn">Choose folder<input type="file" id="dirinput" webkitdirectory directory multiple></label>
      <label class="filebtn">Choose files<input type="file" id="fileinput" accept=".png,image/png" multiple></label>
    </div>
    <div id="picked" class="muted" style="margin-top:8px"></div>
    <div class="row" style="margin-top:8px"><button onclick="upload()">Upload</button></div>
    <progress id="prog" value="0" max="100" style="display:none"></progress>
    <div id="uplog" class="muted"></div>
  </div>

  <div class="card">
    <h2>WiFi Setup</h2>
    <div id="wifinow" class="muted">&hellip;</div>
    <div class="row" style="margin-top:8px">
      <select id="ssidsel" style="flex:1;min-width:0;background:#111;border:1px solid #333;color:#eee;border-radius:8px;padding:9px;font-size:14px">
        <option value="">Scan for networks&hellip;</option>
      </select>
      <button class="sec" onclick="scanWifi()">Scan</button>
    </div>
    <div class="row" style="margin-top:8px"><input type="text" id="ssidman" placeholder="or type network name" style="flex:1"></div>
    <div class="row" style="margin-top:8px"><input type="password" id="wpass" placeholder="WiFi password" style="flex:1"></div>
    <div class="row" style="margin-top:8px"><button onclick="saveWifi()">Save &amp; Restart</button></div>
    <div id="wifimsg" class="muted"></div>
  </div>
</main>

<div id="modal">
  <div class="sheet">
    <h2 id="mtitle">Start print?</h2>
    <div id="mbody" class="muted">Processing files&hellip;</div>
    <div class="row" style="margin-top:16px;justify-content:flex-end">
      <button class="sec" onclick="closeModal()">Cancel</button>
      <button id="mstart" onclick="doStart()">Start Print</button>
    </div>
  </div>
</div>

<script>
const $ = s => document.querySelector(s);
const STATE = ["Homing","Curing","Lifting","Dropping","Canceling","Pausing","Paused","Resuming","Finished"];
let wasPrinting = false;

async function refresh() {
  try {
    const r = await fetch('/status'); const s = await r.json();
    let html = 'Network: <span class="pill on">'+s.wifi+'</span> '+s.ip+'<br>';
    html += 'Free RAM: '+s.heap+' KB &middot; up '+Math.floor(s.uptime/60)+'m '+(s.uptime%60)+'s<br>';
    if (s.printing) {
      const pct = s.layers>0 ? Math.round(100*s.layer/s.layers) : 0;
      html += 'Printing <b>'+s.folder+'</b>'+(s.paused?' <span class="pill">PAUSED</span>':'')+'<br>';
      html += 'State: '+(STATE[s.state]||s.state)+'<br>';
      html += 'Layer '+s.layer+' / '+s.layers+' ('+pct+'%)<br>';
      html += 'Est. remaining: '+s.eta_h+'h '+s.eta_m+'m<br>';
      html += '<progress value="'+pct+'" max="100"></progress>';
    } else { html += '<span class="pill">Idle</span> &mdash; ready to print.'; }
    $('#stat').innerHTML = html;
    const wn = $('#wifinow');
    if (wn) wn.innerHTML = (s.wifi === 'AP')
      ? 'In <b>hotspot</b> mode &mdash; pick your network below to connect the printer to your WiFi.'
      : 'Connected to <b>' + s.ssid + '</b> (' + s.ip + ').';
    // When a print finishes, repopulate the folder list (it was blocked mid-print).
    if (wasPrinting && !s.printing) loadFolders();
    wasPrinting = s.printing;
  } catch(e) { $('#stat').textContent = 'Status unavailable'; }
}

async function loadFolders() {
  try {
    const r = await fetch('/list');
    if (r.status === 503) { // SD card is in use by the print
      $('#folders').innerHTML = '<span class="muted">SD card is busy printing &mdash; the folder list is unavailable until the print finishes.</span>';
      return;
    }
    if (!r.ok) return;
    const a = await r.json();
    if (!a.length) { $('#folders').textContent = 'No print folders on the SD card yet.'; return; }
    $('#folders').innerHTML = a.map(f =>
      '<div class="folder"><span>'+f+'</span><span class="row">'+
      '<button onclick="start(\''+f+'\')">Print</button>'+
      '<button class="sec" onclick="del(\''+f+'\')">Delete</button></span></div>').join('');
  } catch(e) { $('#folders').textContent = 'Folder list unavailable'; }
}

async function cmd(u) { await fetch(u); setTimeout(refresh, 300); }

let pendingFolder = null;

// Print button: run the "processing" step (count layers + estimate) and show a modal
// with the numbers before committing to Start.
async function start(f) {
  pendingFolder = f;
  $('#mtitle').textContent = 'Print "' + f + '"?';
  $('#mbody').innerHTML = 'Processing files&hellip;';
  $('#mstart').disabled = true;
  document.getElementById('modal').classList.add('show');
  try {
    const r = await fetch('/preview?folder=' + encodeURIComponent(f));
    const p = await r.json();
    if (!p.layers) {
      $('#mbody').innerHTML = 'No printable layers found. The files must be named ' +
        '<b>1.png, 2.png, 3.png &hellip;</b> inside the folder.';
      return; // Start stays disabled
    }
    let warn = '<div class="warn">UV exposure and Z motion will begin - make sure resin and the plate are ready.</div>';
    if (p.tall) warn += '<div class="warn">Height exceeds the max build volume.</div>';
    $('#mbody').innerHTML =
      'Layers: <b>' + p.layers + '</b><br>Height: <b>' + p.height + ' mm</b><br>' +
      'Est. time: <b>' + p.eta_h + 'h ' + p.eta_m + 'm</b>' + warn;
    $('#mstart').disabled = false;
  } catch (e) {
    $('#mbody').textContent = 'Could not read that folder.';
  }
}

function closeModal() {
  document.getElementById('modal').classList.remove('show');
  pendingFolder = null;
}

async function doStart() {
  if (!pendingFolder) return;
  const f = pendingFolder;
  const r = await fetch('/start?folder=' + encodeURIComponent(f));
  closeModal();
  if (!r.ok) alert('Could not start: ' + (await r.text()));
  setTimeout(refresh, 500);
}

async function scanWifi() {
  $('#wifimsg').textContent = 'Scanning...';
  try {
    const r = await fetch('/scan');
    const a = await r.json();
    const sel = $('#ssidsel');
    sel.innerHTML = '<option value="">Select a network...</option>';
    a.sort((x, y) => y.rssi - x.rssi).forEach(n => {
      const o = document.createElement('option');
      o.value = n.ssid;
      o.textContent = n.ssid + (n.lock ? ' (locked)' : '') + '  ' + n.rssi + 'dBm';
      sel.appendChild(o);
    });
    $('#wifimsg').textContent = a.length + ' networks found.';
  } catch (e) { $('#wifimsg').textContent = 'Scan unavailable (busy printing?).'; }
}

async function saveWifi() {
  const ssid = ($('#ssidman').value.trim() || $('#ssidsel').value).trim();
  const pass = $('#wpass').value;
  if (!ssid) { alert('Pick or type a network name.'); return; }
  if (!confirm('Save WiFi "' + ssid + '" and restart the printer?')) return;
  $('#wifimsg').textContent = 'Saving...';
  try {
    // The printer reboots right after saving, so this request usually won't return.
    await fetch('/savewifi?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass));
  } catch (e) {}
  $('#wifimsg').innerHTML = 'Saved. The printer is restarting to join <b>' + ssid + '</b>.<br>' +
    'Reconnect your device to that network, then open <b>http://tinymaker.local</b>.';
}

async function del(f) {
  if (!confirm('Delete folder "'+f+'" from the SD card?')) return;
  await fetch('/delete?folder='+encodeURIComponent(f)); loadFolders();
}

// Gather the chosen files from whichever input was used (folder or multi-file).
function chosenFiles() {
  const d = $('#dirinput').files, f = $('#fileinput').files;
  const list = (d && d.length) ? Array.from(d) : Array.from(f || []);
  // Only PNG slices, sorted by their numeric name so upload order is natural.
  return list.filter(x => /\.png$/i.test(x.name))
             .sort((a,b) => (parseInt(a.name)||0) - (parseInt(b.name)||0));
}

function showPicked() {
  const files = chosenFiles();
  if (!files.length) { $('#picked').textContent = ''; return; }
  // Auto-fill the folder name from the picked directory, if empty.
  if (!$('#dest').value.trim() && files[0].webkitRelativePath) {
    $('#dest').value = files[0].webkitRelativePath.split('/')[0] || '';
  }
  $('#picked').textContent = files.length + ' PNG file' + (files.length===1?'':'s') + ' selected.';
}
document.getElementById('dirinput').addEventListener('change', showPicked);
document.getElementById('fileinput').addEventListener('change', showPicked);

function upload() {
  const files = chosenFiles();
  let dest = $('#dest').value.trim();
  if (!files.length) { alert('Choose a folder or PNG files first.'); return; }
  if (!dest) { dest = (files[0].webkitRelativePath || '').split('/')[0] || 'netprint'; }
  $('#prog').style.display = 'block';
  $('#prog').value = 0;
  let i = 0, failed = 0;
  const next = () => {
    if (i >= files.length) {
      $('#uplog').textContent = 'Done: ' + (files.length - failed) + ' / ' + files.length +
        ' uploaded to "' + dest + '"' + (failed ? ' (' + failed + ' failed - try Upload again)' : '.');
      loadFolders();
      return;
    }
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload?folder='+encodeURIComponent(dest)+'&name='+encodeURIComponent(files[i].name));
    // Continue on error so one dropped file doesn't abort the whole job.
    xhr.onload  = () => { if (xhr.status !== 200) failed++; step(); };
    xhr.onerror = () => { failed++; step(); };
    xhr.send(files[i]); // raw file body; browser sets Content-Length
  };
  const step = () => {
    i++;
    $('#prog').value = Math.round(100*i/files.length);
    $('#uplog').textContent = 'Uploading ' + i + ' / ' + files.length + '...';
    next();
  };
  $('#uplog').textContent = 'Uploading to "'+dest+'"...';
  next();
}

refresh(); loadFolders();
setInterval(refresh, 2000);
</script>
</body>
</html>)HTML";

#endif // INDEX_HTML_H
