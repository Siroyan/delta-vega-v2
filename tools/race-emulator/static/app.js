const $ = (selector) => document.querySelector(selector);
const esc = (value) => String(value ?? "").replace(/[&<>"']/g, ch => ({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[ch]));
const fmtTime = (seconds) => `${Math.floor(seconds / 60)}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;
let state = null, cases = [], selected = null, editOriginal = null, socket = null;
let draftPorts = {}, lastSeq = 0, shownEvents = [], toastTimer = null;
let tab5Mode = localStorage.getItem("race-emulator-tab5-mode") === "usb" ? "usb" : "standalone";

async function api(path, method = "GET", data = undefined) {
  const response = await fetch(path, {method, headers: data === undefined ? {} : {"Content-Type": "application/json"}, body: data === undefined ? undefined : JSON.stringify(data)});
  if (!response.ok) {
    let detail = response.statusText;
    try { detail = (await response.json()).detail || detail; } catch (_) {}
    throw new Error(detail);
  }
  return response.json();
}
function toast(message) {
  const el = $("#toast"); el.textContent = message; el.classList.add("show");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove("show"), 4000);
}
function handleError(error) { toast(error.message || String(error)); }

function renderDevice(name) {
  const device = state.devices[name];
  const element = $(name === "atom" ? "#atom-device" : "#tab5-device");
  if (element.contains(document.activeElement) && document.activeElement.tagName === "SELECT") return;
  if (name === "tab5" && tab5Mode === "standalone") {
    const runActive = state.run && ["preparing", "running"].includes(state.run.state);
    element.innerHTML = `<div class="device-head"><div class="device-title"><div class="device-icon">T5</div><div><h2>Tab5 / バッテリー運用</h2><p class="device-sub">USBログなし · 実機で操作</p></div></div><div class="device-state"><span class="status-dot idle"></span>手動</div></div>
      <div class="device-connect"><label class="mode-label">接続方式<select id="tab5-mode" ${runActive ? "disabled" : ""}><option value="standalone" selected>バッテリー単体</option><option value="usb">USB接続・ログ取得</option></select></label></div>
      <p class="standalone-note">Tab5で同じコースとGPS INPUT = PORT.Aを選び、計測開始後にブラウザでAtomS3をスタートしてください。Tab5の再起動・SDログはPCから確認できません。</p>`;
    element.querySelector("select").onchange = event => setTab5Mode(event.target.value);
    return;
  }
  const symbol = name === "atom" ? "A3" : "T5";
  const title = name === "atom" ? "AtomS3 / 信号発生" : "Tab5 / 試験対象";
  const active = device.connected;
  const chosen = draftPorts[name] || device.selected_port || "";
  const ports = state.ports.map(port => `<option value="${esc(port.device)}" ${port.device === chosen ? "selected" : ""}>${esc(port.device)} · ${esc(port.description)}</option>`).join("");
  const status = device.status;
  const tags = name === "atom"
    ? [["MODE", status.mode], ["COURSE", status.course], ["LAP", status.lap], ["PULSES", status.pulses], ["MISSED", status.missed]]
    : [["PHASE", status.phase], ["COURSE", status.course], ["GPS", status.gps], ["PULSES", status.pulses], ["SD", status.sd_ready === undefined ? undefined : status.sd_ready === "1" ? "OK" : "NG"], ["BOOTS", device.boot_count]];
  const runActive = state.run && ["preparing", "running"].includes(state.run.state);
  const modeControl = name === "tab5" ? `<label class="mode-label">接続方式<select id="tab5-mode" ${runActive ? "disabled" : ""}><option value="standalone">バッテリー単体</option><option value="usb" selected>USB接続・ログ取得</option></select></label>` : "";
  element.innerHTML = `<div class="device-head"><div class="device-title"><div class="device-icon">${symbol}</div><div><h2>${title}</h2><p class="device-sub">115200 bps · USB SERIAL</p></div></div><div class="device-state"><span class="status-dot ${active ? "online" : "offline"}"></span>${active ? "接続中" : "未接続"}</div></div>${modeControl}
    <div class="device-connect"><select aria-label="${title} のUSBポート"><option value="">USBポートを選択</option>${ports}</select><button class="${active ? "secondary" : "primary"} small" data-action="${active ? "disconnect" : "connect"}">${active ? "切断" : "接続"}</button></div>
    <div class="device-meta">${tags.filter(([,v]) => v !== undefined).map(([k,v]) => `<span class="${k === "MISSED" && Number(v) > 0 ? "alert" : ""}">${k} ${esc(v)}</span>`).join("") || "<span>状態取得待ち</span>"}</div>`;
  element.querySelector("select[aria-label]").onchange = event => { draftPorts[name] = event.target.value; };
  if (name === "tab5") element.querySelector("#tab5-mode").onchange = event => setTab5Mode(event.target.value);
  element.querySelector("button").onclick = async event => {
    try {
      if (event.target.dataset.action === "connect") {
        const port = element.querySelector("select[aria-label]").value;
        if (!port) throw new Error("USBポートを選択してください");
        await api(`/api/devices/${name}/connect`, "POST", {port});
      } else await api(`/api/devices/${name}/disconnect`, "POST", {});
      state = await api("/api/state"); renderState();
    } catch (error) { handleError(error); }
  };
}

function setTab5Mode(value) {
  if (!["usb", "standalone"].includes(value)) return;
  tab5Mode = value;
  localStorage.setItem("race-emulator-tab5-mode", value);
  if (document.activeElement instanceof HTMLElement) document.activeElement.blur();
  renderState();
}

function renderState() {
  if (!state) return;
  renderDevice("atom"); renderDevice("tab5");
  const run = state.run;
  const active = run && ["preparing", "running"].includes(run.state);
  const label = run ? ({preparing:"準備中",running:"実行中",finished:"終了"}[run.state] || run.state) : "待機中";
  $("#run-state").textContent = label;
  $("#run-pill").innerHTML = `<span class="status-dot ${active ? "online" : "idle"}"></span><span>${run ? esc(run.state.toUpperCase()) : "READY"}</span>`;
  $("#start-run").disabled = !selected || active || !state.devices.atom.connected ||
    (tab5Mode === "usb" && !state.devices.tab5.connected);
  $("#start-run").textContent = tab5Mode === "standalone" ? "▶ AtomS3走行開始" : "▶ 試験開始";
  $("#intro-description").textContent = tab5Mode === "standalone"
    ? "AtomS3の走行と異常注入を操作します。Tab5はバッテリーで動作し、実機で操作します。"
    : "AtomS3の走行と異常注入を操作し、Tab5の応答・再起動を同じ時刻で記録します。";
  $("#run-note").textContent = tab5Mode === "standalone"
    ? "Tab5で計測を開始してから押してください。ブラウザはAtomS3を走行させます。Tab5の終了・取消も実機で操作します。"
    : "試験開始時にAtomS3の条件を設定し、Tab5の計測を開始します。停止時はAtomS3のみ止めます。Tab5の計測は実機で終了・取消してください。";
  $("#stop-run").disabled = !active;
  const elapsed = run?.elapsed_s || 0;
  $("#elapsed").textContent = fmtTime(elapsed);
  const max = selected?.max_duration_s || 0;
  $("#max-duration").textContent = max ? fmtTime(max) : "—";
  $("#progress-fill").style.width = max && active ? `${Math.min(100, elapsed/max*100)}%` : "0%";
  const result = $("#run-result");
  result.hidden = !run || run.state !== "finished";
  if (!result.hidden) result.textContent = `${run.outcome} — ${run.reason} · ${run.run_dir}`;
}

async function loadCases() {
  cases = await api("/api/cases");
  if (!cases.some(item => item.id === selected?.id && !item.error)) selected = cases.find(item => !item.error) || null;
  else selected = cases.find(item => item.id === selected.id) || null;
  renderCases(); renderSelected(); renderState();
}
function renderCases() {
  $("#case-list").innerHTML = cases.length ? cases.map(item => `<button class="case-item ${selected?.id === item.id ? "active" : ""}" data-id="${esc(item.id)}"><strong>${esc(item.name || item.id)}</strong><small>${esc(item.error || item.description || "説明なし")}</small>${item.error ? "" : `<span class="case-tags"><span>${esc(item.course_id)}</span><span>${esc(item.speed_kmh)} km/h</span><span>${item.actions.length} actions</span></span>`}</button>`).join("") : "<p class='empty'>試験ケースがありません。</p>";
  document.querySelectorAll(".case-item").forEach(element => element.onclick = () => {
    selected = cases.find(item => item.id === element.dataset.id && !item.error) || null;
    renderCases(); renderSelected(); renderState();
  });
  $("#edit-case").disabled = !selected;
  $("#duplicate-case").disabled = !selected;
}
function renderSelected() {
  $("#selected-case").innerHTML = !selected ? "<p>左から試験ケースを選択してください。</p>" :
    `<h3>${esc(selected.name)}</h3><p>${esc(selected.description || "説明なし")}</p><dl><div><dt>COURSE</dt><dd>${esc(selected.course_id)}</dd></div><div><dt>SPEED</dt><dd>${esc(selected.speed_kmh)} km/h</dd></div><div><dt>GPS ACTIONS</dt><dd>${selected.actions.length}</dd></div></dl>`;
}
function appendEvents(items) {
  if (!items.length) return;
  lastSeq = Math.max(lastSeq, items[items.length-1].seq);
  shownEvents.push(...items);
  if (shownEvents.length > 600) shownEvents = shownEvents.slice(-600);
  renderEvents();
}
function renderEvents() {
  const showAll = $("#all-lines").checked;
  const filtered = shownEvents.filter(item => showAll || item.kind !== "line" || /^\[(SIM|STATUS|BOOT|APP)\]/.test(item.message));
  const list = $("#event-list");
  const nearBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 60;
  list.innerHTML = filtered.length ? filtered.slice(-300).map(item => `<div class="event-line ${esc(item.source)} ${item.kind === "alert" ? "alert" : ""}"><time>${esc(item.time.slice(11,23))}</time><b>${esc(item.source.toUpperCase())}</b><span>${esc(item.kind === "line" ? item.message : `[${item.kind.toUpperCase()}] ${item.message}`)}</span></div>`).join("") : "<p class='empty'>表示するイベントはありません。</p>";
  if (nearBottom) list.scrollTop = list.scrollHeight;
}
function live() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${proto}://${location.host}/ws`);
  socket.onopen = () => { $("#server-indicator").className = "status-dot online"; $("#server-label").textContent = "LOCAL SERVER ONLINE"; };
  socket.onmessage = event => { const data = JSON.parse(event.data); state = data.state; renderState(); appendEvents(data.events.filter(item => item.seq > lastSeq)); };
  socket.onclose = () => { $("#server-indicator").className = "status-dot offline"; $("#server-label").textContent = "RECONNECTING"; setTimeout(live, 1500); };
}

function addAction(action = {at_s:0,command:"noise 5"}) {
  const row = document.createElement("div"); row.className = "action-row";
  row.innerHTML = `<input aria-label="開始 秒" type="number" min="0" step="1" required value="${esc(action.at_s)}"><input aria-label="AtomS3 コマンド" required value="${esc(action.command)}"><button type="button" aria-label="削除">×</button>`;
  row.querySelector("button").onclick = () => row.remove();
  $("#action-list").appendChild(row);
}
function openEditor(item, duplicate = false) {
  editOriginal = duplicate ? null : item?.id || null;
  const form = $("#case-form"); form.reset();
  const data = item || {id:"",name:"",description:"",course_id:"misato_loop",speed_kmh:12,wheel_circumference_m:1.03,pulses_per_revolution:1,max_duration_s:900,actions:[]};
  for (const key of ["id","name","description","course_id","speed_kmh","wheel_circumference_m","pulses_per_revolution","max_duration_s"]) form.elements.namedItem(key).value = data[key];
  if (duplicate) form.elements.namedItem("id").value = `${data.id}-copy`;
  form.elements.namedItem("id").readOnly = !!editOriginal;
  $("#dialog-title").textContent = duplicate ? "試験ケースを複製" : item ? "試験ケースを編集" : "新しい試験ケース";
  $("#delete-case").hidden = !editOriginal;
  $("#form-error").textContent = "";
  $("#action-list").innerHTML = ""; data.actions.forEach(addAction);
  $("#case-dialog").showModal();
}
function editorData() {
  const elements = $("#case-form").elements;
  const value = key => elements.namedItem(key).value;
  return {schema_version:1,id:value("id").trim(),name:value("name").trim(),description:value("description").trim(),course_id:value("course_id").trim(),speed_kmh:Number(value("speed_kmh")),wheel_circumference_m:Number(value("wheel_circumference_m")),pulses_per_revolution:Number(value("pulses_per_revolution")),max_duration_s:Number(value("max_duration_s")),actions:Array.from($("#action-list").children).map(row => ({at_s:Number(row.children[0].value),command:row.children[1].value.trim()}))};
}

$("#new-case").onclick = () => openEditor(null);
$("#edit-case").onclick = () => selected && openEditor(selected);
$("#duplicate-case").onclick = () => selected && openEditor(selected, true);
$("#close-dialog").onclick = $("#cancel-dialog").onclick = () => $("#case-dialog").close();
$("#add-action").onclick = () => addAction();
$("#case-form").onsubmit = async event => {
  event.preventDefault();
  try { const data = editorData(); selected = await api(`/api/cases/${encodeURIComponent(data.id)}`, "PUT", data); $("#case-dialog").close(); await loadCases(); toast("試験ケースを保存しました"); }
  catch (error) { $("#form-error").textContent = error.message; }
};
$("#delete-case").onclick = async () => {
  if (!editOriginal || !confirm(`試験ケース ${editOriginal} を削除しますか？`)) return;
  try { await api(`/api/cases/${encodeURIComponent(editOriginal)}`, "DELETE"); selected = null; $("#case-dialog").close(); await loadCases(); toast("削除しました"); }
  catch (error) { $("#form-error").textContent = error.message; }
};
$("#start-run").onclick = async () => {
  if (!selected) return;
  try { await api("/api/devices/refresh", "POST", {}); await api("/api/runs/start", "POST", {case_id:selected.id, tab5_mode:tab5Mode}); toast("試験を開始しています"); }
  catch (error) { handleError(error); }
};
$("#stop-run").onclick = async () => { try { await api("/api/runs/stop", "POST", {}); toast("AtomS3を停止しました。Tab5の計測は実機で終了してください"); } catch (error) { handleError(error); } };
$("#all-lines").onchange = renderEvents;
$("#clear-view").onclick = () => { shownEvents = []; renderEvents(); };
setInterval(() => { $("#clock").textContent = new Date().toLocaleString("ja-JP"); }, 1000);
loadCases().catch(handleError); live();
