// Mô hình 3D trực tiếp của nhà kính ("digital twin"): một nhà kính trồng rau thực tế,
// phản ánh trạng thái thật lấy từ /api/status (app.js gán window.twinState / twinWater / twinOnline).
// Cần static/three.min.js. Nếu trình duyệt không có WebGL thì khung 3D hiện thông báo thay thế.
(function () {
  'use strict';
  const host = document.getElementById('twin');
  if (!host) return;
  try {
    THREE.ColorManagement.legacyMode = false;
    const W = () => host.clientWidth, H = () => host.clientHeight;
    const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setSize(W(), H());
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    host.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0xcfe9ff);
    const camera = new THREE.PerspectiveCamera(38, W() / H(), 1, 1800);
    const target = new THREE.Vector3(0, 8, 0);
    let radius = 120, zoom = 1, theta = 0.62, phi = 1.3;
    function placeCamera() {
      radius = 82 * Math.max(1, 1.6 / camera.aspect) * zoom;        // khung hẹp thì lùi máy quay
      camera.position.set(target.x + radius * Math.sin(phi) * Math.sin(theta), target.y + radius * Math.cos(phi), target.z + radius * Math.sin(phi) * Math.cos(theta));
      camera.lookAt(target);
    }

    const hemi = new THREE.HemisphereLight(0xffffff, 0x8fa58f, 0.9); scene.add(hemi);
    const sun = new THREE.DirectionalLight(0xfff4e0, 1.6);
    sun.position.set(-50, 90, 60); sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048); sun.shadow.bias = -0.0005;
    Object.assign(sun.shadow.camera, { left: -80, right: 80, top: 80, bottom: -80, near: 1, far: 300 });
    scene.add(sun);

    // ---------- tiện ích dựng hình ----------
    const mat = (color, o = {}) => new THREE.MeshStandardMaterial(Object.assign({ color, roughness: 0.75, metalness: 0.05 }, o));
    function add(mesh, shadow = true) { mesh.castShadow = shadow; mesh.receiveShadow = true; scene.add(mesh); return mesh; }
    function box(w, h, d, color, x, y, z, o) {
      const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), color.isMaterial ? color : mat(color, o));
      m.position.set(x, y + h / 2, z); return add(m);
    }
    function cyl(r, h, color, x, y, z, o, r2) {
      const m = new THREE.Mesh(new THREE.CylinderGeometry(r2 === undefined ? r : r2, r, h, 36), color.isMaterial ? color : mat(color, o));
      m.position.set(x, y + h / 2, z); return add(m);
    }
    function tube(points, r, color, o) {
      const curve = new THREE.CatmullRomCurve3(points.map((p) => new THREE.Vector3(...p)));
      return add(new THREE.Mesh(new THREE.TubeGeometry(curve, 48, r, 10), mat(color, Object.assign({ roughness: 0.45, metalness: 0.2 }, o))));
    }
    function canvasTexture(w, h) {
      const c = document.createElement('canvas'); c.width = w; c.height = h;
      const t = new THREE.CanvasTexture(c); t.encoding = THREE.sRGBEncoding; return t;
    }

    // ---------- nhãn có số liệu trực tiếp ----------
    const labels = [];
    function sizeLabels() {                        // giữ cỡ chữ cố định theo pixel, không phụ thuộc chiều cao khung
      const k = 0.00050 * 440 / Math.max(H(), 1);
      labels.forEach((l) => l.sprite.scale.set(l.w * k, l.h * k, 1));
    }
    function makeLabel(at, to) {
      const c = document.createElement('canvas'), tex = new THREE.CanvasTexture(c); tex.encoding = THREE.sRGBEncoding;
      const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, depthTest: false, sizeAttenuation: false }));
      sprite.position.set(...at); sprite.renderOrder = 10; scene.add(sprite);
      const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...at), new THREE.Vector3(...to)]),
        new THREE.LineBasicMaterial({ color: 0xffffff, depthTest: false, transparent: true, opacity: 0.9 }));
      line.renderOrder = 9; scene.add(line);
      const dot = new THREE.Mesh(new THREE.SphereGeometry(0.45, 12, 10), new THREE.MeshBasicMaterial({ color: 0xffffff, depthTest: false }));
      dot.position.set(...to); dot.renderOrder = 9; scene.add(dot);
      const l = { sprite, w: 10, h: 10, text: null, tone: null,
        set(text, tone) {                          // tone: 'ok' | 'on' | 'warn' -> màu chấm trạng thái trong nhãn
          if (text === l.text && tone === l.tone) return;
          l.text = text; l.tone = tone;
          const pad = 20, fs = 38, g = c.getContext('2d');
          g.font = `600 ${fs}px Arial`;
          c.width = Math.ceil(g.measureText(text).width) + pad * 2 + 34; c.height = fs + 36;
          g.font = `600 ${fs}px Arial`;
          const r = 16, w = c.width - 2, h = c.height - 2;
          g.fillStyle = 'rgba(16,28,22,0.86)';
          g.beginPath(); g.moveTo(r + 1, 1); g.arcTo(w, 1, w, h, r); g.arcTo(w, h, 1, h, r); g.arcTo(1, h, 1, 1, r); g.arcTo(1, 1, w, 1, r); g.closePath(); g.fill();
          g.fillStyle = tone === 'warn' ? '#ff6b5e' : tone === 'on' ? '#ffd43b' : '#51cf66';
          g.beginPath(); g.arc(pad + 9, c.height / 2, 9, 0, 7); g.fill();
          g.fillStyle = '#ffffff'; g.textBaseline = 'middle'; g.fillText(text, pad + 30, c.height / 2 + 2);
          tex.dispose(); tex.needsUpdate = true;            // đổi cỡ canvas thì phải cấp lại texture
          l.w = c.width; l.h = c.height; sizeLabels();
        } };
      labels.push(l); return l;
    }

    // ---------- BẦU TRỜI: ngày / đêm theo cảm biến ánh sáng ----------
    function skyTexture(top, bottom) {
      const t = canvasTexture(64, 512), g = t.image.getContext('2d'), gr = g.createLinearGradient(0, 0, 0, 512);
      gr.addColorStop(0, top); gr.addColorStop(0.6, bottom); gr.addColorStop(1, bottom);
      g.fillStyle = gr; g.fillRect(0, 0, 64, 512); return t;
    }
    const skyDay = new THREE.Mesh(new THREE.SphereGeometry(800, 40, 24), new THREE.MeshBasicMaterial({ map: skyTexture('#3684dc', '#d6ecff'), side: THREE.BackSide, depthWrite: false }));
    const skyNight = new THREE.Mesh(new THREE.SphereGeometry(790, 40, 24), new THREE.MeshBasicMaterial({ map: skyTexture('#040816', '#1b2a4c'), side: THREE.BackSide, transparent: true, opacity: 0, depthWrite: false }));
    skyDay.renderOrder = -3; skyNight.renderOrder = -2; scene.add(skyDay, skyNight);
    const starPos = [];
    for (let i = 0; i < 800; i++) {
      const az = Math.random() * Math.PI * 2, el = (1.5 + Math.random() * 60) * Math.PI / 180, r = 740;
      starPos.push(Math.sin(az) * r * Math.cos(el), r * Math.sin(el), Math.cos(az) * r * Math.cos(el));
    }
    const starGeo = new THREE.BufferGeometry(); starGeo.setAttribute('position', new THREE.Float32BufferAttribute(starPos, 3));
    const stars = new THREE.Points(starGeo, new THREE.PointsMaterial({ color: 0xffffff, size: 2.6, sizeAttenuation: false, transparent: true, opacity: 0, depthWrite: false }));
    stars.frustumCulled = false; stars.renderOrder = -1; scene.add(stars);
    function skyPos(off, el, dist) {                // vị trí trên trời theo hướng nhìn mặc định: lệch ngang, độ cao (độ)
      const a = 0.62 + Math.PI + off * Math.PI / 180, e = el * Math.PI / 180, r = dist || 640;
      return [Math.sin(a) * r * Math.cos(e), r * Math.sin(e), Math.cos(a) * r * Math.cos(e)];
    }
    const sunBall = new THREE.Mesh(new THREE.SphereGeometry(18, 24, 18), new THREE.MeshBasicMaterial({ color: 0xffe066, transparent: true }));
    sunBall.position.set(...skyPos(-20, 10)); scene.add(sunBall);
    const moon = new THREE.Mesh(new THREE.SphereGeometry(13, 24, 18), new THREE.MeshBasicMaterial({ color: 0xf1f3f9, transparent: true, opacity: 0 }));
    moon.position.set(...skyPos(16, 11)); scene.add(moon);
    const cloudMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.92, depthWrite: false }), clouds = [];
    [[-38, 8, 0.6], [-6, 12, 0.5], [24, 7.5, 0.65], [44, 11, 0.5], [-60, 10, 0.55], [70, 8, 0.6], [-90, 9, 0.6], [110, 10, 0.5]].forEach(([off, el, k]) => {
      const [cx, cy, cz] = skyPos(off, el);
      [[0, 0, 0, 34], [30, 6, 4, 26], [-32, 2, -4, 24], [8, 14, 0, 22]].forEach(([dx, dy, dz, r]) => {
        const c = new THREE.Mesh(new THREE.SphereGeometry(r * k, 16, 12), cloudMat); c.scale.y = 0.55; c.position.set(cx + dx * k, cy + dy * k, cz + dz * k); clouds.push(c); scene.add(c);
      });
    });
    let daylight = 1, lastT = 0;                   // 1 = ban ngày, 0 = ban đêm

    // ---------- KHUÔN VIÊN ----------
    const ground = new THREE.Mesh(new THREE.CircleGeometry(700, 72), mat(0x4f8f3a, { roughness: 1 }));
    ground.rotation.x = -Math.PI / 2; ground.receiveShadow = true; scene.add(ground);
    const GX = 2, GZ = -2, GW = 44, GD = 24, WH = 9, RISE = 6, BASE = 0.6;
    const x0 = GX - GW / 2, x1 = GX + GW / 2, z0 = GZ - GD / 2, z1 = GZ + GD / 2, ridgeY = BASE + WH + RISE;
    box(GW + 4, BASE, GD + 4, 0xb9bcb6, GX, 0, GZ, { roughness: 0.95 });                 // nền bê tông
    box(16, 0.12, 5, 0xc9c2ad, x1 + 10, 0, GZ, { roughness: 1 });                        // lối đi sỏi tới cửa
    box(5, 0.12, 22, 0xc9c2ad, x1 + 15.5, 0, GZ + 13, { roughness: 1 });
    function tree(x, z, s) {
      cyl(0.5 * s, 4 * s, 0x6b4a2b, x, 0, z);
      [[4, 3.6, 5], [6.6, 2.9, 4.4], [8.8, 2.1, 3.6]].forEach(([y, r, h]) => {
        const m = new THREE.Mesh(new THREE.ConeGeometry(r * s, h * s, 14), mat(0x2f6b34, { roughness: 0.95 })); m.position.set(x, (y + h / 2 - 1) * s, z); add(m);
      });
    }
    [[-44, -34, 1.5], [-28, -40, 1.9], [-8, -38, 1.4], [14, -42, 1.8], [36, -36, 1.5], [52, -22, 1.7], [-52, -12, 1.6], [58, 6, 1.3]].forEach((t) => tree(...t));
    [[-34, 20, 2.2], [-30, 24, 1.6], [44, 24, 2], [48, 20, 1.5], [-46, 6, 1.8]].forEach(([x, z, r]) => {
      const b = new THREE.Mesh(new THREE.SphereGeometry(r, 16, 12), mat(0x3d7d3a, { roughness: 1 })); b.scale.y = 0.75; b.position.set(x, r * 0.6, z); add(b);
    });

    // ---------- NHÀ KÍNH ----------
    const glass = new THREE.MeshStandardMaterial({ color: 0xd4efff, transparent: true, opacity: 0.2, roughness: 0.08, metalness: 0.1, side: THREE.DoubleSide, depthWrite: false });
    const alu = mat(0xe9edee, { roughness: 0.35, metalness: 0.55 });
    const F = 0.45, SL = Math.hypot(GD / 2, RISE), ANG = Math.atan2(RISE, GD / 2), BAY = GW / 4;
    box(GW, 1.1, 0.5, 0x8d9494, GX, BASE, z0); box(GW, 1.1, 0.5, 0x8d9494, GX, BASE, z1);          // chân tường
    box(0.5, 1.1, GD, 0x8d9494, x0, BASE, GZ); box(0.5, 1.1, GD, 0x8d9494, x1, BASE, GZ);
    for (let i = 0; i <= 4; i++) {                                                           // cột + vì kèo mỗi khoang
      const x = x0 + i * BAY;
      box(F, WH, F, alu, x, BASE, z0); box(F, WH, F, alu, x, BASE, z1);
      [-1, 1].forEach((sgn) => {
        const g = new THREE.Group(); g.position.set(x, ridgeY, GZ);
        const r = new THREE.Mesh(new THREE.BoxGeometry(F, F, SL), alu); r.position.z = sgn * SL / 2; r.castShadow = true; g.add(r);
        g.rotation.x = sgn * ANG; scene.add(g);
      });
      if (i > 0 && i < 4) box(F * 0.7, F * 0.7, GD, alu, x, BASE + WH - F, GZ);              // thanh giằng ngang
    }
    [z0, z1].forEach((z) => { box(GW, F, F, alu, GX, BASE + WH - F, z); box(GW, F * 0.7, F * 0.7, alu, GX, BASE + WH * 0.5, z); });
    box(GW, F, F, alu, GX, ridgeY - F, GZ);                                                  // xà nóc
    function pane(w, h, x, y, z, ry) { const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h), glass); m.position.set(x, y, z); m.rotation.y = ry; scene.add(m); }
    pane(GW, WH, GX, BASE + WH / 2, z0, 0); pane(GW, WH, GX, BASE + WH / 2, z1, 0);
    pane(GD, WH, x0, BASE + WH / 2, GZ, Math.PI / 2); pane(GD, WH, x1, BASE + WH / 2, GZ, Math.PI / 2);
    [x0, x1].forEach((x) => {                                                                // hai đầu hồi
      const sh = new THREE.Shape(); sh.moveTo(-GD / 2, 0); sh.lineTo(GD / 2, 0); sh.lineTo(0, RISE); sh.closePath();
      const m = new THREE.Mesh(new THREE.ShapeGeometry(sh), glass); m.rotation.y = Math.PI / 2; m.position.set(x, BASE + WH, GZ); scene.add(m);
      box(F, RISE - 0.3, F, alu, x, BASE + WH, GZ);
    });
    // cửa ra vào ở đầu hồi phải
    box(0.3, 7.6, 0.35, alu, x1 + 0.1, BASE, GZ - 2.6); box(0.3, 7.6, 0.35, alu, x1 + 0.1, BASE, GZ + 2.6); box(0.3, 0.35, 5.5, alu, x1 + 0.1, BASE + 7.6, GZ);
    box(0.12, 1.2, 0.2, 0x495057, x1 + 0.3, BASE + 3.4, GZ + 2.1);
    function roofPanel(xc, w, sign, material) {     // tấm mái xoay quanh xà nóc
      const g = new THREE.Group(); g.position.set(xc, ridgeY, GZ);
      const m = new THREE.Mesh(new THREE.BoxGeometry(w - 0.3, 0.18, SL), material); m.position.z = sign * SL / 2; g.add(m);
      const e = new THREE.Mesh(new THREE.BoxGeometry(w, F, F), alu); e.position.z = sign * SL; g.add(e);
      [-w / 2 + 0.2, w / 2 - 0.2].forEach((dx) => { const r = new THREE.Mesh(new THREE.BoxGeometry(F * 0.7, F * 0.7, SL), alu); r.position.set(dx, 0, sign * SL / 2); g.add(r); });
      g.rotation.x = sign * ANG; scene.add(g); return g;
    }
    roofPanel(GX, GW, -1, glass);                                                            // mái sau
    const ventGlass = glass.clone(); ventGlass.opacity = 0.3;
    const vents = [];
    for (let i = 0; i < 4; i++) {
      const xc = x0 + BAY * (i + 0.5);
      if (i % 2 === 1) {                                                                     // khoang 2 và 4: cửa thông gió
        vents.push(roofPanel(xc, BAY, 1, ventGlass));
        box(1.6, 1.1, 0.9, 0x343a40, xc, ridgeY - 1.8, GZ + 0.9);                            // mô-tơ mở cửa
      } else roofPanel(xc, BAY, 1, glass);
    }

    // ---------- LUỐNG TRỒNG + CÂY ----------
    const soilMat = mat(0x6b4a2b, { roughness: 1 }), woodMat = mat(0xa9773f, { roughness: 0.9 });
    const stemGeo = new THREE.CylinderGeometry(0.1, 0.13, 3.4, 8), leafGeo = new THREE.SphereGeometry(1, 12, 10), fruitGeo = new THREE.SphereGeometry(0.24, 10, 8);
    const stemMat = mat(0x3a7d34), leafMat = mat(0x3f9b46, { roughness: 0.95 }), leafMat2 = mat(0x57b25c, { roughness: 0.95 }), fruitMat = mat(0xd9342b, { roughness: 0.5 });
    const plantMeshes = [], BEDS = [GZ - 7.2, GZ, GZ + 7.2], BED_Y = BASE + 1.3;
    BEDS.forEach((bz, bi) => {
      plantMeshes.push(box(GW - 8, 1.3, 4.4, woodMat, GX - 1, BASE, bz));
      plantMeshes.push(box(GW - 8.6, 0.3, 3.8, soilMat, GX - 1, BASE + 1.1, bz));
      for (let i = 0; i < 9; i++) {
        const px = x0 + 5.2 + i * 3.9, pz = bz + (i % 2 ? 0.5 : -0.5), s = 0.85 + ((i * 7 + bi * 3) % 5) * 0.07;
        const st = new THREE.Mesh(stemGeo, stemMat); st.position.set(px, BED_Y + 1.7 * s, pz); st.scale.setScalar(s); plantMeshes.push(add(st));
        [[0, 2.5, 0, 0.95], [0.6, 1.6, 0.3, 0.75], [-0.6, 1.9, -0.25, 0.8], [0.1, 3.3, -0.1, 0.65]].forEach(([dx, dy, dz, r], k) => {
          const lf = new THREE.Mesh(leafGeo, k % 2 ? leafMat2 : leafMat); lf.position.set(px + dx * s, BED_Y + dy * s, pz + dz * s); lf.scale.set(r * s, r * s * 0.85, r * s); plantMeshes.push(add(lf));
        });
        [[0.75, 1.5, 0.55], [-0.6, 2.2, 0.6], [0.2, 2.9, 0.7]].forEach(([dx, dy, dz]) => {
          if ((i + bi) % 3 === 0 && dy > 2.5) return;
          const fr = new THREE.Mesh(fruitGeo, fruitMat); fr.position.set(px + dx * s, BED_Y + dy * s, pz + dz * s); add(fr);
        });
      }
    });

    // ---------- ĐÈN TRỒNG CÂY ----------
    const lampBars = [], lampLights = [];
    const lampMat = mat(0xfff3bf, { emissive: 0xffc94a, emissiveIntensity: 0 });
    [x0 + 8, GX, x1 - 8].forEach((lx) => {
      [-2.2, 2.2].forEach((dx) => cyl(0.05, 2.6, 0x495057, lx + dx, ridgeY - 3.2, GZ));
      box(9, 0.3, 1.5, 0x343a40, lx, ridgeY - 3.5, GZ);
      const bar = new THREE.Mesh(new THREE.BoxGeometry(8.4, 0.16, 1.1), lampMat); bar.position.set(lx, ridgeY - 3.6, GZ); scene.add(bar); lampBars.push(bar);
      const pl = new THREE.PointLight(0xffd27a, 0, 40, 1.6); pl.position.set(lx, ridgeY - 4.4, GZ); scene.add(pl); lampLights.push(pl);
    });

    // ---------- TƯỚI: giàn phun chạy dọc nhà kính ----------
    const RAIL_Y = BASE + WH - 0.9;
    [z0 + 1.4, z1 - 1.4].forEach((rz) => tube([[x0 + 1.5, RAIL_Y, rz], [GX, RAIL_Y, rz], [x1 - 1.5, RAIL_Y, rz]], 0.16, 0xadb5bd));
    const boom = new THREE.Group(); boom.position.set(x0 + 5, RAIL_Y - 0.3, GZ); scene.add(boom);
    const boomPipeMat = mat(0x9aa5ad, { roughness: 0.4, metalness: 0.4 });
    const boomPipe = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.2, GD - 2.8, 16), boomPipeMat); boomPipe.rotation.x = Math.PI / 2; boom.add(boomPipe);
    const NOZ_Z = [-8.4, -6, -1.2, 1.2, 6, 8.4];
    NOZ_Z.forEach((nz) => { const n = new THREE.Mesh(new THREE.CylinderGeometry(0.28, 0.12, 0.6, 12), mat(0x343a40)); n.position.set(0, -0.45, nz); boom.add(n); });
    [-(GD / 2 - 1.4), GD / 2 - 1.4].forEach((cz) => { const c = new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.8, 0.9), mat(0x2f6fd0)); c.position.set(0, 0.25, cz); boom.add(c); });
    const SPRAY_N = 720, sprayPos = new Float32Array(SPRAY_N * 3), sprayP = [];
    for (let i = 0; i < SPRAY_N; i++) sprayP.push({ nz: NOZ_Z[i % NOZ_Z.length], ang: Math.random() * Math.PI * 2, vh: 0.5 + Math.random() * 2.6, vy: Math.random() * 1.2, phase: Math.random(), life: 0.7 + Math.random() * 0.25, x: x0 + 5 });
    const sprayGeo = new THREE.BufferGeometry(); sprayGeo.setAttribute('position', new THREE.BufferAttribute(sprayPos, 3));
    const spray = new THREE.Points(sprayGeo, new THREE.PointsMaterial({ color: 0x4dabf7, size: 0.75, transparent: true, opacity: 0.9, depthWrite: false }));
    spray.visible = false; spray.frustumCulled = false; spray.renderOrder = 5; scene.add(spray);
    let sprayLevel = 0, boomX = x0 + 5;

    // ---------- BỒN NƯỚC + TRẠM BƠM (ngoài trời, bên trái) ----------
    const TX = x0 - 10, TZ = GZ + 3, TH = 11, TR = 4.4;
    cyl(TR + 0.5, 0.6, 0x8d9494, TX, 0, TZ);
    const tankShell = cyl(TR, TH, new THREE.MeshStandardMaterial({ color: 0xe6f3fa, transparent: true, opacity: 0.3, roughness: 0.15, depthWrite: false }), TX, 0.6, TZ);
    const waterMesh = cyl(TR - 0.25, 1, new THREE.MeshStandardMaterial({ color: 0x2f8fe6, transparent: true, opacity: 0.72, roughness: 0.1 }), TX, 0.7, TZ);
    cyl(TR + 0.15, 0.5, 0x495057, TX, 0.6 + TH, TZ); cyl(TR + 0.1, 0.25, 0x495057, TX, 0.6 + TH * 0.5, TZ);
    for (let i = 0; i < 6; i++) box(0.12, 0.08, 0.5, 0xffffff, TX + TR * Math.sin(0.6), 1.2 + i * 2, TZ + TR * Math.cos(0.6));          // vạch mức
    const pumpBox = box(3.4, 2.4, 2.6, 0x2b5fa8, x0 - 4.6, BASE - 0.1, z1 - 2.5, { roughness: 0.5, metalness: 0.3 });
    cyl(0.9, 1.5, 0x495057, x0 - 4.6, BASE + 2.2, z1 - 2.5);
    const pumpLed = new THREE.Mesh(new THREE.SphereGeometry(0.34, 12, 10), mat(0x51cf66, { emissive: 0x2f9e44, emissiveIntensity: 0 })); pumpLed.position.set(x0 - 3.6, BASE + 2, z1 - 1.1); scene.add(pumpLed);
    const pipeA = tube([[TX + TR - 0.3, 1.4, TZ + 1.2], [x0 - 7.5, 1.2, z1 - 3], [x0 - 6.2, 1.4, z1 - 2.5]], 0.3, 0x9aa5ad);
    const pipeB = tube([[x0 - 3, 1.6, z1 - 2.5], [x0 - 0.9, 1.8, z1 - 2], [x0 - 0.7, RAIL_Y - 0.6, z1 - 1.6], [x0 + 1.5, RAIL_Y, z1 - 1.4]], 0.26, 0x9aa5ad);

    // ---------- TỦ ĐIỀU KHIỂN + ĐÈN CẢNH BÁO (trên tường trước) ----------
    const CABX = x0 + 5.5, CABZ = z1 + 0.75;
    box(0.4, 6.4, 0.4, alu, CABX, BASE, CABZ - 0.2);
    box(4.6, 4, 1.1, 0xd9dde0, CABX, BASE + 3, CABZ, { roughness: 0.5, metalness: 0.3 });
    box(4.9, 0.25, 1.5, 0x868e96, CABX, BASE + 7, CABZ);
    let lcdLines = ['', ''];
    const lcdTex = canvasTexture(640, 256);
    function drawLcd() {
      const g = lcdTex.image.getContext('2d');
      g.fillStyle = '#1f3fb8'; g.fillRect(0, 0, 640, 256); g.fillStyle = '#eaf1ff'; g.font = 'bold 60px monospace';
      g.fillText(lcdLines[0], 22, 100); g.fillText(lcdLines[1], 22, 190); lcdTex.needsUpdate = true;
    }
    const lcd = new THREE.Mesh(new THREE.PlaneGeometry(3.7, 1.5), new THREE.MeshBasicMaterial({ map: lcdTex })); lcd.position.set(CABX, BASE + 5.7, CABZ + 0.56); scene.add(lcd);
    [[-1.2, 0x51cf66], [0, 0xffd43b], [1.2, 0xff6b5e]].forEach(([dx, c]) => { const b = new THREE.Mesh(new THREE.CylinderGeometry(0.3, 0.3, 0.2, 16), mat(c)); b.rotation.x = Math.PI / 2; b.position.set(CABX + dx, BASE + 4, CABZ + 0.6); scene.add(b); });
    cyl(0.45, 0.4, 0x343a40, CABX, BASE + 7.25, CABZ);
    const beacon = cyl(0.55, 1, mat(0xff8c1a, { emissive: 0xff5a00, emissiveIntensity: 0, transparent: true, opacity: 0.92 }), CABX, BASE + 7.65, CABZ, {}, 0.45);
    const beaconLight = new THREE.PointLight(0xff6a1a, 0, 34, 1.6); beaconLight.position.set(CABX, BASE + 8.6, CABZ + 0.8); scene.add(beaconLight);

    // ---------- CẢM BIẾN ----------
    const SX = GX + 9, SZ = GZ + 3.6;                                                        // trạm nhiệt độ / độ ẩm (hộp lá chớp)
    cyl(0.14, 5.4, alu, SX, BASE, SZ); box(1.7, 2, 1.7, 0xf8f9fa, SX, BASE + 5.2, SZ);
    for (let i = 0; i < 4; i++) box(1.95, 0.12, 1.95, 0xdee2e6, SX, BASE + 5.4 + i * 0.5, SZ);
    cyl(0.12, 3.4, alu, x1 - 0.3, ridgeY, GZ);                                               // cảm biến ánh sáng trên nóc
    box(1.3, 0.14, 1.3, 0x343a40, x1 - 0.3, ridgeY + 3.4, GZ);
    const luxDome = new THREE.Mesh(new THREE.SphereGeometry(0.5, 16, 12, 0, Math.PI * 2, 0, Math.PI / 2), mat(0xf1f3f5, { roughness: 0.2 })); luxDome.position.set(x1 - 0.3, ridgeY + 3.54, GZ); scene.add(luxDome);
    const PRX = GX - 6, PRZ = BEDS[2] + 1.2;                                                 // đầu dò độ ẩm đất
    box(0.5, 1.5, 0.16, 0xf0b429, PRX, BED_Y - 0.3, PRZ); box(0.8, 0.5, 0.3, 0x343a40, PRX, BED_Y + 1.2, PRZ);

    // ---------- NHÃN ----------
    const L = {
      climate: makeLabel([SX + 9, 23.5, SZ - 6], [SX, BASE + 7.2, SZ]),
      light: makeLabel([x1 + 1, 26.5, GZ - 6], [x1 - 0.3, ridgeY + 3.9, GZ]),
      soil: makeLabel([GX + 4, 2.2, z1 + 12], [PRX, BED_Y + 1.6, PRZ]),
      tank: makeLabel([TX - 6, 19, TZ - 2], [TX, 0.6 + TH + 0.6, TZ]),
      vent: makeLabel([x0 + BAY * 1.5 - 4, 28.5, GZ + 4], [x0 + BAY * 1.5, ridgeY - 2.4, GZ + 5]),
      lamp: makeLabel([GX - 3, 25, GZ - 10], [GX, ridgeY - 3.6, GZ]),
      water: makeLabel([x1 - 4, 20.5, z1 + 10], [x1 - 9, RAIL_Y - 0.4, z1 - 1.4]),
      panel: makeLabel([CABX - 9, 13, CABZ + 9], [CABX, BASE + 6.6, CABZ + 0.6]),
    };
    L.climate.set('Nhiệt độ –  ·  Độ ẩm –', 'ok'); L.light.set('Ánh sáng –', 'ok'); L.soil.set('Độ ẩm đất –', 'ok'); L.tank.set('Bồn nước –', 'ok');
    L.vent.set('Cửa thông gió', 'ok'); L.lamp.set('Đèn trồng cây', 'ok'); L.water.set('Giàn tưới', 'ok'); L.panel.set('Tủ điều khiển', 'ok');

    // ---------- Đồng bộ với dữ liệu thật từ /api/status ----------
    const pad = (n, w) => String(n == null ? '--' : n).padStart(w, ' ');
    const fmt = (n, u) => (n == null ? '–' : n + u);
    let ventAngle = 0, lastLcd = '';
    function animate(t) {
      const d = window.twinState;
      host.classList.toggle('stale', !d || !window.twinOnline);
      if (!d) return;
      const alarm = d.tank_low || d.too_hot;
      const dt = Math.min(0.2, Math.max(0, (t - lastT) / 1000)); lastT = t;

      // Bầu trời theo cảm biến ánh sáng: tối dần thì sao và trăng hiện ra
      const dayTarget = Math.min(1, Math.max(0, (d.light - 12) / 45));
      daylight += (dayTarget - daylight) * Math.min(1, dt * 2.2);
      skyNight.material.opacity = 1 - daylight; moon.material.opacity = 1 - daylight; moon.visible = daylight < 0.98;
      stars.material.opacity = 1 - daylight; stars.visible = daylight < 0.98;
      sunBall.material.opacity = daylight; sunBall.visible = daylight > 0.02;
      cloudMat.opacity = 0.92 * daylight; clouds.forEach((c) => { c.visible = daylight > 0.02; });
      hemi.intensity = 0.2 + 0.7 * daylight; sun.intensity = 0.1 + 1.5 * daylight;
      ground.material.color.setRGB(0.05 + 0.1 * daylight, 0.1 + 0.2 * daylight, 0.05 + 0.06 * daylight);

      // Cửa thông gió
      ventAngle += ((d.vent ? 0.72 : 0) - ventAngle) * Math.min(1, dt * 4);
      vents.forEach((v) => { v.rotation.x = ANG - ventAngle; });

      // Đèn trồng cây: ban đêm toả sáng mạnh hơn
      lampMat.emissiveIntensity = d.lamp ? 1.6 : 0;
      lampLights.forEach((pl) => { pl.intensity = d.lamp ? 0.55 + 1.9 * (1 - daylight) : 0; });

      // Giàn tưới: chạy dọc nhà kính khi bơm chạy (theo góc servo của Arduino 2 nếu có)
      const wn = window.twinWater, useServo = wn && wn.alive && wn.on;
      let targetX = x0 + 5;
      if (useServo) targetX = x0 + 5 + (Math.min(140, Math.max(40, wn.angle)) - 40) / 100 * (GW - 10);
      else if (d.pump) targetX = GX + Math.sin(t / 2600) * (GW / 2 - 5);
      boomX += (targetX - boomX) * Math.min(1, dt * (d.pump ? 2.5 : 1.2));
      boom.position.x = boomX;
      boomPipeMat.color.set(d.pump ? 0x1c7ed6 : 0x9aa5ad);
      pipeA.material.color.set(d.pump ? 0x1c7ed6 : 0x9aa5ad); pipeB.material.color.set(d.pump ? 0x1c7ed6 : 0x9aa5ad);
      pumpLed.material.emissiveIntensity = d.pump ? 1.2 + 0.6 * Math.sin(t / 150) : 0;
      sprayLevel += ((d.pump ? 1 : 0) - sprayLevel) * Math.min(1, dt * 3);
      spray.visible = sprayLevel > 0.03;
      if (spray.visible) {
        spray.material.opacity = 0.9 * sprayLevel;
        for (let i = 0; i < SPRAY_N; i++) {
          const q = sprayP[i], cyc = (t / 1000 / q.life + q.phase) % 1, tau = cyc * q.life;
          if (cyc < 0.06) q.x = boomX;                            // hạt mới sinh ra tại vị trí hiện tại của giàn
          const r = q.vh * tau * sprayLevel, y = Math.max(BED_Y, RAIL_Y - 1 + q.vy * tau - 12 * tau * tau);
          sprayPos[i * 3] = q.x + Math.cos(q.ang) * r; sprayPos[i * 3 + 1] = y; sprayPos[i * 3 + 2] = GZ + q.nz + Math.sin(q.ang) * r * 0.5;
        }
        sprayGeo.attributes.position.needsUpdate = true;
      }
      soilMat.color.setRGB(0.5 - d.soil * 0.0026, 0.36 - d.soil * 0.002, 0.22 - d.soil * 0.0012);     // đất ẩm thì sẫm màu

      // Bồn nước + đèn cảnh báo
      const hWater = Math.max(0.15, d.water / 100 * (TH - 0.4)); waterMesh.scale.y = hWater; waterMesh.position.y = 0.7 + hWater / 2;
      waterMesh.material.color.set(d.tank_low ? 0xd9480f : 0x2f8fe6);
      const flash = alarm ? (Math.floor(t / 350) % 2 ? 1 : 0.15) : 0;
      beacon.material.emissiveIntensity = 2.2 * flash; beaconLight.intensity = 2.4 * flash;

      // Màn hình tủ điều khiển: giống LCD thật (3 trang, 3 giây/trang, nháy cảnh báo bồn cạn)
      const page = Math.floor(t / 3000) % 3;
      let l2 = page === 0 ? `Dat:${pad(d.soil, 3)}% Nc:${pad(d.water, 3)}%` : page === 1 ? `Sang:${pad(d.light, 3)}% D:${d.lamp ? 'BAT' : 'TAT'}` : `Bom:${d.pump ? 'BAT' : 'TAT'} Gio:${d.vent ? 'MO' : 'DONG'}`;
      if (d.tank_low && Math.floor(t / 1000) % 2 === 0) l2 = '!! BON NUOC CAN';
      const l1 = `T${pad(d.temp, 2)}°C H${pad(d.hum, 2)}% ${d.mode ? 'AUTO' : 'MANU'}`;
      if (l1 + l2 !== lastLcd) { lastLcd = l1 + l2; lcdLines = [l1, l2]; drawLcd(); }

      // Nhãn số liệu trực tiếp
      L.climate.set(`Nhiệt độ ${fmt(d.temp, '°C')}  ·  Độ ẩm ${fmt(d.hum, '%')}`, d.too_hot ? 'warn' : 'ok');
      L.light.set(`Ánh sáng ${d.light}%`, d.light < d.light_min ? 'on' : 'ok');
      L.soil.set(`Độ ẩm đất ${d.soil}%`, d.soil < d.soil_min ? 'on' : 'ok');
      L.tank.set(d.tank_low ? `Bồn nước ${d.water}%  ·  SẮP CẠN` : `Bồn nước ${d.water}%`, d.tank_low ? 'warn' : 'ok');
      L.vent.set(d.vent ? 'Cửa thông gió: đang mở' : 'Cửa thông gió: đóng', d.vent ? 'on' : 'ok');
      L.lamp.set(d.lamp ? 'Đèn trồng cây: đang bật' : 'Đèn trồng cây: tắt', d.lamp ? 'on' : 'ok');
      L.water.set(d.pump ? 'Giàn tưới: đang phun' : d.tank_low ? 'Giàn tưới: bị khoá' : 'Giàn tưới: nghỉ', d.pump ? 'on' : d.tank_low ? 'warn' : 'ok');
      L.panel.set(alarm ? 'Tủ điều khiển  ·  CẢNH BÁO' : d.mode ? 'Tủ điều khiển  ·  Tự động' : 'Tủ điều khiển  ·  Thủ công', alarm ? 'warn' : d.mode ? 'ok' : 'on');
    }
    window.twinApplyTheme = function () {};          // nền là bầu trời, không phụ thuộc giao diện sáng/tối

    // ---------- Bấm trực tiếp lên mô hình để điều khiển ----------
    const clickable = [];
    function mark(obj, action) { obj.traverse((o) => { if (o.isMesh) { o.userData.action = action; clickable.push(o); } }); }
    vents.forEach((v) => mark(v, 'vent')); lampBars.forEach((b) => mark(b, 'lamp'));
    mark(pumpBox, 'pump'); mark(tankShell, 'pump'); mark(boom, 'water'); plantMeshes.forEach((m) => mark(m, 'water'));
    const ray = new THREE.Raycaster(), mouse = new THREE.Vector2();
    function pick(e) {
      const r = renderer.domElement.getBoundingClientRect();
      mouse.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(mouse, camera);
      const hit = ray.intersectObjects(clickable, false)[0];
      return hit ? hit.object.userData.action : null;
    }
    let downAt = null, drag = null;
    renderer.domElement.addEventListener('pointerdown', (e) => { downAt = [e.clientX, e.clientY]; drag = [e.clientX, e.clientY]; });
    renderer.domElement.addEventListener('pointerup', (e) => {
      if (!downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 5) return;      // kéo để xoay thì không tính là bấm
      const a = pick(e), d = window.twinState;
      if (!a || !d || !window.twinOnline || typeof sendCmd !== 'function') return;
      if (a === 'water') sendCmd('WATER=10', 'Tưới cây 10 giây');
      else if (a === 'vent') sendCmd(d.vent ? 'VENT=CLOSE' : 'VENT=OPEN', d.vent ? 'Cửa thông gió: đóng' : 'Cửa thông gió: mở');
      else if (a === 'lamp') sendCmd(d.lamp ? 'LAMP=OFF' : 'LAMP=ON', d.lamp ? 'Đèn trồng cây: tắt' : 'Đèn trồng cây: bật');
      else sendCmd(d.pump ? 'PUMP=OFF' : 'PUMP=ON', d.pump ? 'Bơm nước: tắt' : 'Bơm nước: bật');
    });
    window.addEventListener('pointerup', () => { drag = null; });
    window.addEventListener('pointermove', (e) => {
      if (!drag) { if (e.target === renderer.domElement) renderer.domElement.style.cursor = pick(e) ? 'pointer' : 'grab'; return; }
      theta -= (e.clientX - drag[0]) * 0.006; phi = Math.min(1.5, Math.max(0.3, phi - (e.clientY - drag[1]) * 0.005));
      drag = [e.clientX, e.clientY];
    });
    window.twinPoint = (action) => {                 // toạ độ màn hình của một thiết bị (dùng khi kiểm thử)
      const o = clickable.find((m) => m.userData.action === action), v = new THREE.Vector3();
      o.getWorldPosition(v); v.project(camera);
      const r = renderer.domElement.getBoundingClientRect();
      return [r.left + (v.x + 1) / 2 * r.width, r.top + (1 - v.y) / 2 * r.height];
    };

    // ---------- khung nhìn ----------
    function fit() { if (!W() || !H()) return; renderer.setSize(W(), H()); camera.aspect = W() / H(); camera.updateProjectionMatrix(); sizeLabels(); }
    fit();
    window.addEventListener('resize', fit);
    if (window.ResizeObserver) new ResizeObserver(fit).observe(host);
    document.getElementById('twinOut').addEventListener('click', () => { zoom = Math.min(1.8, zoom + 0.12); });
    document.getElementById('twinIn').addEventListener('click', () => { zoom = Math.max(0.45, zoom - 0.12); });
    const card = document.getElementById('twinCard'), maxBtn = document.getElementById('twinMax');
    function setMax(on) {                          // phóng khung 3D ra toàn màn hình (Esc để thoát)
      card.classList.toggle('max', on); document.body.classList.toggle('twin-open', on);
      maxBtn.textContent = on ? 'Thu gọn' : 'Toàn màn hình'; fit();
    }
    maxBtn.addEventListener('click', () => setMax(!card.classList.contains('max')));
    window.addEventListener('keydown', (e) => { if (e.key === 'Escape') setMax(false); });

    let visible = true;                              // chỉ vẽ khi khung 3D đang nằm trong màn hình
    if (window.IntersectionObserver) new IntersectionObserver((e) => { visible = e[0].isIntersecting; }).observe(host);
    (function loop(t) { if (visible) { animate(t || 0); placeCamera(); renderer.render(scene, camera); } window.__twinReady = true; requestAnimationFrame(loop); })();
  } catch (err) {
    host.textContent = 'Trình duyệt này không hiển thị được mô hình 3D (cần WebGL).';
    host.classList.add('nogl');
    if (window.console) console.warn('twin.js:', err);
  }
})();
