# Star Track Project: Apparatus Build Instructions (Step 3)

This document provides detailed assembly instructions for the mechanical and electrical components of the Star Track satellite rotator. Choose one option (A or B) for both the Azimuth and Elevation axes.

## I. Materials Check and Preparation

Ensure you have the following core components ready, plus your chosen mechanical components (Worm Gear or Belt Drive kits).

| Component                            | Quantity  | Notes                                                        |
| ------------------------------------ | --------- | ------------------------------------------------------------ |
| *Arduino Uno*                        | 1         | Motor Control (Primary MCU)                                  |
| *ESP32 Dev Board*                    | 1         | Wi-Fi/Time Sync/Communication                                |
| *NEMA 17 Stepper Motor*              | 2         | One for Azimuth, one for Elevation                           |
| *DRV8825 Driver*                     | 2         | Stepper motor drivers (plus spares)                          |
| *12V  5A Power Supply* | 1         | Dedicated for the motors and drivers                         |
| *Limit Switches*                     | 3         | 2 for Azimuth Home/Limits, 1 for Elevation Home          |
| *5V Regulator*               | 1         | To step down 12V to clean 5V for the ESP32/Uno |
| *Structural Material*                | As needed | 2020 Aluminum Extrusion or Plywood/PVC                       |
| *Bearings*                           | 2-4     | Thrust or large radial bearings for axis support             |
| *Wiring and Connectors*              | Various   | Power wires, signal wires, screw terminals                   |
| *Weatherproof Enclosure*             | 1         | For housing all electronics                                  |

## II. Mechanical Assembly (Azimuth Axis)

The Azimuth axis provides 360^\circ rotation and supports the entire Elevation assembly and antenna.

### Shared: Tripod Mounting and Base Frame

*   **Base Plate:** Construct a sturdy base plate designed to be rigidly affixed to your field tripod head.
*   **Structural Frame:** Assemble the main upright structure using the *2020 Aluminum Extrusion* (or cut the equivalent frame from plywood/PVC). This frame must be strong enough to hold the Azimuth motor and bearing assembly.
*   **Azimuth Bearing:** Install a large thrust or radial bearing into the center of the base frame. This bearing carries the payload weight (2.1kg max) and is the pivot point for the Azimuth shaft.

### Option A: High-Precision Azimuth (Worm Gear Drive)

*   **Gearbox Integration:** Source a *NEMA 17* stepper motor pre-assembled with a planetary or worm gearbox (ratio 60:1 or higher).
*   **Motor Mount:** Mount the geared motor rigidly to the base frame.
*   **Shaft Coupling:** The output shaft of the gearbox must be coupled directly or via a heavy-duty coupling to the main vertical Azimuth shaft, which is supported by the bearing. **Crucially**, ensure the gearbox output shaft is perfectly centered with the main bearing to prevent binding.
*   **Limit Switches:** Install two limit switches at the rotational limits (e.g., 0^\circ and 360^\circ or slightly less) to prevent cable entanglement. The primary switch will serve as the **HOME** position (True/Magnetic North).

### Option B: Budget Azimuth (Two-Stage Belt Drive)

*   **Motor Mount:** Mount the raw *NEMA 17* motor to the base frame.
*   **First Reduction Stage:** Mount a small drive pulley (20T) onto the motor shaft and a medium driven pulley (100T) onto a small intermediate shaft, achieving a 5:1 reduction.
*   **Second Reduction Stage:** Mount a small drive pulley (e.g., 20T) onto the intermediate shaft, which drives a very large gear/pulley (e.g., 400T) fixed to the main vertical Azimuth shaft. This yields 5:1 \times 20:1 \approx 100:1 total ratio.
*   **Tensioning:** Use adjustable motor mounts to tension the belts correctly.
*   **Limit Switches:** Install limit switches for homing and limits as described in Option A.

## III. Mechanical Assembly (Elevation Axis)

The Elevation axis pivots the antenna and must handle the antenna's weight against gravity.

### Shared: Elevation Arm and Pivot

*   **Azimuth Platform:** Build a stable platform that mounts to the top of the Azimuth shaft. This platform holds the Elevation motor and pivot mechanism.
*   **Antenna Boom Pivot:** Install the main Elevation shaft/pivot at the center of the platform. This shaft must be secured by two radial bearings for smooth motion.
*   **Counterweighting (Recommended for both options):** Add ballast (e.g., lead or steel weights) to the rear of the antenna boom to balance the assembly around the pivot point. This significantly reduces the load on the motor.

### Option A: High-Precision Elevation (Worm Gear/Sector Gear)

*   **Sector Gear:** Attach a large sector gear (sized for 0^\circ to 90^\circ travel) rigidly to the Elevation pivot shaft.
*   **Worm Mount:** Mount the *NEMA 17* motor and its worm to the Elevation platform, ensuring the worm meshes perfectly with the teeth of the sector gear.
*   **Self-Locking Check:** After assembly, verify that the worm drive self-locks, meaning the antenna will not drift down due to gravity when the motor is unpowered.
*   **Limit Switch:** Install one limit switch at the horizontal position (0^\circ Elevation) to serve as the **HOME** reference for the Elevation axis.

### Option B: Budget Elevation (Belt Drive with Holding Current)

*   **Pulleys:** Install a pulley on the Elevation pivot shaft and a small drive pulley on the *NEMA 17* motor. Aim for a high ratio (e.g., 50:1 or 75:1) via a multi-stage belt setup if necessary.
*   **Hold Current Plan:** The software plan dictates that this motor will require a continuous holding current when idle. Ensure the motor is robust and the current limit on the *DRV8825* is set correctly to prevent overheating.
*   **Limit Switch:** Install a limit switch at the 0^\circ Elevation position for homing.

## IV. Electrical Integration and Wiring

All electronics must be housed in the weatherproof enclosure and connected as specified.

### 4.1 DRV8825 Current Limiting (CRITICAL STEP)

Before connecting the motors, set the current limit (I_MAX) on the *DRV8825* drivers using the trim pot.

```
V_REF = {I_{MAX}} \times 0.9
```

*   Consult your *NEMA 17* motor's datasheet for its rated phase current (I_{MAX}). (Typical *NEMA 17* motors are 1.5A or 1.7A).
*   Measure the voltage on the `V_REF` pin while turning the trim pot.
*   Set `V_REF` to the calculated value (e.g., for a 1.5A motor, set `V_REF` to 1.35V). This protects the motor and driver.

### 4.2 Power Distribution

*   **12\text{V} Rail:** Connect the 12V supply directly to the *DRV8825* motor power inputs (`VMOT` and `GND`).
*   **5\text{V} Rail:** Use the separate 5V regulator to create a clean power source. Connect this 5V rail to the *Arduino Uno*'s `5V` pin and the *ESP32*'s `5V` input pin (`VBUS` or similar).

### 4.3 Wiring Diagram (ESP32, Uno, Drivers, Switches)

| Component     | Pin Function  | Connects To                   | Notes                                                                        |
| ------------- | ------------- | ----------------------------- | ---------------------------------------------------------------------------- |
| *Arduino Uno* | Digital 2     | Azimuth STEP pin (DRV8825)    | PWM output for movement                                                      |
| *Arduino Uno* | Digital 3     | Azimuth DIR pin (DRV8825)     | Direction control                                                            |
| *Arduino Uno* | Digital 4     | Elevation STEP pin (DRV8825)  | PWM output for movement                                                      |
| *Arduino Uno* | Digital 5     | Elevation DIR pin (DRV8825)   | Direction control                                                            |
| *Arduino Uno* | A0            | Azimuth Home Switch           | Input with pull-up resistor                                                  |
| *Arduino Uno* | A1            | Elevation Home Switch         | Input with pull-up resistor                                                  |
| *Arduino Uno* | RX            | ESP32 TX pin                  | Serial communication (Data/Commands)                                         |
| *Arduino Uno* | TX            | ESP32 RX pin                  | Serial communication (Status/Acks)                                           |
| *DRV8825*     | M0, M1, M2    | Arduino GND or Digital Pins   | Set to 1/32 microstepping (all to HIGH/Floating, depending on your board) |
| *DRV8825*     | ENABLE        | Arduino GND                   | Keep LOW for normal operation                                                |
| *ESP32*       | D21 (TX)      | Arduino RX pin                | Hardware Serial connection                                                   |
| *ESP32*       | D22 (RX)      | Arduino TX pin                | Hardware Serial connection                                                   |
| *Switches*    | Signal Pin    | Arduino Analog Inputs         | Wired for Normally Closed (NC) using internal pull-ups for robustness.       |

### 4.4 Final Enclosure and Testing

*   **Mounting:** Secure all boards inside the weatherproof enclosure. Use standoffs to prevent short circuits.
*   **Pass-Throughs:** Use cable glands or sealed grommets for the motor, switch, and power cables to maintain the enclosure's weather resistance.
*   **Initial Power Test:** Apply 12V power and verify the 5V rail is correct. **Do not** connect the Uno/ESP32 USB during this test unless necessary, relying solely on the 5V regulator.



