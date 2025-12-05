
# By John Durham @kilodelta8
# 10/27/2025
# CIS 2427 IoT Fundamentals


import os
import time
import requests
import json
import threading
import serial
import serial.tools.list_ports

app = Flask(__name__)

# --- CONFIGURATION ---
ARDUINO_BAUD_RATE = 115200
GPS_BAUD_RATE = 9600
# Ports will be auto-detected or specified here
# ARDUINO_PORT = "/dev/ttyACM0" 
# GPS_PORT = "/dev/ttyUSB0"

# --- Global State ---
system_status = {
    "status": "DISCONNECTED", 
    "az": 0.0, 
    "el": 0.0, 
    "time": 0,
    "gps_lat": None,
    "gps_lon": None,
    "gps_fix": False
}

# --- Serial Managers ---

class ArduinoController:
    def __init__(self, baud_rate=115200):
        self.baud_rate = baud_rate
        self.serial_conn = None
        self.lock = threading.Lock()
        self.running = True
        self.connect()

        # Start listener thread
        self.thread = threading.Thread(target=self._listen, daemon=True)
        self.thread.start()

    def connect(self):
        # Auto-detect Arduino (Uno usually has 'Arduino' or 'usbmodem' in name, or just /dev/ttyACM*)
        port = None
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            # Simple heuristic for Pi/Mac
            if "Arduino" in p.description or "usbmodem" in p.device or "ACM" in p.device:
                port = p.device
                break
        
        if port:
            try:
                self.serial_conn = serial.Serial(port, self.baud_rate, timeout=1)
                print(f"Connected to Arduino on {port}")
                system_status["status"] = "IDLE"
            except Exception as e:
                print(f"Failed to connect to Arduino on {port}: {e}")
        else:
            print("Arduino not found.")

    def _listen(self):
        while self.running:
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    if self.serial_conn.in_waiting:
                        line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                        self._parse_line(line)
                except Exception as e:
                    print(f"Serial Read Error: {e}")
                    system_status["status"] = "ERROR"
            time.sleep(0.01)

    def _parse_line(self, line):
        # Handle STATUS_UPDATE:TRACKING, etc.
        if line.startswith("STATUS_UPDATE:"):
            system_status["status"] = line.split(":")[1]
        # Handle POS:Az,El,Time
        elif line.startswith("POS:"):
            parts = line.split(":")[1].split(",")
            if len(parts) >= 3:
                system_status["az"] = float(parts[0])
                system_status["el"] = float(parts[1])
                system_status["time"] = int(float(parts[2]))

    def send_command(self, cmd):
        if self.serial_conn and self.serial_conn.is_open:
            with self.lock:
                full_cmd = f"{cmd}\n"
                self.serial_conn.write(full_cmd.encode('utf-8'))
                return True
        return False

class GPSReader:
    def __init__(self, baud_rate=9600):
        self.baud_rate = baud_rate
        self.serial_conn = None
        self.running = True
        self.connect()
        
        self.thread = threading.Thread(target=self._listen, daemon=True)
        self.thread.start()

    def connect(self):
        # GPS is often /dev/ttyUSB0 or /dev/serial0 on Pi
        # We will try to find a USB serial device that ISN'T the Arduino
        port = None
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            # Skip likely Arduino ports
            if "Arduino" not in p.description and "ACM" not in p.device:
                # This is a guess; often GPS is USB-Serial Controller
                if "USB" in p.device or "serial" in p.device:
                    port = p.device
                    break
        
        if port:
            try:
                self.serial_conn = serial.Serial(port, self.baud_rate, timeout=1)
                print(f"Connected to GPS on {port}")
            except Exception as e:
                print(f"Failed to connect to GPS: {e}")
        else:
            print("GPS not found. Using defaults.")

    def _listen(self):
        while self.running:
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line.startswith("$GPGGA") or line.startswith("$GNGGA"):
                        self._parse_gga(line)
                except Exception as e:
                    pass # GPS errors are common, just ignore
            time.sleep(0.1)

    def _parse_gga(self, line):
        # $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
        try:
            parts = line.split(",")
            if parts[6] != '0': # Fix quality: 0 = invalid
                lat_raw = parts[2]
                lat_dir = parts[3]
                lon_raw = parts[4]
                lon_dir = parts[5]
                
                if lat_raw and lon_raw:
                    # Convert DDMM.MMMM to Decimal Degrees
                    lat_deg = float(lat_raw[:2]) + float(lat_raw[2:]) / 60.0
                    if lat_dir == 'S': lat_deg *= -1
                    
                    lon_deg = float(lon_raw[:3]) + float(lon_raw[3:]) / 60.0
                    if lon_dir == 'W': lon_deg *= -1
                    
                    system_status["gps_lat"] = lat_deg
                    system_status["gps_lon"] = lon_deg
                    system_status["gps_fix"] = True
        except:
            pass

# Initialize Controllers
arduino = ArduinoController(baud_rate=ARDUINO_BAUD_RATE)
gps = GPSReader(baud_rate=GPS_BAUD_RATE)


# --- Mock TLE Data (International Space Station - ZARYA) ---
# In a real application, you'd fetch this dynamically from celestrak.org
ISS_TLE_LINES = [
    "1 25544U 98067A   25305.50000000  .00002130  00000+0  42173-4 0  9999",
    "2 25544  51.6421 213.6268 0005500 240.2789 119.7211 15.49479308472496"
]
# Default observer location (Brookville, OH for context)
DEFAULT_LAT = 39.86
DEFAULT_LON = -84.38
DEFAULT_ALT_M = 300

# --- Skyfield Setup ---
ts = load.timescale()
# Load basic ephemeris data once
eph = load('de421.bsp') 

# ====================================================================
# Core Trajectory Calculation Logic
# ====================================================================

def calculate_trajectory(tle_lines, observer_lat, observer_lon, observer_alt_m):
    """
    Calculates Azimuth and Elevation points for a satellite over a time window.
    
    Returns a Data-Stream-Vector (DSV) string in the format:
    T1,Az1,El1|T2,Az2,El2|T3,Az3,El3|...
    """
    try:
        # Create satellite and observer objects
        satellite = EarthSatellite(tle_lines[0], tle_lines[1], 'ISS', ts)
        observer = Topos(latitude_degrees=observer_lat, 
                         longitude_degrees=observer_lon, 
                         elevation_m=observer_alt_m)
        
        # Calculate time window (e.g., now until 10 minutes from now, sampled every 5 seconds)
        start_time = ts.now().utc_datetime().replace(tzinfo=utc)
        
        # Calculate when the satellite rises and sets above 10 degrees (optional, for finding a pass)
        # We will use a fixed 10-minute window for simplicity in this example
        
        # Fixed 10 minute window for trajectory calculation
        time_step_seconds = 5 # 5 seconds between each point
        duration_seconds = 600 # 10 minutes
        
        t_start = ts.utc(start_time.year, start_time.month, start_time.day, 
                         start_time.hour, start_time.minute, start_time.second)
        
        times = []
        for i in range(0, duration_seconds + 1, time_step_seconds):
            times.append(t_start + (i / 86400.0)) # 86400 seconds in a day

        # Calculate difference vector between satellite and observer
        difference = satellite - observer
        topocentric = difference.at(times)
        
        # Calculate Azimuth and Elevation
        az, el, distance = topocentric.azalt()

        trajectory_points = []
        
        # Format the data into the Arduino DSV string
        for i in range(len(times)):
            epoch_time = int(times[i].ts) # Unix Epoch time (seconds)
            az_deg = az.degrees[i]
            el_deg = el.degrees[i]

            # Filter out points below the horizon (El < 0)
            if el_deg >= 0:
                # Format: T,Az,El
                point = f"{epoch_time},{az_deg:.2f},{el_deg:.2f}"
                trajectory_points.append(point)

        if not trajectory_points:
            return None, "Satellite not visible in the selected 10 minute window."
            
        dsv_string = "|".join(trajectory_points)
        return dsv_string, f"Calculated {len(trajectory_points)} points over {duration_seconds/60} minutes."

    except Exception as e:
        return None, f"Calculation Error: {str(e)}"

# ====================================================================
# Flask Routes and API Endpoints
# ====================================================================

@app.route('/')
def index():
    """Serves the main HTML interface."""
    return render_template_string(open("templates/index.html").read())

@app.route('/api/calculate', methods=['POST'])
def api_calculate():
    """Calculates the trajectory and returns the DSV string."""
    try:
        data = request.json
        # Extract location: prioritize GPS if valid and not overridden by user
        lat = data.get('latitude')
        lon = data.get('longitude')
        
        # If user didn't provide specific coords, use GPS or Default
        if lat is None:
            lat = system_status["gps_lat"] if system_status["gps_fix"] else DEFAULT_LAT
        if lon is None:
            lon = system_status["gps_lon"] if system_status["gps_fix"] else DEFAULT_LON
        # TLE lines are often passed as a list of two strings
        tle = ISS_TLE_LINES 
        
        dsv_string, message = calculate_trajectory(tle, float(lat), float(lon), DEFAULT_ALT_M)
        
        if dsv_string:
            return jsonify({
                "success": True,
                "message": message,
                "trajectory_string": dsv_string
            })
        else:
            return jsonify({"success": False, "message": message}), 400
            
    except Exception as e:
        return jsonify({"success": False, "message": f"Server Error during calculation: {str(e)}"}), 500

@app.route('/api/upload_and_start', methods=['POST'])
def api_upload_and_start():
    """Sends the calculated trajectory data to the ESP32."""
    data = request.json
    trajectory_string = data.get('trajectory_string')
    
    if not trajectory_string:
        return jsonify({"success": False, "message": "Missing trajectory string."}), 400
        
    try:
        # 1. Send Trajectory Start Command
        arduino.send_command("CMD:START_TRAJ")
        time.sleep(0.5) # Give Uno a moment
        
        # 2. Sync Time (Current UTC Unix Epoch)
        now_epoch = int(time.time())
        arduino.send_command(f"TIME:{now_epoch}")
        
        # 3. Send Data
        arduino.send_command(f"DATA:{trajectory_string}")
        
        return jsonify({
            "success": True,
            "message": "Trajectory sent to Arduino via Serial."
        })
            
    except Exception as e:
        return jsonify({"success": False, "message": f"Serial Error: {str(e)}"}), 500

@app.route('/api/command', methods=['POST'])
def api_command():
    """Sends simple commands (HOME/STOP) to the ESP32."""
    data = request.json
    command = data.get('cmd')
    
    if command not in ["HOME", "STOP"]:
        return jsonify({"success": False, "message": "Invalid command."}), 400

    try:
        # Send command to Arduino
        full_cmd = f"CMD:{command}"
        success = arduino.send_command(full_cmd)
        
        if success:
            return jsonify({
                "success": True,
                "message": f"Command '{command}' sent to Arduino."
            })
        else:
            return jsonify({"success": False, "message": "Failed to write to Serial port."}), 500
            
    except Exception as e:
        return jsonify({"success": False, "message": f"Error: {str(e)}"}), 503

@app.route('/api/status', methods=['GET'])
def api_status():
    """Polls the ESP32 for the current system status, clock, and position."""
    return jsonify(system_status)

# Run the Flask app
if __name__ == '__main__':
    # Ensure the required index.html exists when running locally
    if not os.path.exists("templates/index.html"):
        print("FATAL: index.html is missing. Cannot run server.")
    else:
        # Use debug mode for development
        app.run(debug=True, host='0.0.0.0', port=5020)
