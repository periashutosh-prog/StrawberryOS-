# StrawberryOS


An open-source embedded operating system built on ESP32, designed to be hackable, extensible, and free for everyone.

---

## 🍓 What is StrawberryOS?

StrawberryOS is a full embedded OS built from scratch for the Seeed XIAO ESP32-C6, currently targeting smartwatch hardware. It is part of the broader **Strawberry ecosystem** — an open-source platform for embedded devices.

This is not a commercial product. It is a platform built so that hobbyists, engineers, and makers are never bottlenecked by budget, time, or closed ecosystems. The core is designed to be portable — anyone can fork StrawberryOS, modify the app layer, and deploy it on their own hardware.

---

## ⚙️ Hardware Reference Design

| Component | Details |
|---|---|
| MCU | Seeed XIAO ESP32-C6 (RISC-V 160MHz, WiFi 6, BLE 5.3) |
| Display | 0.96" SSD1306 OLED (128x64, I2C) |
| RTC | DS3231 |
| Battery | 1500mAh LiPo |
| Input | 5x tactile buttons (UP, DOWN, LEFT, RIGHT, CENTER) |
| PCB | Custom 6-layer ENIG |
| Enclosure | 52x32mm ASA |

> Development board: ESP32-C3 SuperMini (pin-compatible for prototyping)

---

## ✨ Features

### Currently Implemented
- Watch face with real-time clock via DS3231 RTC
- Date and day of week display
- Stopwatch with millisecond precision (MM:SS:ms)
- Countdown timer with alert
- Radio app with WiFi, BLE, and ESP-NOW stubs
- WiFi network scanning and credential management (up to 5 networks)
- NTP time synchronization over WiFi
- Display timeout and basic power management
- Smooth slide transition animations between screens
- On-screen keyboard (lowercase, uppercase, symbol modes)
- FreeRTOS-based button input task

### Planned
- BLE phone notifications
- ESP-NOW multiplayer with named device discovery
- Groq AI assistant via WiFi
- Weather application
- WhatsUp P2P messaging integration
- Heap watchdog with BSOD error screen
- Daily silent reboot for memory maintenance
- LittleFS-based chat history
- Light sleep eco mode (target: 31 days battery life)

---

StrawberryOS is designed to be portable. The core is hardware-agnostic. Developers can build their own app layer for their own target device.

---

## 🔋 Power Modes

| Mode | Description | Estimated Battery Life |
|---|---|---|
| Normal | All radios active, full display brightness | ~1.5 days |
| Eco | Display off, light sleep, wake on button press | ~31 days |

---

## 🚀 Getting Started

### Requirements
- Arduino IDE, PlatformIO, or AntiGravity IDE
- ESP32 Arduino Core
- Libraries:
  - `Adafruit SSD1306`
  - `Adafruit GFX`
  - `RTClib`
  - `ArduinoJson`
  - `LittleFS`

### Flashing
1. Clone this repository
2. Open `Smartwatch.ino` in your IDE
3. Select board: `Seeed XIAO ESP32-C6` (or `ESP32-C3 SuperMini` for development)
4. Upload

---

## 📁 File Structure

```
StrawberryOS/
├── Smartwatch.ino        # Main OS source
├── README.md
└── PCB/                  # EasyEDA files (coming soon)
```

---

## 🤝 Contributing

StrawberryOS is open to all contributors.

- Found a bug? Open an issue.
- Want a feature? Fork the repository and submit a pull request.
- Porting to a different device? Document your changes and share them with the community.

No minimum experience required. This project exists so that no developer has to start from scratch.

---

## 📜 License

MIT License — You are free to use, modify, and distribute this project. Keeping derivatives open source is encouraged.

---

## 👤 Author

**Ashutosh** — Student, maker, and an average guy who wants to build complex stuff on his own...
