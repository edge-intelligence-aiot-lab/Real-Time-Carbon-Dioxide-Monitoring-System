from flask import Flask, jsonify, render_template

app = Flask(__name__)

SIMULATED_READINGS = [
    {"time": "09:00", "co2_ppm": 480},
    {"time": "09:05", "co2_ppm": 520},
    {"time": "09:10", "co2_ppm": 590},
    {"time": "09:15", "co2_ppm": 650},
]


@app.get("/")
def home():
    return render_template("index.html")


@app.get("/api/readings/latest")
def latest_reading():
    return jsonify(SIMULATED_READINGS[-1])


@app.get("/api/readings")
def readings():
    return jsonify(SIMULATED_READINGS)