from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse, Response
from fastapi.middleware.cors import CORSMiddleware
from datetime import datetime
import time
import os

app = FastAPI(title="Edge AI Monitor Backend")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# 如果网页就在运行 MediaMTX 的电脑上打开，用 127.0.0.1
# 如果手机或其他电脑访问后台页面，改成电脑 IP，例如 http://192.168.1.20:8889/live/
VIDEO_URL = os.getenv("VIDEO_URL", "http://127.0.0.1:8889/live/")

latest_report = {}
history = []
MAX_HISTORY = 3600

today_date = datetime.now().strftime("%Y-%m-%d")
today_track_ids = set()
today_event_count = 0


def now_ms():
    return int(time.time() * 1000)


def now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def pick(data, keys, default=None):
    for key in keys:
        if key in data and data[key] is not None:
            return data[key]
    return default


def to_float(value, default=0.0):
    try:
        return float(value)
    except Exception:
        return default


def to_int(value, default=0):
    try:
        return int(value)
    except Exception:
        return default


def normalize_track(track):
    box = track.get("box", {}) if isinstance(track.get("box"), dict) else {}

    x1 = pick(track, ["x1", "left"], box.get("x1", 0))
    y1 = pick(track, ["y1", "top"], box.get("y1", 0))
    x2 = pick(track, ["x2", "right"], box.get("x2", 0))
    y2 = pick(track, ["y2", "bottom"], box.get("y2", 0))

    return {
        "track_id": pick(track, ["track_id", "tid"], "--"),
        "class_id": pick(track, ["class_id", "id", "cls"], "--"),
        "score": to_float(pick(track, ["score", "prob", "confidence"], 0), 0),
        "x1": to_int(x1),
        "y1": to_int(y1),
        "x2": to_int(x2),
        "y2": to_int(y2),
        "in_roi": bool(pick(track, ["in_roi", "roi"], False)),
        "dwell_time": to_float(pick(track, ["dwell_time", "stay_time"], 0), 0),
        "alarmed": bool(pick(track, ["alarmed", "alarm"], False)),
    }


def normalize_event(event):
    return {
        "type": pick(event, ["type", "event_type", "name"], "unknown"),
        "track_id": pick(event, ["track_id", "tid"], "--"),
        "duration": to_float(pick(event, ["duration", "dwell_time", "stay_time"], 0), 0),
        "roi": pick(event, ["roi", "roi_name", "area"], "watch_area"),
        "level": pick(event, ["level", "severity"], "warning"),
        "time": pick(event, ["time", "event_time"], now_str()),
    }


def reset_today_if_needed():
    global today_date
    global today_track_ids
    global today_event_count

    current_date = datetime.now().strftime("%Y-%m-%d")
    if current_date != today_date:
        today_date = current_date
        today_track_ids = set()
        today_event_count = 0


INDEX_HTML = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>边缘智能监控后台</title>
    <style>
        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            min-height: 100vh;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            background:
                radial-gradient(circle at top left, rgba(34, 211, 238, 0.12), transparent 34%),
                radial-gradient(circle at top right, rgba(59, 130, 246, 0.14), transparent 32%),
                #08111f;
            color: #f8fafc;
        }

        .header {
            height: 70px;
            padding: 0 28px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: linear-gradient(90deg, #0f766e, #164e63, #1e3a8a);
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.28);
        }

        .title {
            font-size: 24px;
            font-weight: 900;
            letter-spacing: 1px;
        }

        .subtitle {
            color: #dbeafe;
            font-size: 13px;
        }

        .main-layout {
            display: grid;
            grid-template-columns: minmax(0, 1.35fr) 480px;
            gap: 18px;
            padding: 18px;
            height: calc(100vh - 70px);
            overflow: hidden;
        }

        .left-panel,
        .right-panel {
            min-width: 0;
            min-height: 0;
        }

        .right-panel {
            overflow-y: auto;
            padding-right: 4px;
        }

        .section {
            background: rgba(15, 23, 42, 0.92);
            border: 1px solid #1f2d44;
            border-radius: 18px;
            padding: 16px;
            margin-bottom: 16px;
            box-shadow: 0 16px 38px rgba(0, 0, 0, 0.25);
        }

        .section h2 {
            margin: 0 0 12px 0;
            font-size: 18px;
            color: #e5e7eb;
        }

        .video-card {
            height: 100%;
            display: flex;
            flex-direction: column;
        }

        .video-box {
            width: 100%;
            max-height: calc(100vh - 170px);
            aspect-ratio: 16 / 9;
            background: #000;
            border-radius: 14px;
            overflow: hidden;
            border: 1px solid #334155;
        }

        .video-box iframe {
            width: 100%;
            height: 100%;
            border: 0;
            display: block;
            background: #000;
        }

        .video-tip {
            margin: 10px 0 0 0;
            color: #94a3b8;
            font-size: 12px;
            line-height: 1.6;
        }

        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 12px;
        }

        .metric-card {
            background: linear-gradient(180deg, #1f2937, #111827);
            border: 1px solid #334155;
            border-radius: 15px;
            padding: 16px;
            min-height: 104px;
        }

        .metric-label {
            color: #94a3b8;
            font-size: 13px;
            margin-bottom: 8px;
        }

        .metric-value {
            color: #22d3ee;
            font-size: 30px;
            font-weight: 900;
            line-height: 1.15;
        }

        .metric-unit {
            font-size: 18px;
            color: #67e8f9;
        }

        .ok {
            color: #22c55e;
        }

        .warn {
            color: #facc15;
        }

        .bad {
            color: #ef4444;
        }

        .muted {
            color: #94a3b8;
        }

        .status-row {
            display: flex;
            justify-content: space-between;
            gap: 14px;
            padding: 9px 0;
            border-bottom: 1px solid #1f2937;
            font-size: 14px;
        }

        .status-row:last-child {
            border-bottom: none;
        }

        .status-row span:last-child {
            text-align: right;
            word-break: break-all;
        }

        .pill {
            display: inline-block;
            padding: 4px 9px;
            border-radius: 999px;
            background: #172554;
            color: #93c5fd;
            font-size: 12px;
            max-width: 260px;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }

        .small {
            color: #94a3b8;
            font-size: 12px;
            line-height: 1.7;
            margin: 10px 0 0 0;
        }

        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 6px;
        }

        th,
        td {
            border-bottom: 1px solid #1f2937;
            padding: 9px 6px;
            text-align: left;
            font-size: 12px;
        }

        th {
            color: #93c5fd;
            font-weight: 800;
        }

        td {
            color: #d1d5db;
        }

        canvas {
            width: 100%;
            height: 170px;
            background: #020617;
            border-radius: 12px;
            border: 1px solid #1f2937;
        }

        .event-list {
            max-height: 220px;
            overflow-y: auto;
        }

        @media (max-width: 1300px) {
            .main-layout {
                grid-template-columns: minmax(0, 1fr) 430px;
            }
        }

        @media (max-width: 1050px) {
            body {
                overflow: auto;
            }

            .main-layout {
                grid-template-columns: 1fr;
                height: auto;
                overflow: visible;
            }

            .video-box {
                max-height: 70vh;
            }

            .right-panel {
                overflow: visible;
            }
        }

        @media (max-width: 640px) {
            .header {
                height: auto;
                padding: 16px;
                align-items: flex-start;
                flex-direction: column;
                gap: 6px;
            }

            .main-layout {
                padding: 12px;
            }

            .metrics-grid {
                grid-template-columns: 1fr;
            }

            .video-box {
                aspect-ratio: 1 / 1;
            }
        }
    </style>
</head>
<body>
    <div class="header">
        <div>
            <div class="title">边缘智能监控后台</div>
            <div class="subtitle">YOLO 检测 · ByteTrack 跟踪 · ROI 事件分析 · 实时视频展示</div>
        </div>
        <div class="subtitle" id="pageClock">--</div>
    </div>

    <main class="main-layout">
        <section class="left-panel">
            <div class="section video-card">
                <h2>实时监控画面</h2>
                <div class="video-box">
                    <iframe src="__VIDEO_URL__" allow="autoplay; fullscreen"></iframe>
                </div>
                <p class="video-tip">
                    视频由 MediaMTX 提供 WebRTC 页面。若画面无法显示，请先单独打开视频地址确认 MediaMTX 与板端 RTSP 是否正常。
                </p>
            </div>
        </section>

        <aside class="right-panel">
            <div class="section">
                <h2>实时指标</h2>
                <div class="metrics-grid">
                    <div class="metric-card">
                        <div class="metric-label">实时人数</div>
                        <div class="metric-value" id="personCount">--</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">FPS</div>
                        <div class="metric-value" id="fps">--</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">CPU 温度</div>
                        <div class="metric-value"><span id="temperature">--</span></div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">上报延迟</div>
                        <div class="metric-value"><span id="latency">--</span></div>
                    </div>
                </div>
            </div>

            <div class="section">
                <h2>智能分析</h2>
                <div class="metrics-grid">
                    <div class="metric-card">
                        <div class="metric-label">今日累计人流</div>
                        <div class="metric-value" id="todayTotal">0</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">ROI 内人数</div>
                        <div class="metric-value" id="roiCount">0</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">跟踪目标数</div>
                        <div class="metric-value" id="trackCount">0</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-label">长时间停留</div>
                        <div class="metric-value warn" id="longStayCount">0</div>
                    </div>
                </div>
            </div>

            <div class="section">
                <h2>系统状态</h2>
                <div class="status-row">
                    <span>数据上报</span>
                    <span id="reportStatus" class="bad">等待数据</span>
                </div>
                <div class="status-row">
                    <span>CPU 占用</span>
                    <span id="cpuUsage" class="ok">--</span>
                </div>
                <div class="status-row">
                    <span>GPU 占用</span>
                    <span id="gpuUsage" class="ok">--</span>
                </div>
                <div class="status-row">
                    <span>NPU 状态</span>
                    <span id="npuLoad" class="ok">--</span>
                </div>
                <div class="status-row">
                    <span>事件数量</span>
                    <span id="eventCount" class="warn">0</span>
                </div>
            </div>

            <div class="section">
                <h2>时间信息</h2>
                <div class="status-row">
                    <span>设备发送时间</span>
                    <span id="deviceTime" class="warn">--</span>
                </div>
                <div class="status-row">
                    <span>后台接收时间</span>
                    <span id="serverTime" class="ok">--</span>
                </div>
                <p class="small">
                    上报延迟 = 后台接收时间 - 设备发送时间，前提是板子与电脑系统时间基本同步。
                </p>
            </div>

            <div class="section">
                <h2>人数趋势</h2>
                <canvas id="historyChart" width="640" height="220"></canvas>
            </div>

            <div class="section">
                <h2>异常事件</h2>
                <div class="event-list">
                    <table>
                        <thead>
                            <tr>
                                <th>类型</th>
                                <th>ID</th>
                                <th>持续</th>
                                <th>区域</th>
                            </tr>
                        </thead>
                        <tbody id="eventTable">
                            <tr><td colspan="4">暂无事件</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <div class="section">
                <h2>跟踪目标</h2>
                <div class="event-list">
                    <table>
                        <thead>
                            <tr>
                                <th>ID</th>
                                <th>置信度</th>
                                <th>ROI</th>
                                <th>停留</th>
                            </tr>
                        </thead>
                        <tbody id="trackTable">
                            <tr><td colspan="4">暂无目标</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <div class="section">
                <h2>连接信息</h2>
                <div class="status-row">
                    <span>视频地址</span>
                    <span class="pill">__VIDEO_URL__</span>
                </div>
            </div>
        </aside>
    </main>

    <script>
        function setText(id, value) {
            const el = document.getElementById(id);
            if (el) el.innerText = value;
        }

        function fmtNumber(value, digits = 1) {
            const n = Number(value);
            if (!Number.isFinite(n)) return "--";
            return n.toFixed(digits);
        }

        function formatTime(ms, textValue) {
            if (textValue) return textValue;
            if (!ms) return "--";
            return new Date(Number(ms)).toLocaleString();
        }

        function refreshClock() {
            setText("pageClock", new Date().toLocaleString());
        }

        function renderTracks(tracks) {
            const tbody = document.getElementById("trackTable");
            tbody.innerHTML = "";

            if (!tracks || tracks.length === 0) {
                tbody.innerHTML = "<tr><td colspan='4'>暂无目标</td></tr>";
                return;
            }

            for (const t of tracks.slice(0, 12)) {
                const tr = document.createElement("tr");
                tr.innerHTML =
                    "<td>" + (t.track_id ?? "--") + "</td>" +
                    "<td>" + fmtNumber(t.score ?? t.prob ?? 0, 2) + "</td>" +
                    "<td>" + (t.in_roi ? "<span class='ok'>是</span>" : "<span class='muted'>否</span>") + "</td>" +
                    "<td>" + fmtNumber(t.dwell_time ?? 0, 1) + "s</td>";
                tbody.appendChild(tr);
            }
        }

        function renderEvents(events) {
            const tbody = document.getElementById("eventTable");
            tbody.innerHTML = "";

            if (!events || events.length === 0) {
                tbody.innerHTML = "<tr><td colspan='4'>暂无事件</td></tr>";
                return;
            }

            for (const e of events.slice(0, 12)) {
                const tr = document.createElement("tr");
                tr.innerHTML =
                    "<td class='bad'>" + (e.type || "--") + "</td>" +
                    "<td>" + (e.track_id ?? "--") + "</td>" +
                    "<td>" + fmtNumber(e.duration ?? e.dwell_time ?? 0, 1) + "s</td>" +
                    "<td>" + (e.roi || e.roi_name || "--") + "</td>";
                tbody.appendChild(tr);
            }
        }

        function drawHistory(history) {
            const canvas = document.getElementById("historyChart");
            const ctx = canvas.getContext("2d");
            const w = canvas.width;
            const h = canvas.height;

            ctx.clearRect(0, 0, w, h);
            ctx.fillStyle = "#020617";
            ctx.fillRect(0, 0, w, h);

            ctx.strokeStyle = "#1f2937";
            ctx.lineWidth = 1;

            for (let i = 0; i <= 4; i++) {
                const y = 18 + i * ((h - 44) / 4);
                ctx.beginPath();
                ctx.moveTo(38, y);
                ctx.lineTo(w - 16, y);
                ctx.stroke();
            }

            if (!history || history.length < 2) {
                ctx.fillStyle = "#94a3b8";
                ctx.font = "15px Microsoft YaHei";
                ctx.fillText("等待历史数据...", 42, 48);
                return;
            }

            const data = history.slice(-120);
            const maxPeople = Math.max(5, ...data.map(x => Number(x.person_count || 0)));

            const left = 38;
            const right = w - 16;
            const top = 18;
            const bottom = h - 26;

            ctx.strokeStyle = "#22d3ee";
            ctx.lineWidth = 3;
            ctx.beginPath();

            data.forEach((p, i) => {
                const x = left + i * ((right - left) / Math.max(1, data.length - 1));
                const y = bottom - (Number(p.person_count || 0) / maxPeople) * (bottom - top);

                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });

            ctx.stroke();

            ctx.fillStyle = "#94a3b8";
            ctx.font = "12px Microsoft YaHei";
            ctx.fillText(String(maxPeople), 8, top + 4);
            ctx.fillText("0", 18, bottom + 4);
            ctx.fillText("最近 " + data.length + " 次上报", left, h - 6);
        }

        async function updateDashboard() {
            try {
                const res = await fetch("/api/latest", { cache: "no-store" });
                const data = await res.json();

                if (!data || Object.keys(data).length === 0) {
                    setText("reportStatus", "等待数据");
                    document.getElementById("reportStatus").className = "bad";
                    return;
                }

                const serverMs = Number(data.server_receive_ms || 0);
                const stale = serverMs > 0 && Date.now() - serverMs > 4000;

                setText("reportStatus", stale ? "连接延迟" : "正常");
                document.getElementById("reportStatus").className = stale ? "warn" : "ok";

                setText("personCount", data.person_count ?? "--");
                setText("fps", data.fps !== undefined ? fmtNumber(data.fps, 2) : "--");
                setText("temperature", data.temperature !== undefined ? fmtNumber(data.temperature, 1) + " °C" : "--");
                setText("latency", data.latency_ms !== null && data.latency_ms !== undefined ? data.latency_ms + " ms" : "--");

                setText("todayTotal", data.today_total ?? 0);
                setText("roiCount", data.roi_count ?? 0);
                setText("trackCount", data.track_count ?? 0);
                setText("longStayCount", data.long_stay_count ?? 0);

                setText("cpuUsage", data.cpu_usage !== undefined ? fmtNumber(data.cpu_usage, 1) + " %" : "--");
                setText("gpuUsage", data.gpu_usage !== undefined ? data.gpu_usage + " %" : "--");
                setText("npuLoad", data.npu_load || "--");
                setText("eventCount", data.event_count ?? 0);

                setText("deviceTime", formatTime(data.device_send_ms, data.device_send_time));
                setText("serverTime", formatTime(data.server_receive_ms, data.server_receive_time));

                renderTracks(data.tracks || []);
                renderEvents(data.events || []);
            } catch (e) {
                setText("reportStatus", "连接异常");
                document.getElementById("reportStatus").className = "bad";
                console.log("update failed", e);
            }
        }

        async function updateHistory() {
            try {
                const res = await fetch("/api/history", { cache: "no-store" });
                const data = await res.json();
                drawHistory(data);
            } catch (e) {
                console.log("history update failed", e);
            }
        }

        setInterval(refreshClock, 1000);
        setInterval(updateDashboard, 1000);
        setInterval(updateHistory, 3000);

        refreshClock();
        updateDashboard();
        updateHistory();
    </script>
</body>
</html>
"""


@app.get("/")
async def index():
    html = INDEX_HTML.replace("__VIDEO_URL__", VIDEO_URL)
    return HTMLResponse(html)


@app.post("/api/report")
async def receive_report(request: Request):
    global latest_report
    global history
    global today_event_count

    reset_today_if_needed()

    raw = await request.json()

    server_receive_ms = now_ms()
    server_receive_time = now_str()

    raw_tracks = pick(raw, ["tracks", "results", "detections"], [])
    raw_events = pick(raw, ["events", "event_list", "alarms"], [])

    if not isinstance(raw_tracks, list):
        raw_tracks = []

    if not isinstance(raw_events, list):
        raw_events = []

    tracks = [normalize_track(t) for t in raw_tracks if isinstance(t, dict)]
    events = [normalize_event(e) for e in raw_events if isinstance(e, dict)]

    for track in tracks:
        tid = track.get("track_id")
        if isinstance(tid, int) or (isinstance(tid, str) and tid.isdigit()):
            today_track_ids.add(str(tid))

    today_event_count += len(events)

    device_send_ms = pick(raw, ["device_send_ms", "device_timestamp_ms", "timestamp_ms"], None)

    latency_ms = None
    if device_send_ms is not None:
        try:
            latency_ms = server_receive_ms - int(device_send_ms)
        except Exception:
            latency_ms = None

    person_count = pick(raw, ["person_count", "target_count", "count"], None)
    if person_count is None:
        person_count = len(tracks)

    event_count = pick(raw, ["event_count", "alarm_count"], None)
    if event_count is None:
        event_count = len(events)

    roi_count = pick(raw, ["roi_count", "roi_person_count"], None)
    if roi_count is None:
        roi_count = sum(1 for t in tracks if t.get("in_roi"))

    long_stay_count = pick(raw, ["long_stay_count", "stay_count"], None)
    if long_stay_count is None:
        long_stay_count = 0
        for event in events:
            event_type = str(event.get("type", "")).lower()
            if "stay" in event_type or "停留" in event_type or "long" in event_type:
                long_stay_count += 1
        for track in tracks:
            if track.get("alarmed"):
                long_stay_count += 1

    latest_report = {
        "person_count": to_int(person_count, 0),
        "fps": to_float(pick(raw, ["fps", "current_fps"], 0), 0),
        "temperature": to_float(pick(raw, ["temperature", "temp", "cpu_temp"], 0), 0),
        "cpu_usage": to_float(pick(raw, ["cpu_usage", "cpu"], 0), 0),
        "gpu_usage": pick(raw, ["gpu_usage", "gpu_load", "gpu"], 0),
        "npu_load": pick(raw, ["npu_load", "npu"], "--"),

        "track_count": to_int(pick(raw, ["track_count"], len(tracks)), len(tracks)),
        "roi_count": to_int(roi_count, 0),
        "event_count": to_int(event_count, 0),
        "long_stay_count": to_int(long_stay_count, 0),
        "today_total": to_int(pick(raw, ["today_total", "total_people"], len(today_track_ids)), len(today_track_ids)),
        "today_event_count": today_event_count,

        "device_send_ms": device_send_ms,
        "device_send_time": pick(raw, ["device_send_time", "device_time"], None),
        "server_receive_ms": server_receive_ms,
        "server_receive_time": server_receive_time,
        "latency_ms": latency_ms,

        "tracks": tracks,
        "events": events,
        "video_url": VIDEO_URL,
    }

    history.append({
        "server_receive_ms": server_receive_ms,
        "server_receive_time": server_receive_time,
        "person_count": latest_report["person_count"],
        "fps": latest_report["fps"],
        "temperature": latest_report["temperature"],
        "latency_ms": latest_report["latency_ms"],
        "event_count": latest_report["event_count"],
        "roi_count": latest_report["roi_count"],
        "track_count": latest_report["track_count"],
        "long_stay_count": latest_report["long_stay_count"],
    })

    if len(history) > MAX_HISTORY:
        history = history[-MAX_HISTORY:]

    print(
        f"[REPORT] people={latest_report['person_count']} "
        f"tracks={latest_report['track_count']} "
        f"roi={latest_report['roi_count']} "
        f"events={latest_report['event_count']} "
        f"fps={latest_report['fps']:.2f} "
        f"latency={latest_report['latency_ms']}ms "
        f"time={server_receive_time}"
    )

    return {"ok": True}


@app.get("/api/latest")
async def get_latest():
    return JSONResponse(latest_report)


@app.get("/api/history")
async def get_history():
    return JSONResponse(history)


@app.get("/api/health")
async def health():
    return {
        "ok": True,
        "video_url": VIDEO_URL,
        "has_report": bool(latest_report),
        "history_count": len(history),
    }


@app.post("/api/clear")
async def clear_history():
    global latest_report
    global history
    global today_track_ids
    global today_event_count

    latest_report = {}
    history = []
    today_track_ids = set()
    today_event_count = 0

    return {"ok": True}


@app.get("/favicon.ico")
async def favicon():
    return Response(status_code=204)