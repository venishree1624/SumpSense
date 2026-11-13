# SumpSense: IoT-Based Smart Sump Water-Level Monitoring and Early-Warning System

## Overview
SumpSense is an ESP32-based IoT system designed to monitor sump water levels and provide real-time alerts. It uses an ultrasonic sensor to measure the water level and indicates the status through an OLED display, RGB LED, and buzzer. Blynk IoT integration enables remote monitoring and alert control over Wi-Fi.

## Features
- Real-time water-level measurement using an ultrasonic sensor.
- OLED display for water-level and system-status information.
- RGB LED indicators for low, normal, and high water levels.
- Audible alerts through a buzzer.
- Remote monitoring through the Blynk IoT dashboard.
- Remote buzzer control through the Blynk application.
- Local monitoring continues even when Wi-Fi is unavailable.

## Hardware Components
- ESP32 DevKit V1
- HC-SR04 Ultrasonic Sensor
- SH1106 OLED Display
- RGB LED
- Active Buzzer
- Push Button
- Breadboard and Jumper Wires

## Circuit Connections

| Component | ESP32 GPIO |
|---|---:|
| OLED SDA | 13 |
| OLED SCL | 26 |
| Ultrasonic TRIG | 27 |
| Ultrasonic ECHO | 34 |
| RGB LED Red | 25 |
| RGB LED Green | 33 |
| RGB LED Blue | 32 |
| Buzzer | 14 |
| Push Button | 35 |

## Working Principle
The HC-SR04 ultrasonic sensor measures the distance between the sensor and the water surface. The ESP32 processes the measurement and determines the water level using predefined thresholds.

The system classifies the water level into three states:
- **Low:** Below 4 cm
- **Normal:** Between 4 cm and 14 cm
- **High:** Above 14 cm

Based on the detected level, the system updates the OLED display, controls the RGB LED, and activates the buzzer when required.

## IoT Integration
The ESP32 communicates with the Blynk IoT platform over Wi-Fi to send water-level readings and system status. The dashboard enables remote monitoring and buzzer control.

## Known Limitations
- The physical push-button mute function may occasionally require multiple attempts to register. Remote buzzer control through Blynk is used as the current workaround.
- Remote monitoring and control require a Wi-Fi connection.
- The system requires a continuous power supply to operate.

## Future Enhancements
- Improve physical push-button response through optimised polling and debouncing.
- Add backup power support.
- Enhance the dashboard with historical water-level data and analytics.

## Project Status
Developed and demonstrated as an academic IoT hardware project.
