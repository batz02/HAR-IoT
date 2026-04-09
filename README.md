# Context-Aware Human Activity Recognition for Wearable IoT: Balancing Edge Inference and Fog Offloading

## Overview
Human Activity Recognition (HAR) using wearable devices faces two major challenges: adapting to physiological variability among users (**Subject Overfitting**) and operating under strict hardware and network constraints. 

This project tackles these issues by simulating a *Smartwatch-only* scenario (PAMAP2 Dataset) using a distributed **Edge-Fog Computing** architecture. Instead of relying purely on a Cloud/Fog server, the system utilizes a smart Edge Gateway that dynamically balances local inference and remote processing.

### Core Highlights
* **Smart Offloading & Graceful Degradation:** The ESP32 Gateway intelligently offloads complex tasks to a Docker Fog node via MQTT. If the network drops, it seamlessly falls back to a local, bare-metal Random Forest model (C++) with zero downtime.
* **Defeating Subject Overfitting:** Experimental results prove that the lightweight Edge model actually **outperforms** the heavy Fog model (75.39% vs. 70.07% accuracy) on new users, as constrained capacity forces the model to learn universal motor patterns rather than individual idiosyncrasies.
* **Context-Awareness:** Incorporates *Thermal Signatures* and *Macro-Fatigue Drift* analysis to demonstrate how human movement mechanics degrade over time.
* **Fault Tolerance:** Built for real-world unreliability. Using ESP-NOW and Forward Fill logic, the system maintains high accuracy even with a **50% Packet Drop Rate**.

## System Architecture

The infrastructure is engineered across three physical and logical layers:

1. **Sensing Layer (ESP8266 Wearable Node)**
   * Simulates the smartwatch. Samples inertial and physiological data.
   * Extracts 28 statistical features to compress the payload.
   * Transmits data via **ESP-NOW** (connectionless, 2.4 GHz) for ultra-low power consumption, featuring automatic Wi-Fi Channel Hopping.

2. **Edge Layer (ESP32-S3 Smart Gateway)**
   * Acts as the decision-making core.
   * **MQTT Up:** Formats the payload into JSON and offloads it to the Fog Node.
   * **MQTT Down (Network Failure):** Executes real-time native inference using a lightweight Random Forest model (generated via `micromlgen`) directly in Flash memory.

3. **Fog Layer (Docker Container)**
   * Simulates a local edge server / home automation hub.
   * Runs an MQTT Broker and a Python worker listening for payloads.
   * Executes inference using a high-capacity **XGBoost** model.

## Repository Structure

```text
├── /HAR                  # Jupyter Notebooks: Data cleaning, Downsampling (33Hz), ML Training & Export
├── /ESP                  # Firmware for the Microcontrollers
│   ├── /8266/node        # PlatformIO project for the Sensing Node (ESP-NOW TX)
│   └── /32-S3/gateway       # ESP-IDF project for the Edge Gateway (ESP-NOW RX, MQTT, Local ML)
├── /cloud_server         # Dockerized Fog Node environment
│   ├── Dockerfile
│   ├── main.py           # MQTT Worker & XGBoost Inference script
│   └── model/xgb_fog.pkl       # Serialized Fog Model
└── README.md
```

## Scientific Findings

### 1. The Edge vs. Fog Paradox
In a rigorous Subject-Independent validation (training on subjects 1-7, testing on 8-9), the constrained Edge model beat the Fog model. The high-capacity XGBoost model overfitted the specific gaits of the training subjects, failing to generalize. The Edge model, limited to 15 trees, was forced to learn true macro-patterns.

| Environment | Model | Accuracy |
| :--- | :--- | :--- |
| **Edge (Local)** | **Lightweight Random Forest** | **75.39%** |
| Fog (Remote) | Heavyweight Random Forest | 70.07% |
| Fog (Remote) | XGBoost (Base) | 73.84% |

### 2. Network Resilience
By transmitting extracted statistical features rather than raw 100Hz IMU data, the system is incredibly robust to network interference. Simulating a catastrophic **50% packet loss** between the sensor and gateway resulted in only a marginal **~1.2% drop** in accuracy.

## Getting Started

### Prerequisites
* [PlatformIO](https://platformio.org/) (for ESP8266)
* [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) (for ESP32)
* Docker & Docker Compose

### Quick Setup
1. **Fog Node:** Navigate to `/cloud_server`, build the Docker image, and spin up the container and MQTT broker.
   ```bash
   docker-compose up --build -d
   ```
2. **Gateway Configuration:** Update `secrets.h` in `/ESP/32-S3/gateway/main` with your Wi-Fi credentials and MQTT broker IP. Flash to the ESP32.
3. **Sensor Node:** Update the target MAC address of the Gateway and the Wi-Fi network name in `/ESP/8266/node/include/secrets.h`. Flash to the ESP8266.
4. **Monitor:** Open the Serial Monitor on the ESP32 to watch the *Smart Offloading* in action. Disconnect your Wi-Fi router to see the *Graceful Degradation* switch to local Edge inference!

##  Author
**Matteo Battilori**
* Project for the *IoT Systems* Course (2025/26)
* University of Modena and Reggio Emilia (UNIMORE)