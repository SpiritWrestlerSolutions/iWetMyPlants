/**
 * CALIBRATION MODULE
 * Handles WebSocket stream and graph rendering
 */
const App = {
    ws: null,
    points: { dry: null, wet: null },
    dataLog: [],
    maxLog: 100,
    isDrawing: false,

    init() {
        this.cacheDOM();
        this.bindEvents();
        this.loadSensors();
    },

    cacheDOM() {
        this.dom = {
            select: document.getElementById('sensor-select'),
            status: document.getElementById('connection-badge'),
            raw: document.getElementById('raw-value'),
            avg: document.getElementById('avg-value'),
            pct: document.getElementById('percent-value'),
            canvas: document.getElementById('reading-graph'),
            btnSave: document.getElementById('btn-save'),
            drySaved: document.getElementById('dry-saved'),
            wetSaved: document.getElementById('wet-saved')
        };
        this.ctx = this.dom.canvas.getContext('2d');
    },

    bindEvents() {
        this.dom.select.onchange = () => this.connect();
        document.getElementById('btn-set-dry').onclick = () => this.capturePoint('dry');
        document.getElementById('btn-set-wet').onclick = () => this.capturePoint('wet');
        this.dom.btnSave.onclick = () => this.sendSignal('save');
    },

    async loadSensors() {
        try {
            const res = await fetch('/api/sensors');
            const data = await res.json();
            this.dom.select.innerHTML = data.map((s, i) => 
                `<option value="${i}">${s.name || 'Sensor ' + (i+1)}</option>`).join('');
            this.connect();
        } catch (e) {
            this.updateStatus('API Error', 'error');
        }
    },

    connect() {
        if (this.ws) this.ws.close();
        
        const id = this.dom.select.value;
        this.ws = new WebSocket(`ws://${location.host}/ws/calibration?sensor=${id}`);

        this.ws.onopen = () => this.updateStatus('Live', 'success');
        this.ws.onclose = () => {
            this.updateStatus('Offline', 'error');
            setTimeout(() => this.connect(), 3000); // Auto-reconnect
        };

        this.ws.onmessage = (msg) => {
            const data = JSON.parse(msg.data);
            if (data.type === 'reading') this.handleData(data);
            if (data.type === 'calibration') this.updatePoints(data);
        };
    },

    handleData(data) {
        this.dom.raw.value = data.raw;
        this.dom.avg.value = data.avg;
        this.dom.pct.value = `${data.percent}%`;

        this.dataLog.push(data.raw);
        if (this.dataLog.length > this.maxLog) this.dataLog.shift();

        if (!this.isDrawing) {
            this.isDrawing = true;
            requestAnimationFrame(() => this.draw());
        }
    },

    draw() {
        const { width, height } = this.dom.canvas;
        const ctx = this.ctx;
        
        ctx.clearRect(0, 0, width, height);
        if (this.dataLog.length < 2) return;

        const min = Math.min(...this.dataLog) * 0.95;
        const max = Math.max(...this.dataLog) * 1.05;
        const range = max - min;

        ctx.beginPath();
        ctx.strokeStyle = '#4ade80';
        ctx.lineWidth = 2;

        this.dataLog.forEach((val, i) => {
            const x = (i / (this.maxLog - 1)) * width;
            const y = height - ((val - min) / range) * height;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });

        ctx.stroke();
        this.isDrawing = false;
    },

    capturePoint(type) {
        const val = parseInt(this.dom.avg.value);
        if (isNaN(val)) return;

        this.points[type] = val;
        this.dom[`${type}Saved`].textContent = val;
        this.sendSignal(`set_${type}`);
        
        // Validation: Dry must be > Wet for most soil sensors (capacitive)
        const isValid = this.points.dry && this.points.wet && this.points.dry > this.points.wet;
        this.dom.btnSave.disabled = !isValid;
    },

    sendSignal(type) {
        this.ws.send(JSON.stringify({ type }));
    },

    updateStatus(text, className) {
        this.dom.status.textContent = text;
        this.dom.status.className = `badge ${className}`;
    }
};

App.init();