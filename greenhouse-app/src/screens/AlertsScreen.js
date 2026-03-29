import React, { useState, useEffect, useCallback } from 'react';
import {
  View, Text, StyleSheet, ScrollView, TouchableOpacity,
  TextInput, Alert, RefreshControl,
} from 'react-native';
import AlertService, { DEFAULT_THRESHOLDS } from '../services/AlertService';

const TYPE_COLOR = {
  fault: '#f44336',
  high:  '#ff9800',
  low:   '#2196f3',
};

const TYPE_ICON = {
  fault: '⚠',
  high:  '↑',
  low:   '↓',
};

function AlertItem({ item }) {
  const color = TYPE_COLOR[item.type] || '#888';
  const icon = TYPE_ICON[item.type] || '•';
  const time = new Date(item.timestamp).toLocaleString();

  return (
    <View style={[styles.alertItem, { borderLeftColor: color }]}>
      <View style={styles.alertHeader}>
        <Text style={[styles.alertIcon, { color }]}>{icon}</Text>
        <Text style={[styles.alertTitle, { color }]}>{item.title}</Text>
        <Text style={styles.alertTime}>{time}</Text>
      </View>
      <Text style={styles.alertBody}>{item.body}</Text>
    </View>
  );
}

function ThresholdRow({ sensor, threshold, onChange }) {
  const [min, setMin] = useState(String(threshold.min));
  const [max, setMax] = useState(String(threshold.max));

  const commit = () => {
    const minVal = parseFloat(min);
    const maxVal = parseFloat(max);
    if (isNaN(minVal) || isNaN(maxVal) || minVal >= maxVal) {
      Alert.alert('Invalid', 'Min must be less than max');
      return;
    }
    onChange(sensor, { ...threshold, min: minVal, max: maxVal });
  };

  return (
    <View style={styles.thresholdRow}>
      <Text style={styles.thresholdLabel}>{threshold.label}</Text>
      <View style={styles.thresholdInputs}>
        <View style={styles.thresholdField}>
          <Text style={styles.thresholdFieldLabel}>Min</Text>
          <TextInput
            style={styles.thresholdInput}
            value={min}
            onChangeText={setMin}
            onBlur={commit}
            keyboardType="numeric"
            returnKeyType="done"
            onSubmitEditing={commit}
          />
          <Text style={styles.thresholdUnit}>{threshold.unit}</Text>
        </View>
        <View style={styles.thresholdField}>
          <Text style={styles.thresholdFieldLabel}>Max</Text>
          <TextInput
            style={styles.thresholdInput}
            value={max}
            onChangeText={setMax}
            onBlur={commit}
            keyboardType="numeric"
            returnKeyType="done"
            onSubmitEditing={commit}
          />
          <Text style={styles.thresholdUnit}>{threshold.unit}</Text>
        </View>
      </View>
    </View>
  );
}

export default function AlertsScreen() {
  const [tab, setTab] = useState('history'); // 'history' | 'thresholds'
  const [history, setHistory] = useState([]);
  const [thresholds, setThresholds] = useState(DEFAULT_THRESHOLDS);
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async () => {
    const [h, t] = await Promise.all([
      AlertService.loadHistory(),
      AlertService.loadThresholds(),
    ]);
    setHistory(h);
    setThresholds(t);
  }, []);

  useEffect(() => {
    load();
    const unsub = AlertService.onHistoryChange(setHistory);
    return unsub;
  }, [load]);

  const onRefresh = async () => {
    setRefreshing(true);
    await load();
    setRefreshing(false);
  };

  const updateThreshold = async (sensor, updated) => {
    const next = { ...thresholds, [sensor]: updated };
    setThresholds(next);
    await AlertService.saveThresholds(next);
  };

  const clearHistory = () => {
    Alert.alert('Clear History', 'Delete all alert history?', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Clear', style: 'destructive',
        onPress: async () => { await AlertService.clearHistory(); setHistory([]); },
      },
    ]);
  };

  const resetThresholds = async () => {
    setThresholds({ ...DEFAULT_THRESHOLDS });
    await AlertService.saveThresholds(DEFAULT_THRESHOLDS);
  };

  return (
    <View style={styles.container}>
      {/* Tab bar */}
      <View style={styles.tabBar}>
        <TouchableOpacity
          style={[styles.tabBtn, tab === 'history' && styles.tabBtnActive]}
          onPress={() => setTab('history')}
        >
          <Text style={[styles.tabBtnText, tab === 'history' && styles.tabBtnTextActive]}>
            History {history.length > 0 ? `(${history.length})` : ''}
          </Text>
        </TouchableOpacity>
        <TouchableOpacity
          style={[styles.tabBtn, tab === 'thresholds' && styles.tabBtnActive]}
          onPress={() => setTab('thresholds')}
        >
          <Text style={[styles.tabBtnText, tab === 'thresholds' && styles.tabBtnTextActive]}>
            Thresholds
          </Text>
        </TouchableOpacity>
      </View>

      {tab === 'history' && (
        <ScrollView
          contentContainerStyle={styles.scroll}
          refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} tintColor="#097479" />}
        >
          {history.length === 0 ? (
            <View style={styles.empty}>
              <Text style={styles.emptyIcon}>✓</Text>
              <Text style={styles.emptyText}>No alerts</Text>
              <Text style={styles.emptySubtext}>Alerts appear here when sensor values go out of range</Text>
            </View>
          ) : (
            <>
              <TouchableOpacity style={styles.clearBtn} onPress={clearHistory}>
                <Text style={styles.clearBtnText}>Clear History</Text>
              </TouchableOpacity>
              {history.map((item) => (
                <AlertItem key={item.id} item={item} />
              ))}
            </>
          )}
        </ScrollView>
      )}

      {tab === 'thresholds' && (
        <ScrollView contentContainerStyle={styles.scroll}>
          <Text style={styles.hint}>
            Notifications fire when a sensor goes outside these ranges.
            A 5-minute cooldown prevents repeated alerts.
          </Text>
          {Object.entries(thresholds).map(([sensor, threshold]) => (
            <ThresholdRow
              key={sensor}
              sensor={sensor}
              threshold={threshold}
              onChange={updateThreshold}
            />
          ))}
          <TouchableOpacity style={styles.resetBtn} onPress={resetThresholds}>
            <Text style={styles.resetBtnText}>Reset to Defaults</Text>
          </TouchableOpacity>
        </ScrollView>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#111111' },
  tabBar: {
    flexDirection: 'row',
    backgroundColor: '#1e1e1e',
    borderBottomWidth: 1,
    borderBottomColor: '#2a2a2a',
  },
  tabBtn: {
    flex: 1,
    paddingVertical: 12,
    alignItems: 'center',
  },
  tabBtnActive: {
    borderBottomWidth: 2,
    borderBottomColor: '#097479',
  },
  tabBtnText: { color: '#666', fontSize: 13, fontWeight: '600' },
  tabBtnTextActive: { color: '#097479' },
  scroll: { padding: 16, paddingBottom: 48 },
  empty: { alignItems: 'center', paddingTop: 80 },
  emptyIcon: { fontSize: 48, color: '#097479', marginBottom: 12 },
  emptyText: { color: '#fff', fontSize: 18, fontWeight: 'bold' },
  emptySubtext: { color: '#555', fontSize: 13, textAlign: 'center', marginTop: 6 },
  clearBtn: {
    alignSelf: 'flex-end',
    marginBottom: 12,
    padding: 6,
  },
  clearBtnText: { color: '#f44336', fontSize: 13 },
  alertItem: {
    backgroundColor: '#1e1e1e',
    borderRadius: 8,
    padding: 12,
    marginBottom: 8,
    borderLeftWidth: 3,
  },
  alertHeader: { flexDirection: 'row', alignItems: 'center', marginBottom: 4, gap: 6 },
  alertIcon: { fontSize: 14, fontWeight: 'bold' },
  alertTitle: { fontSize: 13, fontWeight: '600', flex: 1 },
  alertTime: { color: '#555', fontSize: 11 },
  alertBody: { color: '#888', fontSize: 12 },
  hint: {
    color: '#666',
    fontSize: 12,
    lineHeight: 18,
    marginBottom: 16,
    backgroundColor: '#1a1a1a',
    padding: 10,
    borderRadius: 8,
  },
  thresholdRow: {
    backgroundColor: '#1e1e1e',
    borderRadius: 10,
    padding: 14,
    marginBottom: 10,
    borderWidth: 1,
    borderColor: '#2a2a2a',
  },
  thresholdLabel: { color: '#fff', fontSize: 14, fontWeight: '600', marginBottom: 10 },
  thresholdInputs: { flexDirection: 'row', gap: 16 },
  thresholdField: { flex: 1, flexDirection: 'row', alignItems: 'center', gap: 6 },
  thresholdFieldLabel: { color: '#666', fontSize: 12, width: 28 },
  thresholdInput: {
    flex: 1,
    backgroundColor: '#111',
    color: '#fff',
    borderRadius: 6,
    padding: 8,
    fontSize: 14,
    borderWidth: 1,
    borderColor: '#333',
    textAlign: 'center',
  },
  thresholdUnit: { color: '#555', fontSize: 11, width: 28 },
  resetBtn: {
    borderWidth: 1,
    borderColor: '#555',
    borderRadius: 8,
    padding: 12,
    alignItems: 'center',
    marginTop: 8,
  },
  resetBtnText: { color: '#888', fontSize: 13 },
});
