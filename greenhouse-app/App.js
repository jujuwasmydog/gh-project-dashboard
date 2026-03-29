import React from 'react';
import { View, Text } from 'react-native';
import { NavigationContainer } from '@react-navigation/native';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { StatusBar } from 'expo-status-bar';
import { enableScreens } from 'react-native-screens';

import { GreenhouseProvider } from './src/context/GreenhouseContext';
import DashboardScreen from './src/screens/DashboardScreen';
import ControlScreen from './src/screens/ControlScreen';
import AlertsScreen from './src/screens/AlertsScreen';
import SettingsScreen from './src/screens/SettingsScreen';
import ToastBanner from './src/components/ToastBanner';

enableScreens(false);

const Tab = createBottomTabNavigator();

export default function App() {
  return (
    <GreenhouseProvider>
      <View style={{ flex: 1 }}>
        <NavigationContainer>
          <StatusBar style="light" />
          <Tab.Navigator
            screenOptions={{
              headerStyle: { backgroundColor: '#097479' },
              headerTintColor: '#fff',
              headerTitleStyle: { fontWeight: 'bold' },
              tabBarStyle: { backgroundColor: '#1e1e1e', borderTopColor: '#2a2a2a' },
              tabBarActiveTintColor: '#097479',
              tabBarInactiveTintColor: '#666',
            }}
          >
            <Tab.Screen
              name="Dashboard"
              component={DashboardScreen}
              options={{
                title: 'Greenhouse',
                tabBarLabel: 'Dashboard',
                tabBarIcon: () => <Text style={{ fontSize: 20 }}>🌿</Text>,
              }}
            />
            <Tab.Screen
              name="Control"
              component={ControlScreen}
              options={{
                title: 'System Control',
                tabBarLabel: 'Control',
                tabBarIcon: () => <Text style={{ fontSize: 20 }}>⚙️</Text>,
              }}
            />
            <Tab.Screen
              name="Alerts"
              component={AlertsScreen}
              options={{
                title: 'Alerts',
                tabBarLabel: 'Alerts',
                tabBarIcon: () => <Text style={{ fontSize: 20 }}>🔔</Text>,
              }}
            />
            <Tab.Screen
              name="Settings"
              component={SettingsScreen}
              options={{
                title: 'Settings',
                tabBarLabel: 'Settings',
                tabBarIcon: () => <Text style={{ fontSize: 20 }}>🔧</Text>,
              }}
            />
          </Tab.Navigator>
        </NavigationContainer>
        <ToastBanner />
      </View>
    </GreenhouseProvider>
  );
}
