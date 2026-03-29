import AsyncStorage from '@react-native-async-storage/async-storage';

const HISTORY_KEY = '@greenhouse_alert_history';
const THRESHOLDS_KEY = '@greenhouse_thresholds';
const COOLDOWN_MS = 5 * 60 * 1000;

export const DEFAULT_THRESHOLDS = {
  temperature: { min: 10, max: 40,   unit: '°C',  label: 'Temperature' },
  humidity:    { min: 20, max: 90,   unit: '%',   label: 'Humidity' },
  soil:        { min: 15, max: 100,  unit: '%',   label: 'Soil Moisture' },
  lux:         { min: 0,  max: 80000, unit: 'lux', label: 'Light' },
  fan_pct:     { min: 0,  max: 100,  unit: '%',   label: 'Fan Speed' },
};

class AlertService {
  lastAlertTime = {};
  historyListeners = [];
  toastListeners = [];

  async loadThresholds() {
    try {
      const saved = await AsyncStorage.getItem(THRESHOLDS_KEY);
      return saved ? { ...DEFAULT_THRESHOLDS, ...JSON.parse(saved) } : { ...DEFAULT_THRESHOLDS };
    } catch {
      return { ...DEFAULT_THRESHOLDS };
    }
  }

  async saveThresholds(thresholds) {
    await AsyncStorage.setItem(THRESHOLDS_KEY, JSON.stringify(thresholds));
  }

  async loadHistory() {
    try {
      const saved = await AsyncStorage.getItem(HISTORY_KEY);
      return saved ? JSON.parse(saved) : [];
    } catch {
      return [];
    }
  }

  async addToHistory(entry) {
    const history = await this.loadHistory();
    const updated = [entry, ...history].slice(0, 100);
    await AsyncStorage.setItem(HISTORY_KEY, JSON.stringify(updated));
    this.historyListeners.forEach((fn) => fn(updated));
  }

  async clearHistory() {
    await AsyncStorage.removeItem(HISTORY_KEY);
    this.historyListeners.forEach((fn) => fn([]));
  }

  onHistoryChange(fn) {
    this.historyListeners.push(fn);
    return () => { this.historyListeners = this.historyListeners.filter((l) => l !== fn); };
  }

  // In-app toast system
  onToast(fn) {
    this.toastListeners.push(fn);
    return () => { this.toastListeners = this.toastListeners.filter((l) => l !== fn); };
  }

  _showToast(title, body, type) {
    this.toastListeners.forEach((fn) => fn({ title, body, type }));
  }

  async checkSensorData(sensorData) {
    const thresholds = await this.loadThresholds();
    const now = Date.now();

    if (sensorData.fault && sensorData.fault !== '0' && sensorData.fault !== 'OK' && sensorData.fault !== null) {
      const key = 'fault';
      if (!this.lastAlertTime[key] || now - this.lastAlertTime[key] > COOLDOWN_MS) {
        this.lastAlertTime[key] = now;
        await this._fire('fault', 'Fault Detected', `${sensorData.fault}`, 'fault');
      }
    }

    for (const [sensor, threshold] of Object.entries(thresholds)) {
      const value = parseFloat(sensorData[sensor]);
      if (isNaN(value) || sensorData[sensor] === null) continue;

      const key = sensor;
      if (this.lastAlertTime[key] && now - this.lastAlertTime[key] < COOLDOWN_MS) continue;

      if (value < threshold.min) {
        this.lastAlertTime[key] = now;
        await this._fire(sensor, `${threshold.label} Too Low`,
          `${value}${threshold.unit} — below min of ${threshold.min}${threshold.unit}`, 'low');
      } else if (value > threshold.max) {
        this.lastAlertTime[key] = now;
        await this._fire(sensor, `${threshold.label} Too High`,
          `${value}${threshold.unit} — above max of ${threshold.max}${threshold.unit}`, 'high');
      }
    }
  }

  async _fire(sensor, title, body, type) {
    this._showToast(title, body, type);
    await this.addToHistory({
      id: `${sensor}_${Date.now()}`,
      sensor, title, body, type,
      timestamp: new Date().toISOString(),
    });
  }
}

export default new AlertService();
