// Smart Greenhouse dashboard: trạng thái 1 giây/lần, biểu đồ + lịch sử 5 giây/lần
const $ = (id) => document.getElementById(id);
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const LINK_NAMES = { bluetooth: 'Bluetooth', serial: 'Serial (dự phòng)', simulator: 'Giả lập', offline: 'Mất kết nối' };
const HUM_MAX = 85, TANK_LOW = 15;

let rangeMin = 15, tab = 'readings', last = null, chartRows = [];
const charts = {};

// ------------------------------ Thông báo nổi ------------------------------
function toast(text, isError) {
  const el = document.createElement('div');
  el.className = 'toast' + (isError ? ' err' : '');
  el.textContent = text;
  $('toasts').appendChild(el);
  setTimeout(() => el.remove(), isError ? 5000 : 2600);
}

// ------------------------------ Cảnh báo bằng giọng nói --------------------
// Dùng bộ đọc có sẵn của trình duyệt (Web Speech API), không cần Internet hay thư viện.
// Trình duyệt chỉ cho phát tiếng sau khi người dùng bấm, nên phải bật bằng nút loa.
const VOICE = { on: false, prev: { tank: false, hot: false, offline: false }, lastRepeat: 0 };
const canSpeak = 'speechSynthesis' in window && 'SpeechSynthesisUtterance' in window;

function viVoice() {
  return speechSynthesis.getVoices().find((v) => /^vi([-_]|$)/i.test(v.lang)) || null;
}

function alarmTone() {                             // tiếng "tít tít" đi kèm, phát được cả khi máy không có giọng tiếng Việt
  try {
    const Ctx = window.AudioContext || window.webkitAudioContext, ctx = new Ctx();
    [0, 0.28].forEach((at) => {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = 880; o.connect(g); g.connect(ctx.destination);
      g.gain.setValueAtTime(0.18, ctx.currentTime + at); g.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + at + 0.2);
      o.start(ctx.currentTime + at); o.stop(ctx.currentTime + at + 0.22);
    });
    setTimeout(() => ctx.close(), 900);
  } catch (e) { /* trình duyệt không hỗ trợ âm thanh: bỏ qua */ }
}

let lastUtterance = null;                          // giữ tham chiếu để Chrome không huỷ câu đang đọc

// vi: câu tiếng Việt ; en: câu tiếng Anh dùng khi máy không có giọng tiếng Việt
function speak(vi, en, withTone) {
  if (!VOICE.on) return;
  if (withTone) alarmTone();
  if (!canSpeak) return;
  const voices = speechSynthesis.getVoices(), v = viVoice();
  const u = new SpeechSynthesisUtterance(v ? vi : en);
  if (v) { u.voice = v; u.lang = v.lang; }
  else {
    // Không ép lang = vi-VN khi không có giọng Việt: nhiều trình duyệt sẽ im lặng hoàn toàn
    const e = voices.find((x) => /^en/i.test(x.lang)); if (e) { u.voice = e; u.lang = e.lang; } else u.lang = 'en-US';
  }
  u.rate = 1; u.volume = 1;
  lastUtterance = u;
  const say = () => { speechSynthesis.resume(); speechSynthesis.speak(u); };
  // Gọi cancel() rồi speak() ngay lập tức làm Chrome nuốt mất câu mới -> chỉ cancel khi đang đọc, và chờ một nhịp
  if (speechSynthesis.speaking || speechSynthesis.pending) { speechSynthesis.cancel(); setTimeout(say, 150); } else say();
}

function voiceInfo() {                             // mô tả giọng đang dùng, để biết vì sao không nghe thấy
  if (!canSpeak) return 'Trình duyệt này không có bộ đọc: chỉ phát tiếng tít.';
  const voices = speechSynthesis.getVoices(), v = viVoice();
  if (v) return `Giọng đọc: ${v.name} (${v.lang})`;
  if (!voices.length) return 'Trình duyệt này chưa có giọng đọc nào: chỉ phát tiếng tít. Hãy mở dashboard bằng Chrome hoặc Edge trên laptop.';
  return 'Máy chưa có giọng tiếng Việt: cảnh báo sẽ đọc bằng tiếng Anh.';
}

// Gọi mỗi lần có trạng thái mới: chỉ đọc khi cảnh báo vừa xuất hiện / vừa hết, và nhắc lại mỗi 20 giây
function voiceCheck(online, d) {
  const p = VOICE.prev;
  // Khi mất kết nối thì chưa biết trạng thái thật: giữ nguyên cảnh báo cũ, không báo "đã hết"
  const now = { tank: d ? !!d.tank_low : p.tank, hot: d ? !!d.too_hot : p.hot, offline: !online };
  if (now.offline && !p.offline) speak('Cảnh báo. Mất kết nối với Arduino.', 'Warning. Connection to Arduino lost.', true);
  if (now.tank && !p.tank) speak('Cảnh báo. Bồn nước sắp cạn. Bơm đã bị khoá.', 'Warning. Water tank is low. Pump is locked.', true);
  else if (!now.tank && p.tank) speak('Bồn nước đã đủ.', 'Water tank is OK.');
  if (now.hot && !p.hot) speak('Cảnh báo. Nhiệt độ nhà kính quá cao.', 'Warning. Greenhouse temperature is too high.', true);
  else if (!now.hot && p.hot) speak('Nhiệt độ đã trở lại bình thường.', 'Temperature is back to normal.');
  if (!now.offline && p.offline) speak('Đã kết nối lại với Arduino.', 'Arduino reconnected.');
  const changed = now.tank !== p.tank || now.hot !== p.hot || now.offline !== p.offline;
  if (changed) VOICE.lastRepeat = Date.now();
  else if ((now.tank || now.hot) && Date.now() - VOICE.lastRepeat > 20000) {
    VOICE.lastRepeat = Date.now();
    speak(now.tank ? 'Bồn nước vẫn đang cạn.' : 'Nhiệt độ vẫn quá cao.', now.tank ? 'Water tank is still low.' : 'Temperature is still too high.', true);
  }
  VOICE.prev = now;
}

function setVoice(on, announce) {
  VOICE.on = on;
  const b = $('voiceBtn');
  b.classList.toggle('on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false');
  try { localStorage.setItem('voice', on ? '1' : '0'); } catch (e) { /* bỏ qua */ }
  if (!announce) return;
  if (on) {
    // Bấm nút là một lần thử loa: tiếng tít + một câu đọc + thông báo cho biết đang dùng giọng nào
    const test = () => { speak('Đã bật cảnh báo bằng giọng nói.', 'Voice alerts are on.', true); toast(voiceInfo()); };
    if (canSpeak && !speechSynthesis.getVoices().length) setTimeout(test, 400); else test();   // chờ trình duyệt nạp danh sách giọng
  }
  else { if (canSpeak) speechSynthesis.cancel(); toast('Đã tắt cảnh báo bằng giọng nói'); }
}

// ------------------------------ Trạng thái hiện tại ------------------------
function badge(id, kind, text) {
  const el = $(id);
  el.className = 'badge ' + kind;
  el.textContent = text;
}

function device(id, on, onText, offText, locked) {
  const row = $(id), sw = row.querySelector('.switch');
  row.classList.toggle('on', !!on);
  row.querySelector('.dev-text span').textContent = on ? onText : offText;
  sw.setAttribute('aria-checked', on ? 'true' : 'false');
  sw.disabled = locked;
}

function slider(id, outId, value) {
  const el = $(id);
  if (el.dataset.busy === '1') return;            // đang kéo: không ghi đè
  el.value = value;
  $(outId).textContent = value + el.dataset.unit;
}

function renderLinks(links, active) {
  const frag = document.createDocumentFragment();
  (links || []).forEach((l) => {
    const row = document.createElement('div');
    row.className = 'linkrow';
    const dot = document.createElement('span');
    dot.className = 'dot ' + (l.alive ? 'alive' : l.connected ? 'idle' : 'down');
    const name = document.createElement('b');
    name.textContent = LINK_NAMES[l.name] || l.name;
    const st = document.createElement('span');
    st.className = 'st';
    st.textContent = l.name === active ? 'Đang dùng' : l.alive ? 'Sẵn sàng' : l.connected ? 'Chờ dữ liệu' : 'Chưa kết nối';
    row.append(dot, name, st);
    frag.appendChild(row);
  });
  $('linkList').replaceChildren(frag);
}

// Arduino thứ hai (servo tưới): một dòng trong thẻ Kết nối + một dòng trong thẻ Thiết bị
function renderWaterNode(w) {
  window.twinWater = w || null;
  const note = $('waterNodeNote');
  note.hidden = !w;
  if (!w) return;
  const row = document.createElement('div');
  row.className = 'linkrow';
  const dot = document.createElement('span');
  dot.className = 'dot ' + (w.wrong_port ? 'down' : w.alive ? 'alive' : w.connected ? 'idle' : 'down');
  const name = document.createElement('b');
  name.textContent = 'Arduino 2: servo tưới';
  const st = document.createElement('span');
  st.className = 'st';
  st.textContent = w.wrong_port ? 'Sai cổng Serial' : w.alive ? (w.on ? 'Đang tưới' : 'Sẵn sàng') : w.connected ? 'Chờ dữ liệu' : 'Chưa kết nối';
  row.append(dot, name, st);
  $('linkList').appendChild(row);
  note.textContent = w.wrong_port ? 'Arduino 2 đang cắm nhầm cổng: đổi SERIAL_PORT và WATER_PORT cho nhau.'
    : !w.alive ? 'Servo tưới (Arduino 2): chưa kết nối.'
    : w.on ? `Servo tưới (Arduino 2): đang quét, góc ${w.angle}°.` : 'Servo tưới (Arduino 2): đang nghỉ.';
}

async function refreshStatus() {
  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch (e) { s = { online: false, link: 'offline', links: [], today: {}, rows: 0 }; }

  $('link').className = 'chip link ' + s.link;
  $('linkText').textContent = LINK_NAMES[s.link] || s.link;
  $('todayPump').textContent = (s.today && s.today.PUMP_ON) || 0;
  $('rows').textContent = s.rows;
  $('alarmOffline').hidden = s.online;
  renderLinks(s.links, s.link);
  renderWaterNode(s.water_node);
  voiceCheck(s.online, s.data);

  const d = s.data;
  window.twinOnline = !!d;                        // mô hình 3D: mờ đi khi mất kết nối
  if (d) window.twinState = d;
  document.querySelectorAll('aside button, aside input').forEach((b) => { b.disabled = !d; });
  if (!d) {
    $('alarmTank').hidden = $('alarmHot').hidden = true;
    return;
  }
  last = d;

  // Chỉ số + nhãn trạng thái (luôn có chữ, không chỉ dựa vào màu)
  $('temp').textContent = d.temp ?? '–';
  $('hum').textContent = d.hum ?? '–';
  $('soil').textContent = d.soil;
  $('light').textContent = d.light;
  $('water').textContent = d.water;
  $('humFill').style.width = (d.hum ?? 0) + '%';
  $('soilFill').style.width = d.soil + '%';
  $('lightFill').style.width = d.light + '%';
  $('waterFill').style.width = d.water + '%';
  $('soilTick').style.left = d.soil_min + '%';
  $('lightTick').style.left = d.light_min + '%';
  $('tmax').textContent = d.tmax;
  $('soilMin').textContent = d.soil_min;
  $('lightMin').textContent = d.light_min;

  if (d.temp == null) badge('bTemp', '', 'Chưa đọc được');
  else if (d.too_hot) badge('bTemp', 'crit', 'Quá nóng');
  else if (d.temp >= d.tmax) badge('bTemp', 'warn', 'Nóng');
  else badge('bTemp', 'good', 'Ổn định');
  if (d.hum == null) badge('bHum', '', 'Chưa đọc được');
  else if (d.hum >= HUM_MAX) badge('bHum', 'warn', 'Quá ẩm');
  else badge('bHum', 'good', 'Ổn định');
  if (d.soil < d.soil_min) badge('bSoil', 'warn', 'Khô');
  else badge('bSoil', 'good', 'Đủ ẩm');
  if (d.light < d.light_min) badge('bLight', 'info', 'Tối');
  else badge('bLight', 'good', 'Đủ sáng');
  if (d.tank_low) badge('bWater', 'crit', 'Sắp cạn');
  else if (d.water < 30) badge('bWater', 'warn', 'Thấp');
  else badge('bWater', 'good', 'Đủ nước');

  $('ts').textContent = 'Cập nhật ' + d.ts;
  $('alarmTank').hidden = !d.tank_low;
  $('alarmHot').hidden = !d.too_hot;

  // Chế độ + thiết bị
  const auto = !!d.mode;
  $('modeChip').className = 'chip ' + (auto ? 'auto' : 'manual');
  $('modeChip').textContent = auto ? 'Tự động' : 'Thủ công';
  document.querySelectorAll('#segMode button').forEach((b) =>
    b.classList.toggle('on', b.dataset.cmd === (auto ? 'MODE=AUTO' : 'MODE=MANUAL')));
  $('modeNote').textContent = auto
    ? 'Arduino tự bật/tắt bơm, cửa gió và đèn theo ngưỡng. Gạt một công tắc thiết bị sẽ tự chuyển sang Thủ công.'
    : 'Bạn đang điều khiển tay. Bấm Tự động để hệ thống tự chạy lại theo ngưỡng. Bơm vẫn bị khoá khi bồn nước sắp cạn.';
  device('devPump', d.pump, 'Đang bơm', d.tank_low ? 'Bị khoá: bồn cạn' : 'Tắt', d.tank_low && !d.pump);
  device('devVent', d.vent, 'Đang mở', 'Đóng', false);
  $('waterBtn').disabled = !!d.tank_low;
  device('devLamp', d.lamp, 'Đang bật', 'Tắt', false);

  slider('inTmax', 'oTmax', d.tmax);
  slider('inSoil', 'oSoil', d.soil_min);
  slider('inLight', 'oLight', d.light_min);
  updateThresholdLines();
}

// ------------------------------ Điều khiển ---------------------------------
async function sendCmd(cmd, label) {
  try {
    const r = await fetch('/api/command', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ cmd }) });
    const j = await r.json();
    if (!j.ok) throw new Error(j.error);
    toast(`${label || cmd} · đã gửi qua ${LINK_NAMES[j.link] || j.link}`);
    setTimeout(refreshStatus, 400);
    setTimeout(refreshHistory, 1200);
  } catch (e) {
    toast('Không gửi được lệnh: ' + e.message, true);
  }
}

document.querySelector('aside').addEventListener('click', (ev) => {
  const b = ev.target.closest('button');
  if (!b || b.disabled) return;
  if (b.classList.contains('switch')) {
    const turnOn = b.getAttribute('aria-checked') !== 'true';
    sendCmd(turnOn ? b.dataset.on : b.dataset.off, b.getAttribute('aria-label') + (turnOn ? ': bật' : ': tắt'));
  } else if (b.dataset.cmd) {
    sendCmd(b.dataset.cmd, b.textContent.trim());
  }
});

document.querySelectorAll('input[type=range]').forEach((el) => {
  const out = el.parentElement.querySelector('output');
  el.addEventListener('input', () => { el.dataset.busy = '1'; out.textContent = el.value + el.dataset.unit; });
  el.addEventListener('change', () => {
    sendCmd(`${el.dataset.key}=${el.value}`, `Ngưỡng ${el.value}${el.dataset.unit}`);
    setTimeout(() => { el.dataset.busy = '0'; }, 1500);
  });
});

// ------------------------------ Biểu đồ ------------------------------------
// Mỗi biểu đồ một đại lượng, một trục. Đường đứt nét = ngưỡng điều khiển.
const CHARTS = [
  { key: 'temp', id: 'cTemp', name: 'Nhiệt độ', unit: ' °C', threshold: (d) => d.tmax },
  { key: 'hum', id: 'cHum', name: 'Độ ẩm không khí', unit: ' %', percent: true, threshold: () => HUM_MAX },
  { key: 'soil', id: 'cSoil', name: 'Độ ẩm đất', unit: ' %', percent: true, threshold: (d) => d.soil_min },
  { key: 'light', id: 'cLight', name: 'Ánh sáng', unit: ' %', percent: true, threshold: (d) => d.light_min },
  { key: 'water', id: 'cWater', name: 'Mực nước', unit: ' %', percent: true, threshold: () => TANK_LOW },
];

function makeChart(cfg) {
  const grid = css('--border'), ink = css('--muted'), line = css('--series'), soft = css('--series-soft'), ref = css('--text2');
  return new Chart($(cfg.id), {
    type: 'line',
    data: { labels: [], datasets: [
      { label: cfg.name, data: [], borderColor: line, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4,
        pointHoverBackgroundColor: line, pointHoverBorderColor: css('--surface'), pointHoverBorderWidth: 2,
        tension: 0.3, spanGaps: true, fill: true,
        backgroundColor: (ctx) => {
          const area = ctx.chart.chartArea;
          if (!area) return soft;
          const g = ctx.chart.ctx.createLinearGradient(0, area.top, 0, area.bottom);
          g.addColorStop(0, soft); g.addColorStop(1, 'rgba(0,0,0,0)');
          return g;
        } },
      { label: 'Ngưỡng', data: [], borderColor: ref, borderWidth: 1.2, borderDash: [5, 4], pointRadius: 0,
        pointHoverRadius: 0, fill: false },
    ] },
    options: {
      responsive: true, maintainAspectRatio: false, animation: false,
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: { display: false },
        tooltip: { backgroundColor: css('--text'), titleColor: css('--surface'), bodyColor: css('--surface'),
          padding: 10, cornerRadius: 8, displayColors: false,
          callbacks: { label: (c) => `${c.dataset.label}: ${c.parsed.y}${cfg.unit}` } },
      },
      scales: {
        x: { grid: { display: false }, border: { color: grid },
          ticks: { color: ink, maxTicksLimit: 6, maxRotation: 0, font: { size: 11 } } },
        y: { grid: { color: grid }, border: { display: false },
          ticks: { color: ink, maxTicksLimit: 5, font: { size: 11 } },
          min: cfg.percent ? 0 : undefined, max: cfg.percent ? 100 : undefined, grace: cfg.percent ? 0 : '15%' },
      },
    },
  });
}

function initCharts() {
  CHARTS.forEach((cfg) => {
    if (charts[cfg.key]) charts[cfg.key].destroy();
    charts[cfg.key] = makeChart(cfg);
  });
  drawCharts();
}

function updateThresholdLines() {
  if (!last) return;
  CHARTS.forEach((cfg) => {
    const c = charts[cfg.key];
    const v = cfg.threshold(last);
    if (c.data.datasets[1].data[0] !== v || c.data.datasets[1].data.length !== c.data.labels.length) {
      c.data.datasets[1].data = c.data.labels.map(() => v);
      c.update();
    }
  });
}

function drawCharts() {
  const labels = chartRows.map((r) => r.label);
  CHARTS.forEach((cfg) => {
    const c = charts[cfg.key];
    c.data.labels = labels;
    c.data.datasets[0].data = chartRows.map((r) => r[cfg.key]);
    c.data.datasets[1].data = last ? labels.map(() => cfg.threshold(last)) : [];
    c.update();
  });
  $('chartEmpty').hidden = chartRows.length > 0;
}

async function refreshCharts() {
  try { chartRows = await (await fetch('/api/chart?minutes=' + rangeMin)).json(); } catch (e) { return; }
  drawCharts();
}

// ------------------------------ Lịch sử ------------------------------------
const EVENT_NAMES = {
  PUMP_ON: ['info', 'Bơm bật'], PUMP_OFF: ['', 'Bơm tắt'], VENT_OPEN: ['info', 'Mở cửa gió'], VENT_CLOSE: ['', 'Đóng cửa gió'],
  LAMP_ON: ['info', 'Đèn bật'], LAMP_OFF: ['', 'Đèn tắt'], TANK_LOW: ['crit', 'Bồn cạn'], TANK_OK: ['good', 'Bồn đủ nước'],
  BOOT: ['', 'Arduino khởi động'], LCD: ['', 'Địa chỉ LCD'], WATER_START: ['info', 'Bắt đầu tưới'], WSERVO_ON: ['info', 'Servo tưới chạy'], WSERVO_OFF: ['', 'Servo tưới nghỉ'], WFAILSAFE: ['warn', 'Servo tưới tự dừng'], WATER_DONE: ['good', 'Tưới xong'], START: ['', 'Gateway khởi động'], LINK: ['warn', 'Đổi kênh'],
};
const pill = (kind, text) => ({ badge: kind, text });
const onOff = (v, a, b) => pill(v ? 'good' : '', v ? a : b);
const TABLES = {
  readings: [['ts', 'Thời gian'], ['temp', '°C', null, 1], ['hum', 'Ẩm KK %', null, 1], ['soil', 'Ẩm đất %', null, 1],
    ['light', 'Sáng %', null, 1], ['water', 'Nước %', null, 1],
    ['pump', 'Bơm', (v) => onOff(v, 'Bật', 'Tắt')], ['vent', 'Cửa gió', (v) => onOff(v, 'Mở', 'Đóng')],
    ['lamp', 'Đèn', (v) => onOff(v, 'Bật', 'Tắt')], ['mode', 'Chế độ', (v) => (v ? 'Tự động' : 'Thủ công')],
    ['link', 'Kênh', (v) => LINK_NAMES[v] || v]],
  events: [['ts', 'Thời gian'], ['type', 'Sự kiện', (v) => pill(...(EVENT_NAMES[v] || ['', v]))], ['detail', 'Chi tiết']],
  commands: [['ts', 'Thời gian'], ['cmd', 'Lệnh'], ['link', 'Kênh gửi', (v) => LINK_NAMES[v] || v],
    ['status', 'Phản hồi', (v) => pill(v === 'OK' ? 'good' : v === 'ERR' ? 'crit' : '', v === 'OK' ? 'Thành công' : v === 'ERR' ? 'Bị từ chối' : 'Đang chờ')]],
};

async function refreshHistory() {
  let h;
  try { h = await (await fetch('/api/history?limit=40')).json(); } catch (e) { return; }
  const cols = TABLES[tab];
  const head = document.createElement('tr');
  cols.forEach((c) => {
    const th = document.createElement('th');
    th.textContent = c[1];
    if (c[3]) th.className = 'num';
    head.appendChild(th);
  });
  const body = document.createDocumentFragment();
  h[tab].forEach((r) => {
    const tr = document.createElement('tr');
    cols.forEach((c) => {
      const td = document.createElement('td');
      const v = c[2] ? c[2](r[c[0]]) : r[c[0]];
      if (v && typeof v === 'object') {
        const b = document.createElement('span');
        b.className = 'badge ' + v.badge;
        b.textContent = v.text;
        td.appendChild(b);
      } else {
        td.textContent = v ?? '–';
      }
      if (c[3]) td.className = 'num';
      if (c[0] === 'ts') td.className = 'dim';
      tr.appendChild(td);
    });
    body.appendChild(tr);
  });
  if (!h[tab].length) {
    const tr = document.createElement('tr'), td = document.createElement('td');
    td.colSpan = cols.length; td.className = 'dim'; td.textContent = 'Chưa có dữ liệu.';
    tr.appendChild(td); body.appendChild(tr);
  }
  $('tbl').tHead.replaceChildren(head);
  $('tbl').tBodies[0].replaceChildren(body);
}

// ------------------------------ Khởi động ----------------------------------
function bindSeg(id, attr, fn) {
  $(id).addEventListener('click', (ev) => {
    const b = ev.target.closest('button');
    if (!b) return;
    $(id).querySelectorAll('button').forEach((x) => x.classList.toggle('on', x === b));
    fn(b.dataset[attr]);
  });
}
bindSeg('segRange', 'min', (v) => { rangeMin = +v; refreshCharts(); });
bindSeg('segTab', 'tab', (v) => { tab = v; refreshHistory(); });

$('themeBtn').addEventListener('click', () => {
  const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem('theme', next); } catch (e) { /* bỏ qua nếu trình duyệt chặn */ }
  initCharts();                                   // vẽ lại biểu đồ theo màu mới
  if (window.twinApplyTheme) window.twinApplyTheme();
});

$('voiceBtn').addEventListener('click', () => setVoice(!VOICE.on, true));
if (canSpeak) speechSynthesis.getVoices();        // nạp sẵn danh sách giọng (một số trình duyệt nạp trễ)
// Lần trước đã bật thì bật lại, nhưng trình duyệt chỉ phát tiếng sau cú bấm đầu tiên trên trang
try { if (localStorage.getItem('voice') === '1') setVoice(true, false); } catch (e) { /* bỏ qua */ }

initCharts();
refreshStatus().then(refreshCharts);
refreshHistory();
setInterval(refreshStatus, 1000);
setInterval(() => { refreshCharts(); refreshHistory(); }, 5000);
