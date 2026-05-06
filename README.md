# ⛏️ ESP32 Mining Cluster

A distributed mining system using multiple ESP32-S3 devices connected via I2C. One master device coordinates multiple worker devices to perform SHA256 hashing operations.

![Status](https://img.shields.io/badge/status-working-green)
![ESP32](https://img.shields.io/badge/chip-ESP32--S3-blue)
![Protocol](https://img.shields.io/badge/protocol-I2C-orange)

---

## 📋 Table of Contents

- [What This Does](#what-this-does)
- [Hardware Needed](#hardware-needed)
- [Wiring Diagram](#wiring-diagram)
- [Software Setup](#software-setup)
- [Building & Uploading](#building--uploading)
- [How It Works](#how-it-works)
- [LED Status Guide](#led-status-guide)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)

---

## 🔍 What This Does

This project creates a **mining cluster** where:

- **1 Master ESP32** - Coordinates the cluster, sends jobs, collects results
- **Multiple Worker ESP32s** (up to 8) - Perform actual hashing calculations
- **I2C Communication** - Fast, reliable connection between devices
- **RGB LED Status** - Visual feedback on each device's status
- **Serial Monitoring** - View stats on your computer

**Perfect for learning about:**
- Distributed computing
- I2C communication
- ESP32 multi-core programming
- SHA256 hashing
- Embedded systems

---

## 🛠️ Hardware Needed

### For Each Device (Master + Workers)

| Item | Quantity | Notes |
|------|----------|-------|
| ESP32-S3 DevKit | 1+ | Any ESP32-S3 board with WiFi |
| NeoPixel LED | 1 | WS2812B, connected to GPIO 48 |
| Micro USB Cable | 1 | For power and programming |
| Breadboard & Jumper Wires | - | For connections |

### For the Master Only

| Item | Quantity | Notes |
|------|----------|-------|
| TCA9548A I2C Mux | 1 | Allows 8 workers on same I2C bus |
| 10kΩ Pull-up Resistors | 2 | For I2C SDA/SCL lines |

### Optional

| Item | Quantity | Notes |
|------|----------|-------|
| 1000µF Capacitor | 1 | Across 5V/GND for power stability |
| External 5V Power Supply | 1 | If USB can't provide enough power |

---

## 🔌 Wiring Diagram

### Master Device

