const $ = (id) => document.getElementById(id);
const state = { courses: [], course: null, strategy: null, lap: 1, active: null, valid: false,
  validationSequence: 0, drag: null };
const routeNames = { first_lap: "STARTからの進入路", regular_lap: "周回路", final_lap: "ゴールへの分岐" };

async function request(path, options = {}) {
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error((data.errors || [response.statusText]).join("\n"));
  return data;
}
function send(path, data) {
  return request(path, { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data) });
}
function notice(message) {
  const node = $("toast"); node.textContent = message; node.hidden = false;
  clearTimeout(notice.timer); notice.timer = setTimeout(() => node.hidden = true, 4200);
}
function courseSummary() { return state.courses.find(c => c.folder === $("course-select").value); }
function routeId(lap) { return lap === 1 ? "first_lap" : lap === state.course.data.lap_count ? "final_lap" : "regular_lap"; }
function activeLap() { return state.strategy.laps[state.lap - 1]; }
function formatSeconds(value) {
  if (!Number.isInteger(value) || value < 1 || value > 86400) return "";
  const seconds = String(value % 60).padStart(2, "0");
  const minutes = String(Math.floor(value / 60) % 60).padStart(2, "0");
  return value >= 3600 ? `${Math.floor(value / 3600)}:${minutes}:${seconds}`
    : `${String(Math.floor(value / 60)).padStart(2, "0")}:${seconds}`;
}
function parseTime(value) {
  const parts = value.trim().split(":");
  if (parts.length < 2 || parts.length > 3 || !parts.every(part => /^\d{1,5}$/.test(part))) return null;
  const numbers = parts.map(Number);
  if (numbers.at(-1) >= 60 || (parts.length === 3 && numbers[1] >= 60)) return null;
  const seconds = parts.length === 2 ? numbers[0] * 60 + numbers[1]
    : numbers[0] * 3600 + numbers[1] * 60 + numbers[2];
  return seconds >= 1 && seconds <= 86400 ? seconds : null;
}
function upgradeLegacyPlan(strategy, summary) {
  if (strategy.schema_version !== 1) return false;
  strategy.schema_version = 2;
  strategy.total_target_s = summary.total_target_s;
  strategy.laps.forEach(lap => { lap.lap_target_s = summary.lap_target_s; });
  return true;
}
function freshPlan(course) {
  return { schema_version: 2, course_id: course.summary.course_id, plan_id: "new-strategy",
    plan_type: "race", total_target_s: course.summary.total_target_s,
    laps: Array.from({ length: course.summary.lap_count }, (_, i) => ({
      lap: i + 1, route_id: i === 0 ? "first_lap" : i + 1 === course.summary.lap_count ? "final_lap" : "regular_lap",
      lap_target_s: course.summary.lap_target_s,
      runs: [{ on_s_m: 0, off_s_m: 0 }]
    })) };
}
function filenameFromPlan(planId) {
  return (planId.replace(/[^A-Za-z0-9_-]/g, "_").slice(0, 30) || "strategy") + ".json";
}
function persist() {
  if (!state.strategy) return;
  try { localStorage.setItem("vega-strategy-editor-draft-v1", JSON.stringify({
    folder: courseSummary().folder, strategy: state.strategy, filename: $("filename").value
  })); } catch (_) { /* Private browsing may disallow storage. */ }
}
function syncSetup() {
  $("plan-id").value = state.strategy.plan_id;
  $("plan-type").value = state.strategy.plan_type;
  $("total-target").value = formatSeconds(state.strategy.total_target_s);
  $("destination-name").textContent = $("filename").value || "strategy.json";
  const summary = courseSummary();
  $("course-notice").hidden = summary.supported;
  $("course-notice").textContent = summary.supported ? "" :
    `${summary.name}は${summary.lap_count}周です。Tab5の現行戦略形式は7周までのため、このコースの戦略は書き出せません。`;
  $("lap-count").textContent = `${summary.lap_count} LAPS`;
}
async function loadCourse(folder, makeNew = true) {
  const course = await request("/api/courses/" + encodeURIComponent(folder));
  state.course = course;
  $("course-select").value = folder;
  $("map-image").src = course.summary.image_url;
  if (makeNew) {
    state.strategy = freshPlan(course);
    $("filename").value = filenameFromPlan(state.strategy.plan_id);
  }
  state.lap = 1;
  state.active = null;
  syncSetup(); render();
}
function renderTabs() {
  $("lap-tabs").replaceChildren(...state.strategy.laps.map(lap => {
    const button = document.createElement("button");
    button.type = "button"; button.textContent = `LAP ${lap.lap}`;
    button.className = lap.lap === state.lap ? "active" : "";
    button.setAttribute("role", "tab");
    button.setAttribute("aria-selected", lap.lap === state.lap);
    button.onclick = () => { state.lap = lap.lap; state.active = null; render(); };
    return button;
  }));
}
function renderRows() {
  const lap = activeLap();
  $("lap-title").textContent = `LAP ${state.lap}`;
  $("route-name").textContent = `${routeNames[lap.route_id]} · ${state.course.data.routes[lap.route_id].length_m.toFixed(1)} m`;
  $("route-length").textContent = `${state.course.data.routes[lap.route_id].length_m.toFixed(1)} m`;
  $("lap-target").value = formatSeconds(lap.lap_target_s);
  const rows = lap.runs.map((run, index) => {
    const row = document.createElement("div"); row.className = "run-row";
    const label = document.createElement("div"); label.className = "run-number";
    label.textContent = String(index + 1).padStart(2, "0"); row.append(label);
    for (const [key, title] of [["on_s_m", "ENGINE ON · m"], ["off_s_m", "ENGINE OFF · m"]]) {
      const field = document.createElement("label"); field.textContent = title;
      const input = document.createElement("input"); input.type = "number";
      input.min = "0"; input.step = "0.1";
      input.max = String(state.course.data.routes[lap.route_id].length_m);
      input.value = run[key]; input.dataset.run = index; input.dataset.key = key;
      input.addEventListener("focus", () => { state.active = { index, key }; renderMap(); });
      input.addEventListener("change", () => {
        run[key] = input.value === "" ? null : Number(input.value);
        persist(); renderMap(); validate();
      });
      field.append(input); row.append(field);
    }
    const remove = document.createElement("button"); remove.type = "button";
    remove.className = "remove"; remove.textContent = "×"; remove.title = "区間を削除";
    remove.disabled = lap.runs.length <= 1;
    remove.onclick = () => { lap.runs.splice(index, 1); state.active = null; persist(); render(); };
    row.append(remove);
    return row;
  });
  $("run-list").replaceChildren(...rows);
  $("copy-lap").disabled = state.strategy.laps.length <= 1;
}
function matrixPoint(point) {
  const m = state.course.data.render.local_to_pixel_matrix_2x3;
  return [m[0][0] * point.east_m + m[0][1] * point.north_m + m[0][2],
    m[1][0] * point.east_m + m[1][1] * point.north_m + m[1][2]];
}
function interpolate(points, s) {
  if (s <= 0) return points[0];
  if (s >= points.at(-1).s_m) return points.at(-1);
  let right = 1;
  while (right < points.length && points[right].s_m < s) right++;
  const before = points[right - 1], after = points[right];
  const t = (s - before.s_m) / (after.s_m - before.s_m);
  return { east_m: before.east_m + t * (after.east_m - before.east_m),
    north_m: before.north_m + t * (after.north_m - before.north_m), s_m: s };
}
function polyline(points, start = 0, end = points.at(-1).s_m) {
  const vertices = [interpolate(points, start), ...points.filter(p => p.s_m > start && p.s_m < end), interpolate(points, end)];
  return vertices.map(p => matrixPoint(p).map(n => n.toFixed(2)).join(",")).join(" ");
}
function svgNode(tag, attributes) {
  const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
  for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
  return node;
}
function renderMap() {
  const svg = $("map-overlay"), lap = activeLap();
  const route = state.course.data.routes[lap.route_id];
  if (!route) { svg.replaceChildren(); return; }
  const points = route.points;
  const base = svgNode("polyline", { points: polyline(points), class: "route-path" });
  svg.replaceChildren(base);
  lap.runs.forEach((run, index) => {
    if (!Number.isFinite(run.on_s_m) || !Number.isFinite(run.off_s_m)) return;
    if (run.on_s_m >= 0 && run.off_s_m > run.on_s_m && run.off_s_m <= route.length_m) {
      svg.append(svgNode("polyline", { points: polyline(points, run.on_s_m, run.off_s_m), class: "run-path" }));
    }
    for (const [key, type] of [["on_s_m", "on"], ["off_s_m", "off"]]) {
      const s = run[key];
      if (s < 0 || s > route.length_m) continue;
      const [cx, cy] = matrixPoint(interpolate(points, s));
      const marker = svgNode("circle", { cx, cy, r: 8, class: `marker ${type}` +
        (state.active?.index === index && state.active?.key === key ? " active-marker" : "") });
      marker.dataset.index = index; marker.dataset.key = key;
      marker.setAttribute("aria-label", `区間${index + 1} ${type.toUpperCase()} ${s}m`);
      svg.append(marker);
    }
  });
}
function nearestDistance(event) {
  const rect = $("map-overlay").getBoundingClientRect();
  const x = (event.clientX - rect.left) * 480 / rect.width;
  const y = (event.clientY - rect.top) * 480 / rect.height;
  const points = state.course.data.routes[activeLap().route_id].points;
  let best = { error: Infinity, s: 0 };
  for (let i = 1; i < points.length; i++) {
    const [ax, ay] = matrixPoint(points[i - 1]), [bx, by] = matrixPoint(points[i]);
    const dx = bx - ax, dy = by - ay, length2 = dx * dx + dy * dy;
    const t = length2 ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / length2)) : 0;
    const error = (x - (ax + dx * t)) ** 2 + (y - (ay + dy * t)) ** 2;
    if (error < best.error) best = { error, s: points[i - 1].s_m + t * (points[i].s_m - points[i - 1].s_m) };
  }
  return Math.round(best.s * 10) / 10;
}
function setActiveDistance(event) {
  if (!state.active) return;
  const { index, key } = state.active;
  const value = nearestDistance(event);
  activeLap().runs[index][key] = value;
  const input = document.querySelector(`input[data-run="${index}"][data-key="${key}"]`);
  if (input) input.value = value;
  renderMap(); persist(); validate();
}
function render() { renderTabs(); renderRows(); renderMap(); validate(); }
async function validate() {
  const sequence = ++state.validationSequence;
  state.valid = false; $("export").disabled = true;
  try {
    const result = await send("/api/validate", { strategy: state.strategy, filename: $("filename").value });
    if (sequence !== state.validationSequence) return;
    const box = $("validation");
    box.replaceChildren();
    box.className = result.errors.length ? "invalid" : "";
    if (result.errors.length) {
      const list = document.createElement("ul");
      result.errors.slice(0, 12).forEach(message => { const item = document.createElement("li"); item.textContent = message; list.append(item); });
      if (result.errors.length > 12) { const item = document.createElement("li"); item.textContent = `ほか${result.errors.length - 12}件`; list.append(item); }
      box.append(list);
    } else box.textContent = "Tab5の戦略形式に適合しています。";
    $("byte-count").textContent = `${result.bytes ?? "—"} / 4096 bytes`;
    state.valid = result.errors.length === 0;
    $("export").disabled = !state.valid;
  } catch (error) {
    if (sequence !== state.validationSequence) return;
    $("validation").className = "invalid";
    $("validation").textContent = error.message;
  }
}
async function importBytes(bytes, suggestedFilename) {
  try {
    const imported = await request("/api/import", { method: "POST", body: bytes });
    const strategy = imported.strategy;
    const summary = state.courses.find(c => c.course_id === strategy?.course_id);
    if (!summary) throw new Error("このリポジトリにないコースIDです。コースデータを追加してから読み込んでください。");
    if (!Array.isArray(strategy.laps) || strategy.laps.length !== summary.lap_count)
      throw new Error("周回数がコースと一致しません。");
    if (!strategy.plan_type && strategy.plan_id === "dummy-race-001") strategy.plan_type = "demo";
    const upgraded = upgradeLegacyPlan(strategy, summary);
    const filename = /^[A-Za-z0-9_-]{1,30}\.json$/.test(suggestedFilename) ?
      suggestedFilename : filenameFromPlan(strategy.plan_id || "strategy");
    const check = await send("/api/validate", { strategy, filename });
    if (check.errors.length) throw new Error(check.errors.slice(0, 3).join("\n"));
    await loadCourse(summary.folder, false);
    state.strategy = strategy;
    state.strategy.laps.sort((a, b) => a.lap - b.lap);
    $("filename").value = filename;
    syncSetup(); render(); persist(); notice(upgraded
      ? "旧形式の戦略を移行しました。目標時間にはコース初期値を入れています。必ず確認してください。"
      : "戦略を読み込みました。内容を確認してください。");
  } catch (error) { notice(error.message); }
}
async function initialize() {
  try {
    state.courses = await request("/api/courses");
    const selector = $("course-select");
    state.courses.forEach(course => {
      const option = document.createElement("option"); option.value = course.folder;
      option.textContent = `${course.name} · ${course.lap_count}周${course.supported ? "" : " · 戦略非対応"}`;
      selector.append(option);
    });
    const examples = await request("/api/examples");
    examples.forEach(example => {
      const course = state.courses.find(item => item.course_id === example.course_id);
      const option = document.createElement("option"); option.value = example.filename;
      option.textContent = `${example.plan_id} · ${course?.name || example.course_id}`;
      $("example-select").append(option);
    });
    let draft = null;
    try { draft = JSON.parse(localStorage.getItem("vega-strategy-editor-draft-v1")); } catch (_) { /* Ignore invalid draft. */ }
    const folder = draft && state.courses.some(c => c.folder === draft.folder) ? draft.folder : state.courses[0].folder;
    await loadCourse(folder);
    if (draft && draft.folder === folder && draft.strategy?.course_id === state.course.summary.course_id &&
        Array.isArray(draft.strategy.laps) && draft.strategy.laps.length === state.course.summary.lap_count) {
      state.strategy = draft.strategy;
      const upgraded = upgradeLegacyPlan(state.strategy, state.course.summary);
      $("filename").value = draft.filename;
      syncSetup(); render(); if (upgraded) persist(); notice(upgraded
        ? "旧形式の下書きを移行しました。目標時間を確認してください。"
        : "前回の下書きを復元しました。");
    }
    selector.onchange = () => {
      $("example-select").value = "";
      loadCourse(selector.value).then(persist).catch(error => notice(error.message));
    };
    $("example-select").onchange = async event => {
      if (!event.target.value) return;
      const filename = event.target.value;
      try {
        const response = await fetch("/api/examples/" + encodeURIComponent(filename));
        if (!response.ok) throw new Error("同梱戦略を読み込めません。");
        await importBytes(await response.arrayBuffer(), filename);
      } catch (error) { notice(error.message); }
    };
    $("file-input").onchange = async event => {
      const file = event.target.files[0]; if (!file) return;
      $("example-select").value = "";
      await importBytes(await file.arrayBuffer(), file.name);
      event.target.value = "";
    };
    $("plan-id").oninput = event => {
      const oldDefault = filenameFromPlan(state.strategy.plan_id);
      state.strategy.plan_id = event.target.value;
      if ($("filename").value === oldDefault) {
        $("filename").value = filenameFromPlan(state.strategy.plan_id);
        $("destination-name").textContent = $("filename").value;
      }
      persist(); validate();
    };
    $("plan-type").onchange = event => { state.strategy.plan_type = event.target.value; persist(); validate(); };
    $("total-target").onchange = event => {
      state.strategy.total_target_s = parseTime(event.target.value); persist(); validate();
    };
    $("lap-target").onchange = event => {
      activeLap().lap_target_s = parseTime(event.target.value); persist(); validate();
    };
    $("filename").oninput = event => { $("destination-name").textContent = event.target.value || "strategy.json"; persist(); validate(); };
    $("add-run").onclick = () => { activeLap().runs.push({ on_s_m: 0, off_s_m: 0 }); persist(); render(); };
    $("copy-lap").onclick = () => {
      const source = activeLap().runs, skipped = [];
      for (const lap of state.strategy.laps) {
        if (lap.lap === state.lap) continue;
        const length = state.course.data.routes[lap.route_id].length_m;
        if (source.some(run => run.off_s_m > length)) { skipped.push(lap.lap); continue; }
        lap.runs = source.map(run => ({ ...run }));
      }
      persist(); render(); notice(skipped.length ? `LAP ${skipped.join(", ")} は経路長を超えるためコピーしませんでした。` : "他の周回へコピーしました。各周の経路長を確認してください。");
    };
    const svg = $("map-overlay");
    svg.addEventListener("pointerdown", event => {
      const marker = event.target.closest(".marker");
      if (marker) {
        state.active = { index: Number(marker.dataset.index), key: marker.dataset.key };
        state.drag = event.pointerId; svg.setPointerCapture(event.pointerId); renderMap();
      } else setActiveDistance(event);
    });
    svg.addEventListener("pointermove", event => { if (state.drag === event.pointerId) setActiveDistance(event); });
    svg.addEventListener("pointerup", () => { state.drag = null; });
    svg.addEventListener("pointercancel", () => { state.drag = null; });
    $("export").onclick = async () => {
      if (!state.valid) return;
      const filename = $("filename").value;
      const response = await fetch("/api/export", { method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ strategy: state.strategy, filename }) });
      if (!response.ok) { const result = await response.json(); return notice(result.errors.join("\n")); }
      const url = URL.createObjectURL(await response.blob());
      const anchor = document.createElement("a"); anchor.href = url; anchor.download = filename;
      document.body.append(anchor); anchor.click(); anchor.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      notice(`${filename} をダウンロードしました。microSDの /vega/strategies/ へコピーしてください。`);
    };
  } catch (error) { $("validation").className = "invalid"; $("validation").textContent = error.message; }
}
initialize();
