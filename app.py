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
        "co2_ppm": row["co2_ppm"],
        "outlet_co2_ppm": row["outlet_co2_ppm"],
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
            cursor.execute("SELECT co2_ppm, outlet_co2_ppm, recorded_at FROM readings ORDER BY id DESC LIMIT 1")
            row = cursor.fetchone()
    if row is None:
        return jsonify(error="No readings received yet."), 404
    return jsonify(format_reading(row))


@app.get("/api/readings")
def readings():
    with closing(connect_database()) as connection:
        with closing(connection.cursor(dictionary=True)) as cursor:
            cursor.execute("SELECT co2_ppm, outlet_co2_ppm, recorded_at FROM readings ORDER BY id DESC LIMIT 1000")
            rows = cursor.fetchall()
    return jsonify([format_reading(row) for row in reversed(rows)])


@app.post("/api/readings")
def receive_reading():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify(error="Send a JSON object containing co2_ppm."), 400

    for field in ("co2_ppm", "outlet_co2_ppm"):
        value = payload.get(field)
        if field == "outlet_co2_ppm" and value is None:
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return jsonify(error=f"{field} must be a number."), 400
        try:
            valid = math.isfinite(value) and value >= 0
        except OverflowError:
            valid = False
        if not valid:
            return jsonify(error=f"{field} must be finite and nonnegative."), 400

    co2_ppm = payload["co2_ppm"]
    outlet_co2_ppm = payload.get("outlet_co2_ppm")
    recorded_at = datetime.now(timezone.utc).replace(tzinfo=None, microsecond=0)
    with closing(connect_database()) as connection:
        with closing(connection.cursor()) as cursor:
            cursor.execute(
                "INSERT INTO readings (co2_ppm, outlet_co2_ppm, recorded_at) VALUES (%s, %s, %s)",
                (co2_ppm, outlet_co2_ppm, recorded_at),
            )
        connection.commit()
    return jsonify(format_reading({
        "co2_ppm": co2_ppm,
        "outlet_co2_ppm": outlet_co2_ppm,
        "recorded_at": recorded_at,
    })), 201
