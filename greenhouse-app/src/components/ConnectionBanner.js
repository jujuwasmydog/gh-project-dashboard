import React from 'react';
import { View, Text, StyleSheet } from 'react-native';
import { useGreenhouse } from '../context/GreenhouseContext';

const MODE_LABEL = {
  local:     { label: 'Local',     color: '#4caf50' },
  tailscale: { label: 'Tailscale', color: '#2196f3' },
  custom:    { label: 'Custom',    color: '#ff9800' },
};

export default function ConnectionBanner() {
  const { connected, lastUpdated, connectionMode, nodeRedUrl } = useGreenhouse();
  const mode = MODE_LABEL[connectionMode] || MODE_LABEL.custom;

  return (
    <View style={[styles.banner, connected ? styles.connected : styles.disconnected]}>
      <View style={[styles.dot, { backgroundColor: connected ? mode.color : '#666' }]} />
      <View style={styles.textBlock}>
        <Text style={styles.status}>
          {connected
            ? `${mode.label} · ${nodeRedUrl}`
            : `Disconnected · ${nodeRedUrl}`}
        </Text>
        {connected && lastUpdated && (
          <Text style={styles.time}>Last update: {lastUpdated.toLocaleTimeString()}</Text>
        )}
        {!connected && (
          <Text style={styles.time}>Check Node-RED is running, then go to Settings to change URL</Text>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  banner: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 12,
    paddingVertical: 8,
    gap: 10,
  },
  connected: { backgroundColor: '#0d2e2e' },
  disconnected: { backgroundColor: '#2a1a1a' },
  dot: {
    width: 8,
    height: 8,
    borderRadius: 4,
  },
  textBlock: { flex: 1 },
  status: { color: '#ccc', fontSize: 11 },
  time: { color: '#666', fontSize: 10, marginTop: 1 },
});
