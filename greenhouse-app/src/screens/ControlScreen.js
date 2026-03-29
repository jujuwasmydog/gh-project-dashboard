import React, { useState } from 'react';
import {
  View, Text, StyleSheet, TouchableOpacity,
  ScrollView, ActivityIndicator, Alert,
} from 'react-native';
import { useGreenhouse } from '../context/GreenhouseContext';
import ConnectionBanner from '../components/ConnectionBanner';

const LOUVRE_COMMANDS = [
  { label: 'Open',  value: 'OPEN',  color: '#009933' },
  { label: 'Half',  value: 'HALF',  color: '#888888' },
  { label: 'Close', value: 'CLOSE', color: '#cc3333' },
  { label: 'Stop',  value: 'STOP',  color: '#097479' },
  { label: 'Auto',  value: 'AUTO',  color: '#cccc00' },
];

const FAN_COMMANDS = [
  { label: 'On 50%',  value: 'FAN_50',   color: '#009933' },
  { label: 'On 100%', value: 'FAN_100',  color: '#ff9800' },
  { label: 'Off',     value: 'FAN_OFF',  color: '#cc3333' },
  { label: 'Auto',    value: 'FAN_AUTO', color: '#097479' },
];

const LIGHT_COMMANDS = [
  { label: 'On',   value: 'LIGHT_ON',   color: '#ffeb3b' },
  { label: 'Off',  value: 'LIGHT_OFF',  color: '#888888' },
  { label: 'Auto', value: 'LIGHT_AUTO', color: '#097479' },
];

function ControlGroup({ title, icon, commands, endpoint, nodeRedUrl, activeCmd, setActiveCmd }) {
  const [sending, setSending] = useState(false);

  const sendCommand = async (cmd) => {
    setSending(true);
    try {
      const res = await fetch(`${nodeRedUrl}${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command: cmd.value }),
      });
      if (res.ok) {
        setActiveCmd(cmd.value);
      } else {
        Alert.alert('Error', `Command failed: ${res.status}`);
      }
    } catch {
      Alert.alert('Connection Error', 'Could not reach Node-RED. Is it running?');
    } finally {
      setSending(false);
    }
  };

  return (
    <View style={styles.group}>
      <View style={styles.groupHeader}>
        <Text style={styles.groupIcon}>{icon}</Text>
        <Text style={styles.groupTitle}>{title}</Text>
        {sending && <ActivityIndicator size="small" color="#097479" style={{ marginLeft: 8 }} />}
      </View>
      <View style={styles.btnRow}>
        {commands.map((cmd) => (
          <TouchableOpacity
            key={cmd.value}
            style={[
              styles.btn,
              { borderColor: cmd.color },
              activeCmd === cmd.value && { backgroundColor: cmd.color },
            ]}
            onPress={() => sendCommand(cmd)}
            disabled={sending}
          >
            <Text style={[
              styles.btnText,
              activeCmd === cmd.value && styles.btnTextActive,
            ]}>
              {cmd.label}
            </Text>
          </TouchableOpacity>
        ))}
      </View>
    </View>
  );
}

export default function ControlScreen() {
  const { connected, nodeRedUrl } = useGreenhouse();
  const [activeLouvre, setActiveLouvre] = useState(null);
  const [activeFan, setActiveFan] = useState(null);
  const [activeLight, setActiveLight] = useState(null);

  return (
    <View style={styles.container}>
      <ConnectionBanner />
      {!connected && (
        <View style={styles.offlineBanner}>
          <Text style={styles.offlineText}>⚠ Not connected — commands may not reach the greenhouse</Text>
        </View>
      )}
      <ScrollView contentContainerStyle={styles.scroll}>
        <ControlGroup
          title="Louvre / Vents"
          icon="🪟"
          commands={LOUVRE_COMMANDS}
          endpoint="/api/louvre"
          nodeRedUrl={nodeRedUrl}
          activeCmd={activeLouvre}
          setActiveCmd={setActiveLouvre}
        />
        <ControlGroup
          title="Fan"
          icon="🌀"
          commands={FAN_COMMANDS}
          endpoint="/api/fan"
          nodeRedUrl={nodeRedUrl}
          activeCmd={activeFan}
          setActiveCmd={setActiveFan}
        />
        <ControlGroup
          title="Grow Light"
          icon="💡"
          commands={LIGHT_COMMANDS}
          endpoint="/api/light"
          nodeRedUrl={nodeRedUrl}
          activeCmd={activeLight}
          setActiveCmd={setActiveLight}
        />
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#111111',
  },
  scroll: {
    padding: 16,
    paddingBottom: 40,
  },
  offlineBanner: {
    backgroundColor: '#3d2a00',
    padding: 8,
    alignItems: 'center',
  },
  offlineText: {
    color: '#ffb300',
    fontSize: 12,
  },
  group: {
    backgroundColor: '#1e1e1e',
    borderRadius: 12,
    padding: 16,
    marginBottom: 16,
    borderWidth: 1,
    borderColor: '#2a2a2a',
  },
  groupHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    marginBottom: 14,
  },
  groupIcon: {
    fontSize: 22,
    marginRight: 8,
  },
  groupTitle: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  btnRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
  },
  btn: {
    borderWidth: 1.5,
    borderRadius: 8,
    paddingVertical: 10,
    paddingHorizontal: 16,
    backgroundColor: 'transparent',
  },
  btnText: {
    color: '#ccc',
    fontSize: 13,
    fontWeight: '500',
  },
  btnTextActive: {
    color: '#111',
    fontWeight: 'bold',
  },
});
