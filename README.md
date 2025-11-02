# Star Track Project: Satellite Tracking Antenna

## Phase 1: Planning and System Design (Steps 1 & 2)

This phase defines the system architecture, chooses the core components, and finalizes the requirements for the hardware and software.

### 1.1 System Architecture Overview

The system will use a three-tier architecture:

1. **Presentation/Calculation Layer (Flask Web App)**: Handles TLE data, orbital mechanics calculations, user interface, satellite selection, and scheduling.
2. **Communication Layer (Wi-Fi Module)**: Provides a wireless link between the Flask app (local network) and the microcontroller.
3. **Control Layer (Arduino Uno)**: Receives target Azimuth and Elevation coordinates, drives the stepper motors, and executes the tracking sequence.

### 1.2 Component Selection (Preliminary)

| Component         | Purpose                          | Preliminary Selection | Notes                                                                 |
|--------------------|----------------------------------|------------------------|-----------------------------------------------------------------------|
| Microcontroller    | Motor control and interface     | Arduino Uno            | Confirmed by user.                                                   |
| Connectivity       | Wi-Fi communication             | ESP32 or ESP8266       | ESP32 is preferred for better performance and simultaneous Wi-Fi/Bluetooth capabilities. |
| Motors (2x)        | Azimuth (Rotation) & Elevation (Angle) | NEMA 17 Stepper Motors | Good torque and precision. Need two.                                 |
| Motor Drivers (2x) | Driving the steppers            | A4988 or DRV8825       | DRV8825 offers higher current capacity and finer microstepping for smoother movement. |
| Power Supply       | Powering motors and electronics | 12V DC Adapter         | Separate supply is necessary for motors to prevent brownouts on the Arduino. |
| Antenna            | Tracking target                 | Yagi Antenna (Lightweight) | Must be lightweight to minimize torque requirements.                 |
| Mounting           | Structural stability            | Tripod with 2-Axis Mount | Custom 3D-printed or modified mount for steppers and antenna.        |

### 1.3 Orbital Mechanics & Data Flow Definition

- **Data Source**: Two-Line Element (TLE) data for satellites (e.g., from Celestrak).
- **Calculation Engine**: Python library (e.g., `sgp4`, `pyorbital`) within the Flask app to predict Azimuth/Elevation angles for a given TLE and time/location.
- **Output Data**: The Flask app will transmit time-stamped Az/El coordinate pairs (e.g., "Time, Azimuth, Elevation") to the Arduino over Wi-Fi.

---

## Phase 2: Hardware Build and Mechanical Integration (Steps 3 & 4)

This phase focuses on the physical assembly and connection of all components.

### 2.1 Mechanical Design and Build

- **Mount Design**: Design and build the 2-axis mechanism to hold the antenna and accommodate the two stepper motors. Must ensure zero backlash (or minimal) for accurate tracking.
- **Tripod Integration**: Secure the Azimuth motor/gear mechanism to the tripod base.
- **Alignment**: Integrate a mechanism or bubble level to accurately set the base to True North and zero degrees elevation.

### 2.2 Electronics Wiring

- Wire the Arduino Uno, the ESP module (Wi-Fi), the two stepper drivers, and the power supply.
- Ensure the motor power and Arduino/ESP power are correctly isolated or regulated.

### 2.3 Stepper Calibration

- Determine the steps-per-degree ratio for both the Azimuth and Elevation axes. This is crucial for accurate movement.

---

## Phase 3: Software Development and Integration (Step 4)

This phase involves writing and testing the three core software pieces.

### 3.1 Flask Web Application

- **UI**: Develop a responsive interface to select the current location (GPS coordinates), input TLE data, select a satellite, and define the start/end time for tracking.
- **Orbital Calculation Logic**: Implement the Python code to perform the SGP4 calculation and generate a list of Az/El target points over time (e.g., every 1-5 seconds).
- **Communication Endpoint**: Create an API endpoint (`/track`) that sends the calculated point list to the ESP module via HTTP POST or WebSockets.

### 3.2 Arduino Firmware (Uno + ESP Module)

- **Wi-Fi Listener**: Program the ESP module to connect to the "junk Wi-Fi router" and listen for incoming tracking data from the Flask app.
- **Command Parsing**: Develop code on the Arduino to receive, parse, and store the Az/El point list.
- **Motor Control**: Implement a precise motion control routine:
    - **Interpolation**: Smooth movement between commanded Az/El points.
    - **Timing**: Using the onboard clock (or time synced from the ESP) to hit each coordinate at the correct time.

---

## Phase 4: Testing, Calibration, and Reporting (Steps 5, 6, & 7)

The final stages involve rigorous testing and documentation.

### 4.1 Unit Testing

- Test the Flask SGP4 calculator against known satellite paths.
- Test the Arduino's stepper control for movement accuracy (degrees moved vs. commanded degrees).
- Test the Wi-Fi connection and data transfer reliability.

### 4.2 System Integration and Field Test

- Perform a full end-to-end test: Flask calculates, sends data, Arduino tracks.
- **Field Calibration**: Calibrate the system in the field, aligning it to True North and adjusting for any mechanical offsets or errors in the Az/El calculation.

### 4.3 Documentation and Final Report

- Finalize the **Build Instructions** (Step 6) including parts lists, assembly diagrams, and wiring schematics.
- Prepare the **Final Report** (Step 7) detailing the design choices, test results (accuracy), and potential improvements.

---

This plan provides a solid roadmap. The next step, **Step 2: Refine each section of the plan**, will involve diving into the technical details for component selection, the communication protocol, and the specific libraries we'll use.