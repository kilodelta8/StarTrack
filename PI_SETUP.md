# StarTrack Raspberry Pi Setup Guide

This guide explains how to set up the StarTrack system on a Raspberry Pi, connecting directly to the Arduino via USB and using a USB GPS module.

## 1. Hardware Connections
1.  **Arduino Uno**: Connect to the Raspberry Pi via USB cable.
2.  **GPS Module**: Connect to the Raspberry Pi via USB (or native UART pins, though USB is easier).
3.  **Motors**: Connect NEMA 23 motors to your drivers (DRV8825/DM542T), and the drivers to the Arduino as per the `StarTrack.ino` pin definitions.
    *   **Azimuth**: Step=D2, Dir=D3
    *   **Elevation**: Step=D4, Dir=D5
    *   **Power**: Ensure your motor drivers have adequate 12V-24V power. NEMA 23s often need more current than NEMA 17s.

## 2. Software Installation (Raspberry Pi)

### Implement Dependencies
Open a terminal on your Pi and install the Python requirements:

```bash
# If you haven't already cloned the repo
git clone https://github.com/kilodelta8/StarTrack.git
cd StarTrack/WebApp

# Create and activate a Virtual Environment (Recommended)
python3 -m venv venv
source venv/bin/activate

# Install Libraries
pip install -r requirements.txt
```

### verify Device Permissions
The current user needs permission to access serial ports.
```bash
sudo usermod -a -G dialout $USER
```
*Reboot the Pi after running this command.*

## 3. Running the Application
1.  Plug in the Arduino and GPS.
2.  Start the Flask App:
    ```bash
    cd StarTrack/WebApp
    source venv/bin/activate
    python3 StarTrackWebApp.py
    ```
3.  The app will attempt to auto-detect:
    *   **Arduino**: Looking for devices with "Arduino" or "ACM" in the name.
    *   **GPS**: Looking for devices with "USB" or "Serial" (that aren't the Arduino).
4.  Watch the terminal output for "Connected to Arduino..." messages.

## 4. Troubleshooting
*   **"Arduino not found"**: Check `ls /dev/tty*`. If your Arduino shows up as `/dev/ttyUSB0` (some clones do), you might need to edit `StarTrackWebApp.py` line 38 to encompass that, or hardcode the port.
*   **Motors not moving**: Check your 12V/24V power supply. Check the `StarTrack.ino` baud rate matches (115200).
*   **GPS not fixing**: Ensure the GPS antenna has a clear view of the sky. Indoor testing often fails.

## 5. NEMA 23 Note
If using NEMA 23 motors, ensure your drivers (e.g., DRV8825) are current-limited correctly.
*   **Formula**: `V_REF = Current_Limit / 2` (Standard DRV8825) or `Current_Limit * 5 * Resistor_Value`. Consult your driver's datasheet!
*   **Torque**: NEMA 23s provide more torque, which is great for the heavier Yagi antenna.
