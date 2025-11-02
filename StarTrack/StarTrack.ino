// ====================================================================
// Star Track Project - Arduino Uno Tracking Engine Firmware (TrackingEngine)
// Purpose: Receives trajectory data from ESP32, manages the real-time clock,
//          implements homing, and executes precise, non-blocking motor tracking.
// Dependencies: AccelStepper Library (Install via Arduino Library Manager)
// NOTE: This code uses the Uno's Hardware Serial (pins D0/D1) for communication 
// with the ESP32. You must disconnect the ESP32 during Uno code upload.
//
// By John Durham @kilodelta8
// 10/27/2025
// CIS 2427 IoT Fundamentals
// ====================================================================

#include <AccelStepper.h>

// --- System Configuration and Motor/Driver Setup ---

// Define Uno Pins for DRV8825 Drivers (as per BUILDPLANS.md)
// Azimuth (Horizontal Rotation)
#define AZ_STEP_PIN 2 // Digital 2
#define AZ_DIR_PIN  3 // Digital 3
// Elevation (Vertical Angle)
#define EL_STEP_PIN 4 // Digital 4
#define EL_DIR_PIN  5 // Digital 5

// Define Uno Pins for Limit Switches (Homing)
#define AZ_HOME_PIN A0 // Analog 0 - Azimuth Home/Limit
#define EL_HOME_PIN A1 // Analog 1 - Elevation Home/Limit

// Define Motor Parameters (Based on 1.8 degree NEMA 17)
#define STEPS_PER_REV 200.0 
#define MICROSTEPPING 32.0 // DRV8825 set to 1/32 microstep (M0, M1, M2 pins HIGH/Floating)

// Gear Ratios (Example: Assuming 100:1 total reduction for high precision)
// Update these values based on your final mechanical build (Worm or Belt Drive)
#define AZ_GEAR_RATIO 100.0 
#define EL_GEAR_RATIO 100.0 // Worm drive for elevation is highly recommended
#define STEPS_PER_DEGREE_AZ (STEPS_PER_REV * MICROSTEPPING * AZ_GEAR_RATIO) / 360.0 
#define STEPS_PER_DEGREE_EL (STEPS_PER_REV * MICROSTEPPING * EL_GEAR_RATIO) / 360.0

// Tracking Parameters
#define MAX_SPEED_DEG_SEC 50.0 // Max speed for slewing (50 deg/sec)
#define MAX_ACCEL_DEG_SEC 150.0 // Acceleration (150 deg/sec/sec)

// --- Trajectory Data Structures ---

// A single point in the satellite's path
struct TrajectoryPoint {
  long epochTime; // The time (seconds since 1970) this position is valid
  float azimuth;  // Target Azimuth in degrees (0-360)
  float elevation; // Target Elevation in degrees (0-90)
};

#define MAX_POINTS 100 // Max points the Uno can store (depends on Uno RAM, 100 is safe)
TrajectoryPoint trajectory[MAX_POINTS];
int trajectoryCount = 0;
int nextPointIndex = 0;
bool trajectoryLoaded = false;

// --- State Machine and Clock ---
enum SystemState { 
  IDLE, 
  HOMING, 
  READY_TO_TRACK, 
  TRACKING, 
  STOPPING, 
  ERROR 
};
SystemState currentState = IDLE;

long currentEpochTime = 0; // Synchronized via ESP32
long lastSyncMillis = 0;   // last time the epoch was received
const long CLOCK_DRIFT_THRESHOLD = 60000; // Check clock every 60 seconds

// --- AccelStepper Initialization ---
// Initialize with type 1 for DRV8825 (STEPPER::DRIVER)
AccelStepper stepperAz(AccelStepper::DRIVER, AZ_STEP_PIN, AZ_DIR_PIN);
AccelStepper stepperEl(AccelStepper::DRIVER, EL_STEP_PIN, EL_DIR_PIN);

// ====================================================================
// Motor Control and Utility Functions
// ====================================================================

// Converts an angle to the corresponding absolute position in steps
long angleToSteps(float angle, float stepsPerDegree) {
  // We assume 0 steps = 0 degrees (Set by Homing)
  return (long)(angle * stepsPerDegree);
}

void setMotorParameters() {
  // Convert max angular speed/acceleration to steps/second 
  stepperAz.setMaxSpeed(MAX_SPEED_DEG_SEC * STEPS_PER_DEGREE_AZ);
  stepperAz.setAcceleration(MAX_ACCEL_DEG_SEC * STEPS_PER_DEGREE_AZ);
  stepperEl.setMaxSpeed(MAX_SPEED_DEG_SEC * STEPS_PER_DEGREE_EL);
  stepperEl.setAcceleration(MAX_ACCEL_DEG_SEC * STEPS_PER_DEGREE_EL);
  
  // Set initial default speed for homing
  stepperAz.setSpeed(10.0 * STEPS_PER_DEGREE_AZ); 
  stepperEl.setSpeed(10.0 * STEPS_PER_DEGREE_EL);
}

// Function to move to a new Az/El target position
void moveToTarget(float targetAz, float targetEl) {
  long targetStepsAz = angleToSteps(targetAz, STEPS_PER_DEGREE_AZ);
  long targetStepsEl = angleToSteps(targetEl, STEPS_PER_DEGREE_EL);

  stepperAz.moveTo(targetStepsAz);
  stepperEl.moveTo(targetStepsEl);
}

// ====================================================================
// System Actions (Homing and Tracking)
// ====================================================================

// Blocking Homing Routine: Finds home, sets position to 0
void homeAxes() {
  currentState = HOMING;
  Serial.println("STATUS_UPDATE:HOMING");

  // --- 1. Home Azimuth Axis ---
  Serial.println("Homing Az...");
  // Set speed and move slowly in the direction of the home switch (e.g., negative steps)
  stepperAz.setSpeed(-5.0 * STEPS_PER_DEGREE_AZ); // 5 deg/sec in reverse
  
  // Continuously move until the limit switch is hit (assuming NC wiring means HIGH when open)
  while (digitalRead(AZ_HOME_PIN) == HIGH) {
    stepperAz.runSpeed();
    // Safety timeout could be added here
  }
  
  // Back off a small distance (e.g., 2 degrees) to release the switch
  stepperAz.move(angleToSteps(2.0, STEPS_PER_DEGREE_AZ));
  while (stepperAz.distanceToGo() != 0) {
    stepperAz.run();
  }

  // Set this position as the zero reference (True North)
  stepperAz.setCurrentPosition(0);
  Serial.println("Azimuth Homing Complete.");

  // --- 2. Home Elevation Axis ---
  Serial.println("Homing El...");
  stepperEl.setSpeed(-5.0 * STEPS_PER_DEGREE_EL); // 5 deg/sec in reverse
  
  while (digitalRead(EL_HOME_PIN) == HIGH) {
    stepperEl.runSpeed();
  }

  // Back off a small distance (e.g., 2 degrees) to release the switch
  stepperEl.move(angleToSteps(2.0, STEPS_PER_DEGREE_EL));
  while (stepperEl.distanceToGo() != 0) {
    stepperEl.run();
  }
  
  // Set this position as the zero reference (Horizontal, 0 degrees) !!! 15 degrees declination for my location !!!
  stepperEl.setCurrentPosition(0);
  Serial.println("Elevation Homing Complete.");

  currentState = READY_TO_TRACK;
  Serial.println("STATUS_UPDATE:READY_TO_TRACK");
}

// Main function for trajectory following
void runTrackingEngine() {
  
  if (!trajectoryLoaded || trajectoryCount == 0) {
    Serial.println("ERROR: No trajectory data to track.");
    currentState = ERROR;
    return;
  }
  
  // 1. Update the local Epoch Clock
  if (millis() - lastSyncMillis > CLOCK_DRIFT_THRESHOLD) {
    // We haven't received a fresh time sync from ESP32 in a while. 
    // This clock drift is expected and fine, but we update the time.
    currentEpochTime += (millis() - lastSyncMillis) / 1000;
    lastSyncMillis = millis();
  }

  // 2. Find the current target point
  // Check if we are past the current target point's time
  while (nextPointIndex < trajectoryCount && currentEpochTime >= trajectory[nextPointIndex].epochTime) {
    // We are past the time for the current point, move to the next one
    nextPointIndex++;
  }

  // 3. Tracking Logic (Interpolation)
  if (nextPointIndex >= trajectoryCount) {
    // End of Trajectory
    Serial.println("STATUS_UPDATE:IDLE");
    Serial.println("Tracking complete.");
    currentState = IDLE;
    trajectoryLoaded = false;
    return;
  }
  
  // Current target point is the one we just passed (or 0 if tracking just started)
  TrajectoryPoint startPoint = trajectory[nextPointIndex - 1]; 
  // Next point is the one we are moving towards
  TrajectoryPoint endPoint = trajectory[nextPointIndex]; 

  // Calculate time elapsed since the start point and the total time interval
  float timeElapsed = currentEpochTime - startPoint.epochTime;
  float timeInterval = endPoint.epochTime - startPoint.epochTime;

  // Linear Interpolation Factor (0.0 to 1.0)
  float factor = timeInterval > 0 ? timeElapsed / timeInterval : 0.0;
  
  // Calculate interpolated Azimuth and Elevation
  float currentAz = startPoint.azimuth + (endPoint.azimuth - startPoint.azimuth) * factor;
  float currentEl = startPoint.elevation + (endPoint.elevation - startPoint.elevation) * factor;
  
  // 4. Send motors to interpolated position
  moveToTarget(currentAz, currentEl);
  
  // 5. Run the motors (non-blocking)
  stepperAz.run();
  stepperEl.run();
  
  // Optional: Send current position back to ESP32 for web status update
  if (millis() % 1000 == 0) { // Send roughly once per second
    float currentPosAz = (float)stepperAz.currentPosition() / STEPS_PER_DEGREE_AZ;
    float currentPosEl = (float)stepperEl.currentPosition() / STEPS_PER_DEGREE_EL;
    
    // Format: POS:Az,El,Time
    Serial.print("POS:");
    Serial.print(currentPosAz, 2);
    Serial.print(",");
    Serial.print(currentPosEl, 2);
    Serial.print(",");
    Serial.println(currentEpochTime);
  }
}

// ====================================================================
// Serial Communications Handler
// ====================================================================

void handleSerialCommand() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    Serial.print("-> ESP32 Command: ");
    Serial.println(command);

    if (command.startsWith("TIME:")) {
      // Format: TIME:1730707200
      currentEpochTime = command.substring(5).toInt();
      lastSyncMillis = millis();
      Serial.print("Clock Synced to: ");
      Serial.println(currentEpochTime);
    } 
    else if (command.startsWith("CMD:HOME")) {
      homeAxes();
    } 
    else if (command.startsWith("CMD:STOP")) {
      currentState = IDLE;
      stepperAz.stop();
      stepperEl.stop();
      Serial.println("STATUS_UPDATE:IDLE");
      trajectoryLoaded = false; // Cancel any active trajectory
    } 
    else if (command.startsWith("CMD:START_TRAJ")) {
      currentState = READY_TO_TRACK;
      trajectoryCount = 0; // Reset count
      Serial.println("Ready to receive trajectory data.");
    } 
    else if (command.startsWith("DATA:")) {
      // Format: DATA:T1,Az1,El1|T2,Az2,El2|...
      if (currentState != READY_TO_TRACK) {
        Serial.println("ERROR: Cannot receive data. Not in READY_TO_TRACK state.");
        return;
      }
      
      String dataString = command.substring(5);
      
      // Use strtok for robust parsing (since Uno doesn't handle String manipulation well)
      char buffer[dataString.length() + 1];
      dataString.toCharArray(buffer, sizeof(buffer));
      
      char* pointToken = strtok(buffer, "|");
      int i = 0;

      while (pointToken != NULL && i < MAX_POINTS) {
        // Parse T, Az, El from the pointToken (e.g., "1730707200,45.5,10.2")
        char* token;
        
        // Time
        token = strtok(pointToken, ",");
        trajectory[i].epochTime = (token != NULL) ? atol(token) : 0;
        
        // Azimuth
        token = strtok(NULL, ",");
        trajectory[i].azimuth = (token != NULL) ? atof(token) : 0.0;

        // Elevation
        token = strtok(NULL, ",");
        trajectory[i].elevation = (token != NULL) ? atof(token) : 0.0;
        
        i++;
        pointToken = strtok(NULL, "|");
      }
      
      trajectoryCount = i;
      trajectoryLoaded = true;
      nextPointIndex = 1; // Start interpolation from the first point
      
      Serial.print("Trajectory received. Points: ");
      Serial.println(trajectoryCount);
      
      // Move to TRACKING state and perform initial slewing
      currentState = TRACKING;
      Serial.println("STATUS_UPDATE:TRACKING");
    }
    else if (command.startsWith("QUERY:STATUS")) {
      // Respond to ESP32 status query
      if (currentState == TRACKING) {
        Serial.println("STATUS_UPDATE:TRACKING");
      } else if (currentState == IDLE) {
        Serial.println("STATUS_UPDATE:IDLE");
      } else if (currentState == HOMING) {
        Serial.println("STATUS_UPDATE:HOMING");
      } else {
        Serial.println("STATUS_UPDATE:UNKNOWN");
      }
    }
  }
}

// ====================================================================
// Arduino Setup and Main Loop
// ====================================================================

void setup() {
  Serial.begin(115200); // Initialize serial to ESP32
  
  // Initialize Limit Switch pins with internal pull-up resistors
  pinMode(AZ_HOME_PIN, INPUT_PULLUP);
  pinMode(EL_HOME_PIN, INPUT_PULLUP);
  
  // Set up motor driver pins
  pinMode(AZ_STEP_PIN, OUTPUT);
  pinMode(AZ_DIR_PIN, OUTPUT);
  pinMode(EL_STEP_PIN, OUTPUT);
  pinMode(EL_DIR_PIN, OUTPUT);
  
  // Set up AccelStepper motor parameters (Max Speed and Acceleration)
  setMotorParameters();

  Serial.println("STATUS_UPDATE:INIT");
}

void loop() {
  // 1. Check for commands from ESP32
  handleSerialCommand();

  // 2. State Machine Logic
  switch (currentState) {
    case IDLE:
      // Motors are stopped. Await HOME or START_TRAJ command.
      break;
      
    case HOMING:
      // The homeAxes() function is blocking, so this state should only be
      // entered when the user initiates it. The function handles the transition
      // to READY_TO_TRACK.
      break;
      
    case READY_TO_TRACK:
      // Waiting for the current epoch time to match the first point's time
      if (trajectoryLoaded && currentEpochTime >= trajectory[0].epochTime) {
        currentState = TRACKING;
      }
      break;
      
    case TRACKING:
      runTrackingEngine();
      break;
      
    case STOPPING:
      // Can implement a controlled deceleration if needed, but CMD:STOP handles it
      break;

    case ERROR:
      // Send error status frequently
      Serial.println("STATUS_UPDATE:ERROR");
      delay(500);
      break;
  }
}
