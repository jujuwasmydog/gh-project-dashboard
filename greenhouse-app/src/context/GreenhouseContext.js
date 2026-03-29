import React, { createContext, useContext, useState, useEffect, useRef, useCallback } from 'react';
import AsyncStorage from '@react-native-async-storage/async-storage';
import AlertService from '../services/AlertService';

const GreenhouseContext = createContext();

const STORAGE_KEY = '@greenhouse_node_red_url';
const DEFAULT_URL = 'http://127.0.0.1:1880';
const POLL_INTERVAL = 3000; // ms

const DEFAULT_DATA = {
  temperature: null,
  humidity: null,
  lux: null,
  soil: null,
  louvre_pct: null,
  louvre_state: null,
  fan_pct: null,
  fault: null,
};

export function GreenhouseProvider({ children }) {
  const [nodeRedUrl, setNodeRedUrl] = useState(DEFAULT_URL);
  const [sensorData, setSensorData] = useState(DEFAULT_DATA);
  const [connected, setConnected] = useState(false);
  const [connectionMode, setConnectionMode] = useState('local');
  const [lastUpdated, setLastUpdated] = useState(null);
  const pollRef = useRef(null);

  // Load saved URL on mount
  useEffect(() => {
    AsyncStorage.getItem(STORAGE_KEY).then((saved) => {
      if (saved) setNodeRedUrl(saved);
    });
  }, []);

  // Poll sensors whenever URL changes
  useEffect(() => {
    if (pollRef.current) clearInterval(pollRef.current);
    setConnected(false);
    setSensorData(DEFAULT_DATA);

    const poll = async () => {
      try {
        const res = await fetch(`${nodeRedUrl}/api/sensors`, {
          method: 'GET',
          headers: { Accept: 'application/json' },
        });
        if (res.ok) {
          const data = await res.json();
          setConnected(true);
          setSensorData((prev) => {
            const updated = { ...prev, ...data };
            AlertService.checkSensorData(updated);
            return updated;
          });
          setLastUpdated(new Date());
        } else {
          setConnected(false);
        }
      } catch {
        setConnected(false);
      }
    };

    poll(); // immediate first poll
    pollRef.current = setInterval(poll, POLL_INTERVAL);

    return () => clearInterval(pollRef.current);
  }, [nodeRedUrl]);

  const saveUrl = useCallback(async (url) => {
    const trimmed = url.trim().replace(/\/$/, '');
    await AsyncStorage.setItem(STORAGE_KEY, trimmed);
    setNodeRedUrl(trimmed);
    if (trimmed.includes('100.')) setConnectionMode('tailscale');
    else if (trimmed.includes('127.0.0.1') || trimmed.includes('localhost')) setConnectionMode('local');
    else setConnectionMode('custom');
  }, []);

  const sendCommand = useCallback(async (endpoint, command) => {
    try {
      await fetch(`${nodeRedUrl}${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command }),
      });
    } catch (e) {
      console.warn('Command failed:', e.message);
    }
  }, [nodeRedUrl]);

  return (
    <GreenhouseContext.Provider value={{
      sensorData,
      connected,
      connectionMode,
      lastUpdated,
      nodeRedUrl,
      saveUrl,
      sendCommand,
    }}>
      {children}
    </GreenhouseContext.Provider>
  );
}

export function useGreenhouse() {
  return useContext(GreenhouseContext);
}
