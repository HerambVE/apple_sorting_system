# 🔧 Hardware Setup Guide

This document details the hardware components and pin mappings required for the Apple Sorting System.

## Component List

| Component | Quantity | Description |
|-----------|----------|-------------|
| Raspberry Pi 4/5 | 1 | Main controller, handles logic and camera capture |
| L298N Motor Driver | 1 | Controls the conveyor belt DC motor |
| DC Motor | 1 | Drives the conveyor belt |
| Servo Motor | 3 | Actuates the sorting gates to push apples off the belt |
| IR Sensor | 1 | Detects the presence of an apple |
| Pi CSI Camera | 1 | Takes top-down view images |
| USB Webcam | 1 | Takes side-view images |
| External Battery | 1 | Powers the motor driver and servos independently |

## Pin Mappings

All pin numbers use the **BCM (Broadcom)** numbering system.

| Component | Pin Type | RPi BCM Pin | Notes |
|-----------|----------|-------------|-------|
| IR Sensor | Input | BCM 17 | Goes HIGH/LOW when obstacle detected |
| L298N ENA | PWM Output | BCM 12 | Speed control (PWM set to ~180) |
| L298N IN1 | Digital Out| BCM 27 | Motor direction control |
| L298N IN2 | Digital Out| BCM 22 | Motor direction control |
| Servo 1 (Blotch) | PWM Output| BCM 18 | Pushes blotched apples |
| Servo 2 (Rot) | PWM Output| BCM 23 | Pushes rotten apples |
| Servo 3 (Scab) | PWM Output| BCM 24 | Pushes scab apples |

## Wiring Notes
1. **Power**: Do NOT power the L298N motor driver or the servos directly from the Raspberry Pi 5V pins. Use the external battery.
2. **Grounds**: Ensure the ground of the external battery, the motor driver, the servos, and the Raspberry Pi are all connected together (Common Ground).
3. **Cameras**: 
   - Connect the Pi Camera to the CSI port.
   - Connect the USB Webcam to a USB 3.0 (blue) port on the Pi. It typically mounts at `/dev/video1` (assuming `/dev/video0` is the CSI camera).
