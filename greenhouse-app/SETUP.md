# Greenhouse App — Setup Guide

## Prerequisites

- Node.js v18+ — https://nodejs.org
- Git — https://git-scm.com

---

## iOS Setup (Expo Go)

No build tools required. Uses the Expo Go app on your iPhone.

### Steps

1. Install Expo Go on your iPhone from the App Store
   https://apps.apple.com/app/expo-go/id982107779

2. Install dependencies
   ```
   cd greenhouse-app
   npm install
   ```

3. Start the development server
   ```
   npx expo start
   ```

4. Scan the QR code shown in the terminal using the Expo Go app

### Requirements
- iPhone and PC must be on the same WiFi network
- Expo Go must be used — the default iOS camera app will not work

---

## Android Setup (Emulator via Android Studio)

### Step 1 — Install Android Studio
Download from https://developer.android.com/studio and install with default settings.

### Step 2 — Set environment variables
Open PowerShell and run:

```powershell
$current = [System.Environment]::GetEnvironmentVariable("Path", "User")
[System.Environment]::SetEnvironmentVariable("ANDROID_HOME", "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk", "User")
[System.Environment]::SetEnvironmentVariable("JAVA_HOME", "C:\Program Files\Android\Android Studio\jbr", "User")
[System.Environment]::SetEnvironmentVariable("Path", "$current;C:\Users\$env:USERNAME\AppData\Local\Android\Sdk\platform-tools;C:\Users\$env:USERNAME\AppData\Local\Android\Sdk\emulator;C:\Program Files\Android\Android Studio\jbr\bin", "User")
```

Close and reopen PowerShell, then verify:
```powershell
adb version
java -version
```

### Step 3 — Create an Android Virtual Device (AVD)
1. Open Android Studio
2. Go to Tools → Device Manager
3. Click Create Device
4. Select Pixel 8 → Next
5. Select API 35 (Android 15) — download if prompted → Next
6. Click Finish
7. Click the ▶ play button to start the emulator

### Step 4 — Run the app
Once the emulator is booted and showing the Android home screen:

```powershell
cd greenhouse-app
npm install
npx expo start --android
```

The app will install and launch on the emulator automatically.

### Note on localhost
When connecting to Node-RED from the Android emulator, use:
```
http://10.0.2.2:1880
```
instead of `http://127.0.0.1:1880` — the emulator uses `10.0.2.2` to refer to your PC.

---

## Android Setup (Physical Device)

1. On your Android phone go to Settings → About Phone → tap Build Number 7 times to enable Developer Options
2. Go to Settings → Developer Options → enable USB Debugging
3. Connect phone to PC via USB
4. Run `adb devices` — your device should appear
5. Run `npx expo start --android`

---

## Connecting to Node-RED

| Environment       | Node-RED URL                  |
|-------------------|-------------------------------|
| Local PC          | http://127.0.0.1:1880         |
| Android emulator  | http://10.0.2.2:1880          |
| Raspberry Pi (LAN)| http://192.168.1.x:1880       |
| Tailscale (remote)| http://100.x.x.x:1880         |

Update the URL in the app via the **Settings** tab → enter URL → Test Connection → Apply.

### Starting Node-RED locally
```powershell
node-red
```
Then open http://127.0.0.1:1880 in your browser to access the dashboard editor.

---

## Known Issues

- Push notifications require a development build (`eas build`) and are not supported in Expo Go SDK 53+
- `enableScreens(false)` is set in App.js to fix a boolean type mismatch in react-native-screens v4
  with React Native 0.81 new architecture on iOS
- The Android emulator cannot use `127.0.0.1` — always use `10.0.2.2` instead
