import React from 'react';
import { View, Text, StyleSheet } from 'react-native';

export default function SensorCard({ label, value, unit, icon, color = '#097479', warning = false }) {
  const displayValue = value !== null && value !== undefined ? String(value) : '--';

  return (
    <View style={[styles.card, warning && styles.cardWarning]}>
      <Text style={styles.icon}>{icon}</Text>
      <Text style={[styles.value, { color: warning ? '#ff6b6b' : color }]}>
        {displayValue}
        {value !== null && <Text style={styles.unit}> {unit}</Text>}
      </Text>
      <Text style={styles.label}>{label}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  card: {
    backgroundColor: '#1e1e1e',
    borderRadius: 12,
    padding: 16,
    margin: 8,
    flex: 1,
    alignItems: 'center',
    minWidth: 140,
    borderWidth: 1,
    borderColor: '#2a2a2a',
  },
  cardWarning: {
    borderColor: '#ff6b6b',
    backgroundColor: '#2a1a1a',
  },
  icon: {
    fontSize: 28,
    marginBottom: 8,
  },
  value: {
    fontSize: 28,
    fontWeight: 'bold',
  },
  unit: {
    fontSize: 14,
    fontWeight: 'normal',
    color: '#aaa',
  },
  label: {
    fontSize: 12,
    color: '#888',
    marginTop: 4,
    textAlign: 'center',
  },
});
