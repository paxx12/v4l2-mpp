HTML_CONTROL = """<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
  <title>V4L2 Controls</title>
  <base href=\"./\">
  <style>
    :root {
      color-scheme: light dark;
      --bg: #0f1115;
      --panel: #1b1f2a;
      --panel-strong: #222838;
      --tile: #1c2230;
      --accent: #63b3ff;
      --accent-strong: #4ea1ff;
      --text: #e6e9ef;
      --muted: #9aa3b2;
      --border: #2b3040;
      --shadow: 0 12px 30px rgba(0, 0, 0, 0.35);
      --gradient: linear-gradient(145deg, rgba(78, 161, 255, 0.12), rgba(0, 0, 0, 0));
    }
    [data-theme=\"light\"] {
      color-scheme: light;
      --bg: #f6f7fb;
      --panel: #ffffff;
      --panel-strong: #f0f2f7;
      --tile: #ffffff;
      --accent: #3d7bff;
      --accent-strong: #2f62d8;
      --text: #1a1f2b;
      --muted: #5c677d;
      --border: #e3e6ef;
      --shadow: 0 12px 30px rgba(30, 46, 90, 0.12);
      --gradient: linear-gradient(145deg, rgba(61, 123, 255, 0.1), rgba(255, 255, 255, 0));
    }
    body {
      margin: 0;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    header {
      padding: 16px 20px;
      background: var(--panel);
      border-bottom: 1px solid var(--border);
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }
    header h1 {
      margin: 0;
      font-size: 20px;
    }
    main {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 16px;
      padding: 16px;
    }
    .panel {
      background: var(--panel);
      border-radius: 16px;
      padding: 16px;
      box-shadow: var(--shadow);
      border: 1px solid var(--border);
    }
    .panel--preview {
      background: var(--panel);
      background-image: var(--gradient);
    }
    .panel h2 {
      margin-top: 0;
      font-size: 16px;
    }
    label {
      display: block;
      font-size: 13px;
      color: var(--muted);
      margin-bottom: 6px;
    }
    select, input[type=\"number\"], input[type=\"range\"] {
      width: 100%;
      padding: 10px 12px;
      border-radius: 10px;
      border: 1px solid var(--border);
      background: var(--panel-strong);
      color: var(--text);
      font-size: 14px;
      box-sizing: border-box;
    }
    .row {
      display: flex;
      gap: 8px;
      align-items: center;
    }
    .row input[type=\"range\"] {
      flex: 1;
    }
    button {
      width: 100%;
      padding: 12px;
      border-radius: 10px;
      background: var(--accent);
      color: #0b1020;
      font-weight: 600;
      border: none;
      cursor: pointer;
      transition: transform 0.1s ease, box-shadow 0.2s ease;
    }
    button:hover {
      transform: translateY(-1px);
      box-shadow: 0 6px 18px rgba(0, 0, 0, 0.2);
    }
    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
    }
    .note {
      margin-top: 8px;
      font-size: 13px;
      color: #8dd4ff;
    }
    .preview {
      width: 100%;
      aspect-ratio: 16/9;
      background: #0b0e14;
      border-radius: 14px;
      overflow: hidden;
      display: flex;
      align-items: center;
      justify-content: center;
      border: 1px solid var(--border);
    }
    .preview iframe, .preview img {
      width: 100%;
      height: 100%;
      object-fit: contain;
      border: 0;
    }
    .control {
      background: var(--tile);
      padding: 14px;
      border-radius: 14px;
      border: 1px solid var(--border);
      box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.02);
    }
    .control-title {
      font-size: 14px;
      margin-bottom: 6px;
    }
    .control-grid {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    }
    .section-title {
      font-size: 14px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      color: var(--muted);
      margin: 18px 0 10px;
    }
    .slider-row {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 10px;
      align-items: center;
    }
    input[type=\"range\"] {
      height: 36px;
      accent-color: var(--accent-strong);
    }
    .control input[type=\"number\"] {
      margin-top: 10px;
    }
    .value-pill {
      min-width: 62px;
      padding: 8px 12px;
      border-radius: 999px;
      background: var(--panel-strong);
      border: 1px solid var(--border);
      text-align: center;
      font-weight: 600;
      font-size: 13px;
      color: var(--text);
    }
    .status {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, \"Liberation Mono\", monospace;
      font-size: 12px;
      background: var(--panel-strong);
      padding: 10px;
      border-radius: 12px;
      white-space: pre-wrap;
      min-height: 80px;
      border: 1px solid var(--border);
    }
    .action-bar {
      position: sticky;
      bottom: 0;
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      padding-top: 14px;
      background: linear-gradient(180deg, rgba(0, 0, 0, 0), var(--panel) 60%);
    }
    .ghost {
      background: transparent;
      color: var(--text);
      border: 1px solid var(--border);
    }
    .theme-select {
      min-width: 140px;
    }
  </style>
</head>
<body>
  <header>
    <h1>V4L2 Controls</h1>
    <div style=\"display:flex; gap:8px; align-items:center;\">
      <label for=\"theme-select\" style=\"margin-bottom:0;\">Theme</label>
      <select id=\"theme-select\" class=\"theme-select\">
        <option value=\"system\">System</option>
        <option value=\"dark\">Dark</option>
        <option value=\"light\">Light</option>
      </select>
    </div>
  </header>
  <main>
    <section class=\"panel panel--preview\">
      <h2>Preview</h2>
      <label for=\"preview-mode\">Preview mode</label>
      <select id=\"preview-mode\">
        <option value=\"webrtc\">WebRTC</option>
        <option value=\"mjpg\">MJPG</option>
        <option value=\"snapshot\">Snapshot</option>
      </select>
      <div class=\"preview\" id=\"preview\" style=\"margin-top:12px;\"></div>
      <div class=\"note\">Control settings are persisted by the backend service.</div>
    </section>
    <section class=\"panel\">
      <div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;\">
        <h2 style=\"margin:0;\">Controls</h2>
        <label style=\"display:flex; gap:8px; align-items:center; cursor:pointer; margin:0;\">
          <input type=\"checkbox\" id=\"auto-apply\" style=\"width:auto; cursor:pointer;\" />
          <span style=\"font-size:14px;\">Auto-apply</span>
        </label>
      </div>
      <div id=\"controls\"></div>
      <div class=\"action-bar\">
        <button id=\"apply\">Apply changes</button>
        <button id=\"reset\" class=\"ghost\">Reset</button>
      </div>
    </section>
    <section class=\"panel\">
      <h2>Status</h2>
      <div class=\"status\" id=\"status\">Ready.</div>
    </section>
  </main>
  <script>
    const previewMode = document.getElementById('preview-mode');
    const preview = document.getElementById('preview');
    const controlsContainer = document.getElementById('controls');
    const applyButton = document.getElementById('apply');
    const resetButton = document.getElementById('reset');
    const statusBox = document.getElementById('status');
    const themeSelect = document.getElementById('theme-select');
    const autoApplyCheckbox = document.getElementById('auto-apply');

    let currentControls = [];
    let lastControls = [];
    let autoApplyTimeout = null;
    let rpcCounter = 1;

    const storagePrefix = `v4l2ctrls-${window.location.pathname.replace(/\W+/g, '-')}`;
    const storageKey = (key) => `${storagePrefix}-${key}`;

    const GROUPS = [
      { key: 'focus', title: 'Focus', match: name => name.includes('focus') },
      { key: 'exposure', title: 'Exposure', match: name => name.includes('exposure') },
      { key: 'white_balance', title: 'White Balance', match: name => name.includes('white_balance') },
      { key: 'color', title: 'Color', match: name => ['brightness', 'contrast', 'saturation', 'hue', 'gain', 'gamma'].some(t => name.includes(t)) },
      { key: 'image', title: 'Image', match: name => ['sharpness', 'zoom', 'pan', 'tilt'].some(t => name.includes(t)) },
    ];

    const PREVIEW_PATHS = {
      webrtc: 'webrtc',
      mjpg: 'stream.mjpg',
      snapshot: 'snapshot.jpg',
    };

    function logStatus(message) {
      statusBox.textContent = message;
    }

    function applyTheme(theme) {
      if (theme === 'system') {
        document.documentElement.removeAttribute('data-theme');
      } else {
        document.documentElement.setAttribute('data-theme', theme);
      }
      localStorage.setItem(storageKey('theme'), theme);
    }

    function baseStreamUrl() {
      return new URL('..', window.location.href);
    }

    function buildPreviewUrl(mode) {
      const base = baseStreamUrl();
      return new URL(PREVIEW_PATHS[mode], base).toString();
    }

    function appendCacheBuster(url) {
      const marker = url.includes('?') ? '&' : '?';
      return `${url}${marker}t=${Date.now()}`;
    }

    function updatePreview() {
      const mode = previewMode.value;
      let previewUrl = buildPreviewUrl(mode);
      if (mode === 'snapshot') {
        previewUrl = appendCacheBuster(previewUrl);
      }
      if (mode === 'webrtc') {
        preview.innerHTML = `<iframe src="${previewUrl}"></iframe>`;
      } else if (mode === 'mjpg') {
        preview.innerHTML = `<img src="${previewUrl}" alt="MJPG stream" />`;
      } else {
        preview.innerHTML = `<img src="${previewUrl}" alt="Snapshot" />`;
      }
      localStorage.setItem(storageKey('preview-mode'), mode);
    }

    function buildControl(control) {
      const wrapper = document.createElement('div');
      wrapper.className = 'control';
      const title = document.createElement('div');
      title.className = 'control-title';
      title.textContent = control.name;
      wrapper.appendChild(title);

      if (control.type === 'int') {
        const row = document.createElement('div');
        row.className = 'slider-row';
        const range = document.createElement('input');
        range.type = 'range';
        range.min = control.min;
        range.max = control.max;
        range.step = control.step || 1;
        range.value = control.value;
        range.dataset.control = control.name;
        range.dataset.role = 'value';
        const pill = document.createElement('div');
        pill.className = 'value-pill';
        pill.textContent = String(control.value);
        const number = document.createElement('input');
        number.type = 'number';
        number.min = control.min;
        number.max = control.max;
        number.step = control.step || 1;
        number.value = control.value;
        number.dataset.control = control.name;
        number.dataset.role = 'value';
        range.addEventListener('input', () => {
          number.value = range.value;
          pill.textContent = range.value;
        });
        range.addEventListener('change', () => {
          scheduleAutoApply();
        });
        number.addEventListener('input', () => {
          range.value = number.value;
          pill.textContent = number.value;
        });
        number.addEventListener('change', () => {
          scheduleAutoApply();
        });
        row.appendChild(range);
        row.appendChild(pill);
        wrapper.appendChild(row);
        wrapper.appendChild(number);
      } else if (control.type === 'bool') {
        const select = document.createElement('select');
        select.dataset.control = control.name;
        select.dataset.role = 'value';
        const off = new Option('Off', '0');
        const on = new Option('On', '1');
        select.add(off);
        select.add(on);
        select.value = String(control.value || 0);
        select.addEventListener('change', () => {
          scheduleAutoApply();
        });
        wrapper.appendChild(select);
      } else if (control.type === 'menu') {
        const select = document.createElement('select');
        select.dataset.control = control.name;
        select.dataset.role = 'value';
        (control.menu || []).forEach(item => {
          const opt = new Option(item.label, String(item.value));
          select.add(opt);
        });
        select.value = String(control.value || 0);
        select.addEventListener('change', () => {
          scheduleAutoApply();
        });
        wrapper.appendChild(select);
      } else {
        const span = document.createElement('div');
        span.textContent = `Unsupported control type: ${control.type}`;
        wrapper.appendChild(span);
      }

      return wrapper;
    }

    function groupFor(name) {
      for (const group of GROUPS) {
        if (group.match(name)) {
          return group.key;
        }
      }
      return 'other';
    }

    function renderControls(controls) {
      controlsContainer.innerHTML = '';
      const buckets = {};
      controls.forEach(control => {
        const key = groupFor(control.name);
        if (!buckets[key]) {
          buckets[key] = [];
        }
        buckets[key].push(control);
      });
      const ordered = [...GROUPS.map(group => group.key), 'other'];
      ordered.forEach(key => {
        const items = buckets[key] || [];
        if (!items.length) {
          return;
        }
        const title = document.createElement('div');
        const group = GROUPS.find(g => g.key === key);
        title.className = 'section-title';
        title.textContent = group ? group.title : 'Other';
        controlsContainer.appendChild(title);
        const grid = document.createElement('div');
        grid.className = 'control-grid';
        items.forEach(control => {
          grid.appendChild(buildControl(control));
        });
        controlsContainer.appendChild(grid);
      });
    }

    async function rpc(method, params = {}) {
      const response = await fetch('rpc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ jsonrpc: '2.0', id: rpcCounter++, method, params }),
      });
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.error?.message || 'RPC failed');
      }
      if (data.error) {
        throw new Error(data.error.message || 'RPC error');
      }
      return data.result;
    }

    async function fetchControls(silent = false) {
      if (!silent) {
        logStatus('Loading controls...');
      }
      try {
        const data = await rpc('list');
        currentControls = data.controls || [];
        lastControls = JSON.parse(JSON.stringify(currentControls));
        renderControls(currentControls);
        if (!silent) {
          logStatus(`Loaded ${currentControls.length} controls.`);
        }
      } catch (err) {
        renderControls([]);
        if (!silent) {
          logStatus(`Error: ${err.message}`);
        }
      }
    }

    async function fetchInfo() {
      try {
        const data = await rpc('info');
        logStatus(data.info || 'No info.');
      } catch (err) {
        logStatus(`Error: ${err.message}`);
      }
    }

    function controlMap(controls) {
      const map = {};
      (controls || []).forEach(control => {
        map[control.name] = control;
      });
      return map;
    }

    function scheduleAutoApply() {
      if (!autoApplyCheckbox.checked) {
        return;
      }
      if (autoApplyTimeout) {
        clearTimeout(autoApplyTimeout);
      }
      autoApplyTimeout = setTimeout(() => {
        applyChanges();
      }, 500);
    }

    async function applyChanges() {
      const payload = {};
      const previous = controlMap(lastControls);
      controlsContainer.querySelectorAll('[data-control][data-role=\"value\"]').forEach(el => {
        const name = el.dataset.control;
        const parsed = parseInt(el.value, 10);
        if (Number.isNaN(parsed)) {
          return;
        }
        const before = previous[name];
        if (!before || before.value !== parsed) {
          payload[name] = parsed;
        }
      });
      if (!Object.keys(payload).length) {
        logStatus('No changes to apply.');
        return;
      }
      applyButton.disabled = true;
      try {
        const data = await rpc('set', { controls: payload });
        logStatus(`Applied: ${JSON.stringify(data.applied, null, 2)}`);
        await fetchControls(true);
        if (previewMode.value === 'snapshot') {
          updatePreview();
        } else {
          const snap = appendCacheBuster(buildPreviewUrl('snapshot'));
          const img = new Image();
          img.src = snap;
        }
      } catch (err) {
        logStatus(`Error: ${err.message}`);
      } finally {
        applyButton.disabled = false;
      }
    }

    async function init() {
      const storedTheme = localStorage.getItem(storageKey('theme')) || 'system';
      themeSelect.value = storedTheme;
      applyTheme(storedTheme);
      const storedMode = localStorage.getItem(storageKey('preview-mode'));
      if (storedMode) {
        previewMode.value = storedMode;
      }
      const storedAutoApply = localStorage.getItem(storageKey('auto-apply'));
      if (storedAutoApply === 'true') {
        autoApplyCheckbox.checked = true;
      }
      updatePreview();
      await fetchControls();
      await fetchInfo();
    }

    previewMode.addEventListener('change', updatePreview);
    themeSelect.addEventListener('change', () => {
      applyTheme(themeSelect.value);
    });
    autoApplyCheckbox.addEventListener('change', () => {
      localStorage.setItem(storageKey('auto-apply'), autoApplyCheckbox.checked);
    });
    applyButton.addEventListener('click', applyChanges);
    resetButton.addEventListener('click', () => {
      if (!lastControls.length) {
        logStatus('No controls to reset.');
        return;
      }
      renderControls(lastControls);
      logStatus('Reset to last loaded values.');
    });

    init().catch(err => {
      logStatus(`Error: ${err.message}`);
    });
  </script>
</body>
</html>
"""
