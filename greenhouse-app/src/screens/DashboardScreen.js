import React from 'react';
import { View, Text, ScrollView, StyleSheet, RefreshControl } from 'react-native';
import { useGreenhouse } from '../context/GreenhouseContext';
import SensorCard from '../components/SensorCard';
import ConnectionBanner from '../components/ConnectionBanner';

export default function DashboardScreen() {
  const { sensorData, connected } = useGreenhouse();
  const [refreshing, setRefreshing] = React.useState(false);

  const onRefresh = () => {
    setRefreshing(true);
    setTimeout(() => setRefreshing(false), 1000);
  };

  const isFault = sensorData.fault && sensorData.fault !== '0' && sensorData.fault !== 'OK';

  return (
    <View style={styles.container}>
      <ConnectionBanner />

      {isFault && (
        <View style={styles.faultBanner}>
          <Text style={styles.faultText}>⚠ Fault Detected: {sensorData.fault}</Text>
        </View>
      )}

      <ScrollView
        contentContainerStyle={styles.scroll}
        refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} tintColor="#097479" />}
      >
        <Text style={styles.sectionTitle}>Environment</Text>
        <View style={styles.row}>
          <SensorCard label="Temperature" value={sensorData.temperature} unit="°C" icon="🌡️" color="#ff9800" />
          <SensorCard label="Humidity" value={sensorData.humidity} unit="%" icon="💧" color="#2196f3" />
        </View>
        <View style={styles.row}>
          <SensorCard label="Light" value={sensorData.lux} unit="lux" icon="☀️" color="#ffeb3b" />
          <SensorCard label="Soil Moisture" value={sensorData.soil} unit="%" icon="🌱" color="#4caf50" />
        </View>

        <Text style={styles.sectionTitle}>Actuators</Text>
        <View style={styles.row}>
          <SensorCard label="Louvre Open" value={sensorData.louvre_pct} unit="%" icon="🪟" color="#097479" />
          <SensorCard label="Louvre State" value={sensorData.louvre_state} unit="" icon="⚙️" color="#097479" />
        </View>
        <View style={styles.row}>
          <SensorCard label="Fan Speed" value={sensorData.fan_pct} unit="%" icon="🌀" color="#9c27b0" />
        </View>
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
    padding: 8,
    paddingBottom: 32,
  },
  sectionTitle: {
    color: '#097479',
    fontSize: 13,
    fontWeight: '600',
    letterSpacing: 1,
    textTransform: 'uppercase',
    marginLeft: 8,
    marginTop: 16,
    marginBottom: 4,
  },
  row: {
    flexDirection: 'row',
    flexWrap: 'wrap',
  },
  faultBanner: {
    backgroundColor: '#5c1a1a',
    padding: 10,
    alignItems: 'center',
  },
  faultText: {
    color: '#ff6b6b',
    fontWeight: 'bold',
    fontSize: 13,
  },
});
