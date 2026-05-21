Module['arguments'] = ['--bios=bios/pcxtbios.bin', '--hostdir0=/hostfs'];

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function() {
  FS.mkdir('/hostfs');
});

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

  function loadLocalFile(drive, file) {
    var label = drive === 0 ? 'A' : 'B';
    var status = document.getElementById('disk-status');
    if (status) status.textContent = label + ': loading...';
    var reader = new FileReader();
    reader.onload = function() {
      var data = new Uint8Array(reader.result);
      var ptr = Module._malloc(data.length);
      Module.HEAPU8.set(data, ptr);
      var rc = Module.ccall('lilpc_load_disk', 'number',
        ['number', 'number', 'number'], [drive, ptr, data.length]);
      Module._free(ptr);
      if (rc === 0) {
        if (status) status.textContent = label + ': ' + file.name +
          ' (' + data.length + ' bytes)';
      } else {
        if (status) status.textContent = label + ': load failed';
      }
    };
    reader.readAsArrayBuffer(file);
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

    var fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.img,.ima,.bin,.dsk,.flp';
    fileInput.style.cssText = 'display:none;';
    row.appendChild(fileInput);

    var fileBtn = document.createElement('button');
    fileBtn.textContent = 'Browse…';
    fileBtn.style.cssText = 'background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);' +
      'color:#ccc;font:13px/1 monospace;padding:4px 10px;border-radius:4px;cursor:pointer;';
    row.appendChild(fileBtn);

    sel.addEventListener('change', function() {
      if (sel.value) {
        loadDisk(drive, sel.value);
      } else {
        loadDisk(drive, '');
      }
    });

    fileBtn.addEventListener('click', function() { fileInput.click(); });
    fileInput.addEventListener('change', function() {
      if (fileInput.files.length > 0) {
        sel.value = '';
        loadLocalFile(drive, fileInput.files[0]);
      }
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

  /* ---- turbo toggle ---- */
  var turboRow = document.createElement('div');
  turboRow.style.cssText = 'display:flex;align-items:center;gap:6px;';

  var turboBtn = document.createElement('button');
  turboBtn.style.cssText = 'background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);' +
    'color:#ccc;font:13px/1 monospace;padding:4px 10px;border-radius:4px;cursor:pointer;';

  function updateTurboBtn() {
    var on = Module.ccall('lilpc_get_turbo', 'number', [], []);
    turboBtn.textContent = on ? 'Turbo: ON (286)' : 'Turbo: OFF (8088)';
    turboBtn.style.color = on ? '#4f4' : '#ccc';
  }
  turboBtn.addEventListener('click', function() {
    var on = Module.ccall('lilpc_get_turbo', 'number', [], []);
    Module.ccall('lilpc_set_turbo', null, ['number'], [on ? 0 : 1]);
    updateTurboBtn();
  });
  updateTurboBtn();
  turboRow.appendChild(turboBtn);
  toolbar.appendChild(turboRow);

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
    if (disks.length >= 1) {
      selA.value = disks[0].url;
      loadDisk(0, disks[0].url);
    }
    if (disks.length >= 2) {
      selB.value = disks[1].url;
      loadDisk(1, disks[1].url);
    }
  }).catch(function() {});

  /* ---- host filesystem (H: drive) ---- */
  var hostfsCount = 0;

  function hostfsEnsureDir(path) {
    var parts = path.split('/');
    var cur = '';
    for (var i = 0; i < parts.length; i++) {
      if (!parts[i]) continue;
      cur += '/' + parts[i];
      try { FS.mkdir(cur); } catch (e) { /* EEXIST */ }
    }
  }

  function hostfsAddFile(relPath, data) {
    var full = '/hostfs/' + relPath;
    var dir = full.substring(0, full.lastIndexOf('/'));
    hostfsEnsureDir(dir);
    FS.writeFile(full, new Uint8Array(data));
    hostfsCount++;
    var el = document.getElementById('hostfs-status');
    if (el) el.textContent = hostfsCount + ' file(s)';
  }

  function readAllEntries(reader, callback) {
    var all = [];
    (function batch() {
      reader.readEntries(function(entries) {
        if (!entries.length) { callback(all); return; }
        all = all.concat(Array.prototype.slice.call(entries));
        batch();
      });
    })();
  }

  function traverseEntry(entry, prefix) {
    if (entry.isFile) {
      entry.file(function(file) {
        var rd = new FileReader();
        rd.onload = function() { hostfsAddFile(prefix + file.name, rd.result); };
        rd.readAsArrayBuffer(file);
      });
    } else if (entry.isDirectory) {
      readAllEntries(entry.createReader(), function(entries) {
        for (var i = 0; i < entries.length; i++)
          traverseEntry(entries[i], prefix + entry.name + '/');
      });
    }
  }

  /* H: toolbar row */
  var hostfsRow = document.createElement('div');
  hostfsRow.style.cssText = 'display:flex;align-items:center;gap:6px;';

  var hostfsLbl = document.createElement('span');
  hostfsLbl.textContent = 'H:';
  hostfsLbl.style.cssText = 'color:#ccc;font:13px/1 monospace;min-width:20px;';
  hostfsRow.appendChild(hostfsLbl);

  var hostfsStat = document.createElement('span');
  hostfsStat.id = 'hostfs-status';
  hostfsStat.textContent = 'drag files to upload';
  hostfsStat.style.cssText = 'color:#888;font:13px/1 monospace;flex:1;';
  hostfsRow.appendChild(hostfsStat);

  var hostfsInput = document.createElement('input');
  hostfsInput.type = 'file';
  hostfsInput.multiple = true;
  hostfsInput.style.display = 'none';
  hostfsRow.appendChild(hostfsInput);

  var btnCss = 'background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);' +
    'color:#ccc;font:13px/1 monospace;padding:4px 10px;border-radius:4px;cursor:pointer;';

  var uploadBtn = document.createElement('button');
  uploadBtn.textContent = 'Upload…';
  uploadBtn.style.cssText = btnCss;
  hostfsRow.appendChild(uploadBtn);

  var filesBtn = document.createElement('button');
  filesBtn.textContent = 'Files…';
  filesBtn.style.cssText = btnCss;
  hostfsRow.appendChild(filesBtn);

  uploadBtn.addEventListener('click', function() { hostfsInput.click(); });
  hostfsInput.addEventListener('change', function() {
    for (var i = 0; i < hostfsInput.files.length; i++) {
      (function(file) {
        var rd = new FileReader();
        rd.onload = function() { hostfsAddFile(file.name, rd.result); };
        rd.readAsArrayBuffer(file);
      })(hostfsInput.files[i]);
    }
  });

  toolbar.insertBefore(hostfsRow, statusEl);

  /* canvas drag-and-drop */
  var cv = document.getElementById('canvas');
  cv.addEventListener('dragover', function(e) {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
  });
  cv.addEventListener('drop', function(e) {
    e.preventDefault();
    if (e.dataTransfer.items) {
      for (var i = 0; i < e.dataTransfer.items.length; i++) {
        var entry = e.dataTransfer.items[i].webkitGetAsEntry
          ? e.dataTransfer.items[i].webkitGetAsEntry() : null;
        if (entry) {
          traverseEntry(entry, '');
        } else if (e.dataTransfer.items[i].kind === 'file') {
          (function(file) {
            var rd = new FileReader();
            rd.onload = function() { hostfsAddFile(file.name, rd.result); };
            rd.readAsArrayBuffer(file);
          })(e.dataTransfer.items[i].getAsFile());
        }
      }
    }
  });

  /* file browser panel */
  function showFileBrowser(fsPath, dosLabel) {
    var old = document.getElementById('hostfs-browser');
    if (old) old.remove();

    var panel = document.createElement('div');
    panel.id = 'hostfs-browser';
    panel.style.cssText = 'position:absolute;bottom:200px;left:12px;' +
      'background:#1a1a1a;border:1px solid rgba(255,255,255,0.2);' +
      'border-radius:6px;padding:8px;z-index:20;min-width:280px;' +
      'max-height:400px;overflow-y:auto;font:13px/1.6 monospace;color:#ccc;';

    var hdr = document.createElement('div');
    hdr.style.cssText = 'display:flex;justify-content:space-between;align-items:center;' +
      'margin-bottom:6px;padding-bottom:4px;border-bottom:1px solid rgba(255,255,255,0.1);';
    var ttl = document.createElement('span');
    ttl.textContent = dosLabel;
    ttl.style.color = '#4af';
    hdr.appendChild(ttl);
    var xBtn = document.createElement('button');
    xBtn.textContent = '×';
    xBtn.style.cssText = 'background:none;border:none;color:#888;' +
      'font-size:18px;cursor:pointer;padding:0 4px;';
    xBtn.addEventListener('click', function() { panel.remove(); });
    hdr.appendChild(xBtn);
    panel.appendChild(hdr);

    var entries;
    try { entries = FS.readdir(fsPath); } catch (e) { entries = []; }
    entries.sort();

    var fileCount = 0;
    for (var i = 0; i < entries.length; i++) {
      var name = entries[i];
      if (name === '.') continue;

      var st;
      try { st = FS.stat(fsPath + '/' + name); } catch (e) { continue; }
      var isDir = FS.isDir(st.mode);

      var row = document.createElement('div');
      row.style.cssText = 'cursor:pointer;padding:2px 4px;border-radius:3px;';
      row.addEventListener('mouseenter', function() {
        this.style.background = 'rgba(255,255,255,0.08)';
      });
      row.addEventListener('mouseleave', function() {
        this.style.background = 'none';
      });

      if (isDir) {
        row.textContent = '[' + name + ']';
        row.style.color = '#ff4';
        (function(n) {
          var childFs = fsPath + '/' + n;
          var childDos = (n === '..')
            ? dosLabel.replace(/[^\\]+\\$/, '') || 'H:\\'
            : dosLabel + n.toUpperCase() + '\\';
          row.addEventListener('click', function() {
            showFileBrowser(childFs, childDos);
          });
        })(name);
      } else {
        row.textContent = name + '  (' + st.size + ')';
        fileCount++;
        (function(n) {
          row.addEventListener('click', function() {
            var data = FS.readFile(fsPath + '/' + n);
            var blob = new Blob([data], {type: 'application/octet-stream'});
            var url = URL.createObjectURL(blob);
            var a = document.createElement('a');
            a.href = url;
            a.download = n;
            a.click();
            URL.revokeObjectURL(url);
          });
        })(name);
      }
      panel.appendChild(row);
    }

    if (fileCount === 0 && entries.length <= 2) {
      var empty = document.createElement('div');
      empty.textContent = '(empty)';
      empty.style.color = '#666';
      panel.appendChild(empty);
    }

    document.body.appendChild(panel);
  }

  filesBtn.addEventListener('click', function() {
    var existing = document.getElementById('hostfs-browser');
    if (existing) { existing.remove(); return; }
    showFileBrowser('/hostfs', 'H:\\');
  });
});
