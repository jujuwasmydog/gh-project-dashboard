import React, { useState, useEffect } from 'react';
import {
  View, Text, TextInput, StyleSheet,
  TouchableOpacity, ActivityIndicator, ScrollView, Alert,
} from 'react-native';
import { useGreenhouse } from '../context/GreenhouseContext';

const PRESETS = [
  {
    label: 'Local (this PC)',
    description: 'Node-RED running on Windows',
    url: 'http://127.0.0.1:1880',
    icon: '💻',
  },
  {
    label: 'Raspberry Pi — Local Network',
    description: 'Pi on same WiFi (edit IP)',
    url: 'http://192.168.1.100:1880',
    icon: '🍓',
  },
  {
    label: 'Tailscale — Remote',
    description: 'Off-network via Tailscale VPN',
    url: 'http://100.x.x.x:1880',
    icon: '🔒',
  },
];

function ProbeStatus({ status }) {
  if (!status) return null;
  const map = {
    probing: { color: '#ff9800', text: 'Probing...' },
    ok:      { color: '#4caf50', text: 'Reachable ✓' },
    fail:    { color: '#f44336', text: 'Unreachable ✗' },
  };
  const s = map[status] || map.fail;
  return <Text style={[styles.probeStatus, { color: s.color }]}>{s.text}</Text>;
}

export default function SettingsScreen() {
  const { nodeRedUrl, saveUrl, connected } = useGreenhouse();
  const [input, setInput] = useState(nodeRedUrl);
  const [probeStatus, setProbeStatus] = useState(null);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    setInput(nodeRedUrl);
  }, [nodeRedUrl]);

  const probe = async (url) => {
    setProbeStatus('probing');
    try {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 4000);
      const res = await fetch(`${url.trim().replace(/\/$/, '')}/`, { signal: controller.signal });
      clearTimeout(timeout);
      setProbeStatus(res.ok || res.status < 500 ? 'ok' : 'fail');
    } catch {
      setProbeStatus('fail');
    }
  };

  const apply = async () => {
    const trimmed = input.trim().replace(/\/$/, '');
    if (!trimmed.startsWith('http')) {
      Alert.alert('Invalid URL', 'URL must start with http:// or https://');
      return;
    }
    setSaving(true);
    await saveUrl(trimmed);
    setSaving(false);
    Alert.alert('Applied', 'Reconnecting to ' + trimmed);
    setProbeStatus(null);
  };

  return (
    <ScrollView style={styles.container} contentContainerStyle={styles.scroll}>
      <Text style={styles.sectionLabel}>Current Connection</Text>
      <View style={styles.currentBox}>
        <View style={[styles.statusDot, { backgroundColor: connected ? '#4caf50' : '#f44336' }]} />
        <Text style={styles.currentUrl}>{nodeRedUrl}</Text>
      </View>

      <Text style={styles.sectionLabel}>Node-RED URL</Text>
      <TextInput
        style={styles.input}
        value={input}
        onChangeText={(v) => { setInput(v); setProbeStatus(null); }}
        placeholder="http://192.168.1.x:1880"
        placeholderTextColor="#555"
        autoCapitalize="none"
        keyboardType="url"
        autoCorrect={false}
      />

      <View style={styles.inputActions}>
        <TouchableOpacity style={styles.probeBtn} onPress={() => probe(input)}>
          <Text style={styles.probeBtnText}>Test Connection</Text>
        </TouchableOpacity>
        <ProbeStatus status={probeStatus} />
      </View>

      <TouchableOpacity style={styles.applyBtn} onPress={apply} disabled={saving}>
        {saving
          ? <ActivityIndicator color="#fff" />
          : <Text style={styles.applyBtnText}>Apply & Reconnect</Text>}
      </TouchableOpacity>

      <Text style={styles.sectionLabel}>Presets</Text>
      {PRESETS.map((p) => (
        <TouchableOpacity
          key={p.label}
          style={[styles.preset, nodeRedUrl === p.url && styles.presetActive]}
          onPress={() => setInput(p.url)}
        >
          <Text style={styles.presetIcon}>{p.icon}</Text>
          <View style={styles.presetText}>
            <Text style={styles.presetLabel}>{p.label}</Text>
            <Text style={styles.presetDesc}>{p.description}</Text>
            <Text style={styles.presetUrl}>{p.url}</Text>
          </View>
          {nodeRedUrl === p.url && <Text style={styles.activeBadge}>ACTIVE</Text>}
        </TouchableOpacity>
      ))}

      <Text style={styles.sectionLabel}>Tailscale Setup</Text>
      <View style={styles.infoBox}>
        <Text style={styles.infoText}>
          To access the greenhouse remotely:{'\n\n'}
          1. Install Tailscale on the Raspberry Pi{'\n'}
          {'   '}curl -fsSL https://tailscale.com/install.sh | sh{'\n\n'}
          2. Install Tailscale on this device{'\n'}
          {'   '}tailscale.com/download{'\n\n'}
          3. Sign into the same Tailscale account{'\n\n'}
          4. Get the Pi's Tailscale IP from{'\n'}
          {'   '}login.tailscale.com/admin/machines{'\n\n'}
          5. Enter it as: http://100.x.x.x:1880
        </Text>
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#111111' },
  scroll: { padding: 16, paddingBottom: 48 },
  sectionLabel: {
    color: '#097479',
    fontSize: 11,
    fontWeight: '600',
    letterSpacing: 1,
    textTransform: 'uppercase',
    marginTop: 24,
    marginBottom: 8,
  },
  currentBox: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#1e1e1e',
    borderRadius: 8,
    padding: 12,
    gap: 10,
    borderWidth: 1,
    borderColor: '#2a2a2a',
  },
  statusDot: { width: 8, height: 8, borderRadius: 4 },
  currentUrl: { color: '#ccc', fontSize: 13 },
  input: {
    backgroundColor: '#1e1e1e',
    color: '#fff',
    borderRadius: 8,
    padding: 12,
    fontSize: 14,
    borderWidth: 1,
    borderColor: '#2a2a2a',
  },
  inputActions: {
    flexDirection: 'row',
    alignItems: 'center',
    marginTop: 8,
    gap: 12,
  },
  probeBtn: {
    borderWidth: 1,
    borderColor: '#097479',
    borderRadius: 6,
    paddingVertical: 6,
    paddingHorizontal: 12,
  },
  probeBtnText: { color: '#097479', fontSize: 13 },
  probeStatus: { fontSize: 13, fontWeight: '600' },
  applyBtn: {
    backgroundColor: '#097479',
    borderRadius: 8,
    padding: 14,
    alignItems: 'center',
    marginTop: 16,
  },
  applyBtnText: { color: '#fff', fontWeight: 'bold', fontSize: 15 },
  preset: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#1e1e1e',
    borderRadius: 10,
    padding: 12,
    marginBottom: 8,
    borderWidth: 1,
    borderColor: '#2a2a2a',
    gap: 12,
  },
  presetActive: { borderColor: '#097479' },
  presetIcon: { fontSize: 22 },
  presetText: { flex: 1 },
  presetLabel: { color: '#fff', fontSize: 13, fontWeight: '600' },
  presetDesc: { color: '#666', fontSize: 11, marginTop: 1 },
  presetUrl: { color: '#097479', fontSize: 11, marginTop: 2 },
  activeBadge: {
    color: '#097479',
    fontSize: 10,
    fontWeight: 'bold',
    borderWidth: 1,
    borderColor: '#097479',
    borderRadius: 4,
    paddingHorizontal: 4,
    paddingVertical: 2,
  },
  infoBox: {
    backgroundColor: '#1a1a2e',
    borderRadius: 10,
    padding: 14,
    borderWidth: 1,
    borderColor: '#2a2a4a',
  },
  infoText: { color: '#888', fontSize: 12, lineHeight: 20 },
});
