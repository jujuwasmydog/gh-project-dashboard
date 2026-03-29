import React, { useEffect, useState, useRef } from 'react';
import { Animated, Text, StyleSheet, TouchableOpacity } from 'react-native';
import AlertService from '../services/AlertService';

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

export default function ToastBanner() {
  const [toast, setToast] = useState(null);
  const opacity = useRef(new Animated.Value(0)).current;
  const timer = useRef(null);

  useEffect(() => {
    const unsub = AlertService.onToast((t) => {
      setToast(t);
      if (timer.current) clearTimeout(timer.current);

      Animated.sequence([
        Animated.timing(opacity, { toValue: 1, duration: 300, useNativeDriver: true }),
        Animated.delay(4000),
        Animated.timing(opacity, { toValue: 0, duration: 400, useNativeDriver: true }),
      ]).start(() => setToast(null));
    });
    return unsub;
  }, [opacity]);

  if (!toast) return null;

  const color = TYPE_COLOR[toast.type] || '#888';
  const icon = TYPE_ICON[toast.type] || '•';

  return (
    <Animated.View style={[styles.toast, { opacity, borderLeftColor: color }]}>
      <Text style={[styles.title, { color }]}>{icon} {toast.title}</Text>
      <Text style={styles.body}>{toast.body}</Text>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  toast: {
    position: 'absolute',
    top: 60,
    left: 16,
    right: 16,
    backgroundColor: '#1e1e1e',
    borderRadius: 10,
    padding: 14,
    borderLeftWidth: 4,
    zIndex: 999,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.4,
    shadowRadius: 6,
    elevation: 8,
  },
  title: { fontWeight: 'bold', fontSize: 14, marginBottom: 3 },
  body:  { color: '#aaa', fontSize: 12 },
});
