Module['arguments'] = ['--bios=bios/pcxtbios.bin'];

/* ---------- lazy-fetch floppy disk UI ---------- */
Module['postRun'] = Module['postRun'] || [];
Module['postRun'].push(function() {
  /* ---- helpers ---- */
  function loadDisk(drive, url) {
    var label = drive === 0 ? 'A' : 'B';
    var status = document.getElementById('disk-status');
    if (!url) {
      Module.ccall('lilpc_eject_disk', null, ['number'], [drive]);
      if (status) status.textContent = label + ': ejected';
      return;
    }
    if (status) status.textContent = label + ': loading...';
    fetch(url).then(function(r) {
      if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
      return r.arrayBuffer();
    }).then(function(buf) {
      var data = new Uint8Array(buf);
      var ptr = Module._malloc(data.length);
      Module.HEAPU8.set(data, ptr);
      var rc = Module.ccall('lilpc_load_disk', 'number',
        ['number', 'number', 'number'], [drive, ptr, data.length]);
      Module._free(ptr);
      if (rc === 0) {
        if (status) status.textContent = label + ': ' + url.split('/').pop() +
          ' (' + data.length + ' bytes)';
      } else {
        if (status) status.textContent = label + ': load failed';
      }
    }).catch(function(err) {
      console.error('disk fetch error:', err);
      if (status) status.textContent = label + ': ' + err.message;
    });
  }

  function makeDriveUI(drive, parentEl) {
    var label = drive === 0 ? 'A:' : 'B:';
    var row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:6px;';

    var lbl = document.createElement('span');
    lbl.textContent = label;
    lbl.style.cssText = 'color:#ccc;font:13px/1 monospace;min-width:20px;';
    row.appendChild(lbl);

    var sel = document.createElement('select');
    sel.style.cssText = 'background:#222;color:#ccc;border:1px solid rgba(255,255,255,0.2);' +
      'font:13px/1 monospace;padding:4px 6px;border-radius:4px;max-width:160px;';
    var optNone = document.createElement('option');
    optNone.value = '';
    optNone.textContent = '(empty)';
    sel.appendChild(optNone);
    row.appendChild(sel);

    var urlInput = document.createElement('input');
    urlInput.type = 'text';
    urlInput.placeholder = 'URL or drop .img';
    urlInput.style.cssText = 'background:#222;color:#ccc;border:1px solid rgba(255,255,255,0.2);' +
      'font:13px/1 monospace;padding:4px 6px;border-radius:4px;width:200px;';
    row.appendChild(urlInput);

    var goBtn = document.createElement('button');
    goBtn.textContent = 'Load';
    goBtn.style.cssText = 'background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);' +
      'color:#ccc;font:13px/1 monospace;padding:4px 10px;border-radius:4px;cursor:pointer;';
    row.appendChild(goBtn);

    sel.addEventListener('change', function() {
      if (sel.value) {
        urlInput.value = sel.value;
        loadDisk(drive, sel.value);
      } else {
        loadDisk(drive, '');
      }
    });

    goBtn.addEventListener('click', function() {
      var url = urlInput.value.trim();
      loadDisk(drive, url);
    });

    /* drag-and-drop local files */
    urlInput.addEventListener('dragover', function(e) { e.preventDefault(); });
    urlInput.addEventListener('drop', function(e) {
      e.preventDefault();
      var file = e.dataTransfer.files[0];
      if (!file) return;
      urlInput.value = file.name;
      var reader = new FileReader();
      reader.onload = function() {
        var data = new Uint8Array(reader.result);
        var ptr = Module._malloc(data.length);
        Module.HEAPU8.set(data, ptr);
        var rc = Module.ccall('lilpc_load_disk', 'number',
          ['number', 'number', 'number'], [drive, ptr, data.length]);
        Module._free(ptr);
        var status = document.getElementById('disk-status');
        var l = drive === 0 ? 'A' : 'B';
        if (rc === 0) {
          if (status) status.textContent = l + ': ' + file.name +
            ' (' + data.length + ' bytes)';
        } else {
          if (status) status.textContent = l + ': load failed';
        }
      };
      reader.readAsArrayBuffer(file);
    });

    parentEl.appendChild(row);
    return sel;
  }

  /* ---- build the toolbar ---- */
  var toolbar = document.createElement('div');
  toolbar.id = 'disk-toolbar';
  toolbar.style.cssText = 'position:absolute;bottom:12px;left:12px;' +
    'display:flex;flex-direction:column;gap:6px;z-index:10;';

  var selA = makeDriveUI(0, toolbar);
  var selB = makeDriveUI(1, toolbar);

  var statusEl = document.createElement('div');
  statusEl.id = 'disk-status';
  statusEl.style.cssText = 'color:#888;font:11px/1 monospace;padding-left:26px;';
  toolbar.appendChild(statusEl);

  document.body.appendChild(toolbar);

  /* ---- populate dropdown from disk catalog ---- */
  /* The catalog is a JSON array: [{"name":"...", "url":"..."}, ...] */
  /* Look for catalog.json next to the page, fall back to empty list */
  fetch('disk/catalog.json').then(function(r) {
    if (!r.ok) return [];
    return r.json();
  }).then(function(disks) {
    if (!Array.isArray(disks)) return;
    disks.forEach(function(d) {
      var optA = document.createElement('option');
      optA.value = d.url;
      optA.textContent = d.name;
      selA.appendChild(optA.cloneNode(true));
      selB.appendChild(optA);
    });
  }).catch(function() {});
});
