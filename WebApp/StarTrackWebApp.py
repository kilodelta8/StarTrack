
# By John Durham @kilodelta8
# 10/27/2025
# CIS 2427 IoT Fundamentals


import os
import time
import requests
import json
from flask import Flask, render_template_string, request, jsonify
from skyfield.api import load, EarthSatellite, Topos, utc

app = Flask(__name__)

# --- CONFIGURATION ---
# IMPORTANT: Replace with the IP address of your ESP32 module when deployed!
ESP32_IP_ADDRESS = "192.168.4.1" 
ESP32_BASE_URL = f"http://{ESP32_IP_ADDRESS}"

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
    return render_template_string(open("index.html").read())

@app.route('/api/calculate', methods=['POST'])
def api_calculate():
    """Calculates the trajectory and returns the DSV string."""
    try:
        data = request.json
        # Extract location and TLE (using mock data for now)
        lat = data.get('latitude', DEFAULT_LAT)
        lon = data.get('longitude', DEFAULT_LON)
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
        # 1. Send the data to the ESP32's upload endpoint
        response = requests.post(
            f"{ESP32_BASE_URL}/upload_trajectory",
            data=trajectory_string,
            headers={'Content-Type': 'text/plain'},
            timeout=5 # Timeout after 5 seconds
        )

        if response.status_code == 200:
            return jsonify({
                "success": True,
                "message": "Trajectory successfully uploaded and tracking initiated on ESP32.",
                "esp32_response": response.text
            })
        else:
            return jsonify({
                "success": False, 
                "message": f"ESP32 Upload Failed. Status Code: {response.status_code}",
                "esp32_response": response.text
            }), 502 # Bad Gateway
            
    except requests.exceptions.RequestException as e:
        return jsonify({"success": False, "message": f"Error connecting to ESP32 at {ESP32_IP_ADDRESS}: {str(e)}"}), 503 # Service Unavailable

@app.route('/api/command', methods=['POST'])
def api_command():
    """Sends simple commands (HOME/STOP) to the ESP32."""
    data = request.json
    command = data.get('cmd')
    
    if command not in ["HOME", "STOP"]:
        return jsonify({"success": False, "message": "Invalid command."}), 400

    try:
        # Send the command to the ESP32's command endpoint
        response = requests.post(
            f"{ESP32_BASE_URL}/command",
            json={"cmd": command},
            timeout=5
        )
        
        if response.status_code == 200:
            return jsonify({
                "success": True,
                "message": f"Command '{command}' sent successfully.",
                "esp32_response": response.text
            })
        else:
            return jsonify({
                "success": False, 
                "message": f"ESP32 Command Failed. Status Code: {response.status_code}",
                "esp32_response": response.text
            }), 502 
            
    except requests.exceptions.RequestException as e:
        return jsonify({"success": False, "message": f"Error connecting to ESP32 at {ESP32_IP_ADDRESS}: {str(e)}"}), 503

@app.route('/api/status', methods=['GET'])
def api_status():
    """Polls the ESP32 for the current system status, clock, and position."""
    try:
        response = requests.get(f"{ESP32_BASE_URL}/status", timeout=2)
        
        if response.status_code == 200:
            # ESP32 returns JSON containing 'status', 'epoch_time', and optionally 'az'/'el'
            return jsonify(response.json())
        else:
            return jsonify({"status": "OFFLINE", "message": f"ESP32 status error: {response.status_code}"}), 502
            
    except requests.exceptions.RequestException:
        # If connection fails, assume offline
        return jsonify({"status": "OFFLINE", "message": f"Could not reach ESP32 at {ESP32_IP_ADDRESS}"}), 503

# Run the Flask app
if __name__ == '__main__':
    # Ensure the required index.html exists when running locally
    if not os.path.exists("index.html"):
        print("FATAL: index.html is missing. Cannot run server.")
    else:
        # Use debug mode for development
        app.run(debug=True, host='0.0.0.0', port=5000)
