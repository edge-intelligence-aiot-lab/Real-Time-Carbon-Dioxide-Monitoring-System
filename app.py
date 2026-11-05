from datetime import datetime, timezone
import os
from pathlib import Path
import math
from contextlib import closing

import mysql.connector
from dotenv import load_dotenv

from flask import Flask, jsonify, render_template, request

load_dotenv(Path(__file__).with_name(".env"))
app = Flask(__name__)

# Units are included in the field names. Only inlet CO2 is required.
READING_LIMITS = {
    "co2_ppm": (0, None),
    "outlet_co2_ppm": (0, None),
    "inlet_temperature_c": (-273.15, None),
    "outlet_temperature_c": (-273.15, None),
    "inlet_humidity_rh": (0, 100),
    "outlet_humidity_rh": (0, 100),
    "inlet_particulate_ug_m3": (0, None),
    "outlet_particulate_ug_m3": (0, None),
    "mass_g": (0, None),
}
READING_COLUMNS = ", ".join(READING_LIMITS)



def connect_database():
    return mysql.connector.connect(
        host=os.getenv("DB_HOST", "127.0.0.1"),
        port=int(os.getenv("DB_PORT", "3306")),
        user=os.environ["DB_USER"],
        password=os.environ["DB_PASSWORD"],
        database=os.getenv("DB_NAME", "co2_monitoring"),
        connection_timeout=5,
    )


def format_reading(row):
    return {
        "time": row["recorded_at"].replace(tzinfo=timezone.utc).isoformat(timespec="seconds"),
        **{field: row[field] for field in READING_LIMITS},
    }


@app.errorhandler(mysql.connector.Error)
def database_error(error):
    app.logger.error("Database operation failed: %s", error)
    return jsonify(error="Database unavailable. Please try again."), 503


@app.get("/")
def home():
    return render_template("index.html")


@app.get("/api/readings/latest")
def latest_reading():
    with closing(connect_database()) as connection:
        with closing(connection.cursor(dictionary=True)) as cursor:
            cursor.execute(f"SELECT {READING_COLUMNS}, recorded_at FROM readings ORDER BY id DESC LIMIT 1")
            row = cursor.fetchone()
    if row is None:
        return jsonify(error="No readings received yet."), 404
    return jsonify(format_reading(row))


@app.get("/api/readings")
def readings():
    with closing(connect_database()) as connection:
        with closing(connection.cursor(dictionary=True)) as cursor:
            cursor.execute(f"SELECT {READING_COLUMNS}, recorded_at FROM readings ORDER BY id DESC LIMIT 1000")
            rows = cursor.fetchall()
    return jsonify([format_reading(row) for row in reversed(rows)])


@app.post("/api/readings")
def receive_reading():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify(error="Send a JSON object containing co2_ppm."), 400

    for field, (minimum, maximum) in READING_LIMITS.items():
        value = payload.get(field)
        if field != "co2_ppm" and value is None:
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return jsonify(error=f"{field} must be a number."), 400
        try:
            valid = math.isfinite(value) and value >= minimum
            if maximum is not None:
                valid = valid and value <= maximum
        except OverflowError:
            valid = False
        if not valid:
            return jsonify(error=f"{field} must be finite and within its valid range ({minimum} to {maximum if maximum is not None else 'no upper limit'})."), 400

    reading = {field: payload.get(field) for field in READING_LIMITS}
    reading["recorded_at"] = datetime.now(timezone.utc).replace(tzinfo=None, microsecond=0)
    placeholders = ", ".join(["%s"] * len(reading))
    with closing(connect_database()) as connection:
        with closing(connection.cursor()) as cursor:
            cursor.execute(
                f"INSERT INTO readings ({READING_COLUMNS}, recorded_at) VALUES ({placeholders})",
                tuple(reading.values()),
            )
        connection.commit()
    return jsonify(format_reading(reading)), 201
