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
| 4.7kΩ Pull-up Resistors | 18(1 resistor on each I2C line, for master AND workers) | For I2C SDA/SCL lines |

### Optional

| Item | Quantity | Notes |
|------|----------|-------|
| 500µF Capacitor | 1 for each ESP32 | Across 5V/GND for power stability |
| External 5V Power Supply | 1 | If USB can't provide enough power | THIS WILL PULL 300-500mah EACH!!
***| 0.10uF Ceramic Filter Capacitor | 2 for each ESP32 | Between the SDA and SCL lines and ESP32's | These help stabilize the signal |***Testing PHASE***

---

## 🔌 Wiring Diagram

### Master Device

ESP32-S3 Master TCA9548A Mux ────────────── GPIO 8 (SDA) ──────► SDA GPIO 9 (SCL) ──────► SCL GPIO 7 (RST) ──────► RST 5V ──────► VCC GND ──────► GND GPIO 48 ──────► NeoPixel DIN


### Worker Devices (Each)

ESP32-S3 Worker #0 TCA9548A Channel 0 ESP32-S3 Worker #1 TCA9548A Channel 1 ESP32-S3 Worker #2 TCA9548A Channel 2 ... ... ESP32-S3 Worker #7 TCA9548A Channel 7

All workers: SDA ──────► I2C SDA (via mux channel) SCL ──────► I2C SCL (via mux channel) GND ──────► Common GND 5V ──────► Power GPIO 48 ──► NeoPixel DIN


### I2C Addresses

| Device | Address | Notes |
|--------|---------|-------|
| TCA9548A Mux | 0x70 | Default (A0-A2 grounded) |
| Worker #0 | 0x20 | Fixed address |
| Worker #1 | 0x20 | Same address (isolated by mux) |
| Worker #2 | 0x20 | Same address (isolated by mux) |

---

## 💻 Software Setup

### Step 1: Install PlatformIO

1. **Install VS Code**: https://code.visualstudio.com/
2. **Install PlatformIO Extension**:
   - Open VS Code
   - Go to Extensions (Ctrl+Shift+X)
   - Search "PlatformIO IDE"
   - Click Install

### Step 2: Clone/Download This Project

git clone <https://github.com/drone1337llc-lgtm/ESPI2Cluster.git>
cd ESPI2Cluster
Or download and extract the ZIP file.

Step 3: Install Libraries
PlatformIO will auto-install these on first build:

adafruit/Adafruit NeoPixel@^1.15.4
WifWaf/TCA9548A@^1.1.3
Step 4: Configure Your Project
Open config.h and verify settings:

// I2C Pins (Master only)
#define I2C_MASTER_SDA 8
#define I2C_MASTER_SCL 9

// LED Pin (All devices)
#define LED_PIN 48

// Number of workers (Master only)
#define MAX_WORKERS 8


🔨 Building & Uploading
For Master Device
Connect Master ESP32 via USB
Open PlatformIO in VS Code
Select Environment: master (bottom toolbar)
Upload: Click the → arrow (Upload button)
Open Monitor: Click the plug icon (Serial Monitor)
For Worker Devices
Connect Worker ESP32 via USB
Select Environment: worker (bottom toolbar)
Upload: Click the → arrow
Repeat for each worker
Command Line Alternative

# Build and upload master
platformio run -e master --target upload --upload-port COM3

# Build and upload worker
platformio run -e worker --target upload --upload-port COM3

# Open serial monitor
platformio device monitor -p COM3 -b 115200

⚙️ How It Works
System Flow

┌─────────────┐
│   MASTER    │
│             │
│ 1. Scans    │
│    workers  │──────────────┐
│ 2. Sends    │              │
│    jobs     │              ▼
│ 3. Collects │        ┌───────────┐
│    results  │        │  TCA9548A │
│             │        │    MUX    │
└─────────────┘        └─────┬─────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
         ┌─────────┐   ┌─────────┐   ┌─────────┐
         │ WORKER  │   │ WORKER  │   │ WORKER  │
         │   #0    │   │   #1    │   │   #2    │
         │         │   │         │   │         │
         │ Mining  │   │ Mining  │   │ Mining  │
         │ Core 0  │   │ Core 0  │   │ Core 0  │
         │ Mining  │   │ Mining  │   │ Mining  │
         │ Core 1  │   │ Core 1  │   │ Core 1  │
         └─────────┘   └─────────┘   └─────────┘

Communication Protocol
Packet     Type	Direction	            Purpose
HELLO	Worker → Master	Worker         announces presence
ASSIGN_ID	Master → Worker	Master     assigns worker ID
JOB	Master → Worker	                Send mining job with block header
RESULT	Worker → Master	            Submit hash count and found nonces


Mining Process
Master generates random block header
Master sends JOB to all active workers via I2C mux
Workers split nonce range between Core 0 (HW SHA) and Core 1 (SW SHA)
Workers mine continuously, reporting every 2 seconds
Master collects results, tracks accepted shares
Repeat every 5 seconds with new job

💡 LED Status Guide
All Devices
LED Pattern	Color	Meaning
Fading	🟢 Green	Startup/Boot sequence
Breathing	🔵 Blue	Connecting/Searching for master
Solid	🔵 Blue	Registered with master
Solid	🟢 Green	Mining actively
Breathing	🔵 Blue	Mining idle (no job)
Fast Flash	🟢 Green	Share found/submitted
Breathing	🟢 Green	Share accepted
Fast Flash	🔴 Red	Share rejected
Fast Blink	🔴 Red	Error (check connections)
Slow Blink	🔵 Blue	No job received
Slow Blink	🔴 Red	Connection lost/timeout
OFF	⚫ Black	Device off/disabled

🐛 Troubleshooting
Master Won't Detect Workers
Symptom	Solution
"No workers found"	Check I2C wiring (SDA/SCL)
Verify TCA9548A is powered (5V)
Check RST pin connected to GPIO 7
Run I2C scanner to verify mux address
Workers timeout after job	Workers not reporting - check worker code
Increase HEARTBEAT_TIMEOUT_MS in config.h
Verify workers send results every 2 seconds
Workers Won't Connect
Symptom	Solution
LED stays blue (breathing)	Worker can't find master
Check I2C connection through mux
Verify worker I2C address (0x20)
Check worker is on correct mux channel
LED turns red	Max hello retries exceeded
Check wiring
Restart master first, then workers
I2C Communication Issues
Symptom	Solution
Random timeouts	Add 10kΩ pull-up resistors on SDA/SCL
Add 1000µF capacitor for power stability
Reduce I2C speed in config.h
Only some workers work	Check mux channel wiring
Test each channel individually
Verify all workers have unique physical connections
Build/Upload Errors
Error	Solution
"Library not found"	Run platformio lib install
"Port not found"	Check USB cable, install CP210x drivers
"Brownout detector"	Use better power supply, add capacitor
"Stack overflow"	Reduce MINING_STACK_SIZE in config.h
Serial Monitor Shows Garbage
Symptom	Solution
Random characters	Set baud rate to 115200
Check USB cable quality
Nothing appears	Enable DEBUG_ENABLED in config.h
Try different USB port
❓ FAQ
Q: How many workers can I connect?
A: Up to 8 workers using the TCA9548A mux. Each worker connects to one mux channel.

Q: Can I use ESP32 instead of ESP32-S3?
A: Yes, but you'll need to:

Change board in platformio.ini
Remove HARDWARE_SHA256 flag (no HW SHA on regular ESP32)
Adjust pin numbers as needed

Q: What is the actual hashrate?
A: Approximately:

Per worker: 50-100 GH/s (depends on difficulty)
8 workers: 70 GH/s total

Q: Can I mine actual Bitcoin?
A: No. This is for educational purposes. Bitcoin mining requires ASIC hardware. This demonstrates distributed computing concepts.

Q: Why I2C and not WiFi?
A: I2C is:

More reliable for local communication
Lower latency
No network configuration needed
Workers don't need WiFi credentials
Q: Can I add more features?
A: Yes! Popular additions:

OLED display for stats
Web interface (WiFi on master)
MQTT integration
Share difficulty adjustment
Worker hashrate balancing
📁 Project Structure

ESPI2Cluster/
├── src/
│   ├── common/              # Shared code
│   │   ├── led_manager.cpp  # RGB LED control
│   │   ├── led_manager.h
│   │   ├── protocol.cpp     # Packet definitions
│   │   ├── protocol.h
│   │   ├── sha256_optimized.cpp
│   │   └── sha256_optimized.h
│   ├── master/              # Master device code
│   │   ├── master_main.cpp
│   │   ├── i2c_mux.cpp
│   │   └── i2c_mux.h
│   ├── worker/              # Worker device code
│   │   └── worker_main.cpp
│   ├── config.h             # Configuration
│   └── main.cpp             # Entry point
├── platformio.ini           # Build configuration
└── README.md                # This file


📄 License
This project is GPL - 3.0 - or -later. Author : Sergio WIlliams 

🙏 Credits
SHA256 optimization inspired by nerdSHA256plus
I2C mux library by WifWaf
NeoPixel library by Adafruit
📞 Support
If you have issues:

Check the Troubleshooting section
Review wiring diagrams carefully
Test with one worker first
Check Serial Monitor output for errors
Good luck and happy mining! ⛏️🚀


