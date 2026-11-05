-- Schema for a new database. Existing local readings have already been migrated.
CREATE DATABASE IF NOT EXISTS co2_monitoring CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE co2_monitoring;
CREATE TABLE IF NOT EXISTS readings (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    co2_ppm DOUBLE NOT NULL,
    outlet_co2_ppm DOUBLE NULL,
    inlet_temperature_c DOUBLE NULL,
    outlet_temperature_c DOUBLE NULL,
    inlet_humidity_rh DOUBLE NULL,
    outlet_humidity_rh DOUBLE NULL,
    inlet_particulate_ug_m3 DOUBLE NULL,
    outlet_particulate_ug_m3 DOUBLE NULL,
    mass_g DOUBLE NULL,
    recorded_at DATETIME NOT NULL
);
