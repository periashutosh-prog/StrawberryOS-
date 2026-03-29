# StrawberryOS

StrawberryOS is an open-source embedded operating system built for ESP32-based platforms. It is designed to be modular, portable, and extensible across a wide range of embedded applications. While initially developed for smartwatch hardware, the system architecture allows adaptation to other devices.

---

## Overview

StrawberryOS is developed for the Seeed XIAO ESP32-C6 and targets resource-constrained embedded systems. It is part of the broader Strawberry ecosystem, which focuses on open and adaptable embedded platforms.

This project is intended as a foundation for development and experimentation. The system is designed to be hardware-agnostic at its core, enabling developers to modify and extend functionality according to their requirements.

---

## Hardware Reference Design

| Component       | Description                  |
| --------------- | ---------------------------- |
| Microcontroller | Seeed XIAO ESP32-C6          |
| Display         | SSD1306 OLED (128×64, I2C)   |
| Real-Time Clock | DS3231                       |
| Battery         | Lithium Polymer (LiPo)       |
| Input           | Tactile button interface     |
| PCB             | Custom multi-layer design    |
| Enclosure       | Compact wearable form factor |

---

## Features

### Implemented

* Real-time clock integration
* Date and weekday display
* Stopwatch functionality
* Countdown timer
* Wireless communication framework (Wi-Fi, BLE, ESP-NOW)
* Network-based time synchronization
* Power management and display control
* Graphical user interface with transitions
* On-screen input system
* Task-based input handling

### Planned

* Wireless notification support
* Device-to-device communication features
* External service integration
* Environmental data applications
* Persistent data storage
* System monitoring and fault handling
* Low-power optimization modes

---

## System Design

StrawberryOS is structured to separate core system functionality from application-level components. This separation enables portability and simplifies adaptation to different hardware platforms.

The system leverages a task-based architecture suitable for real-time embedded environments and is designed to operate within the constraints of low-power devices.

---

## Power Modes

| Mode   | Description                                      |
| ------ | ------------------------------------------------ |
| Normal | Full system operation with all subsystems active |
| Eco    | Reduced power mode with limited functionality    |

---

## Project Structure

```id="static-structure"
StrawberryOS/
├── StrawberryOS.ino        # Main source file
├── README.md
├── LICENSE                 # GNU General Public License v3
└── PCB/                    # Hardware design files
```

---

## Contributing

Contributions are welcome. Improvements, modifications, and hardware adaptations may be submitted through standard version control workflows. Contributors are encouraged to document any significant changes.

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

Redistribution and modification are permitted under the terms of this license. Any derivative work must also be distributed under the same license, and corresponding source code must be made available.

Refer to the `LICENSE` file for complete details.

---

## Author

**Ashutosh**
A Student and an ambitious Maker
