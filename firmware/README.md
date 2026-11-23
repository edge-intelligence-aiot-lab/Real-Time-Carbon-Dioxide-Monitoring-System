# ESP-IDF CO₂ test nodes

Use the same project on both boards. Select **Inlet** on one and **Outlet** on the other. The nodes send fixed test readings of 800 and 560 ppm every five seconds. These values test Wi-Fi, Flask, database storage, and the dashboard before wiring the sensors.

1. Start the database and run Flask from the repository root:

   ```powershell
   .\.venv\Scripts\python.exe -m flask --app app run --host 0.0.0.0
   ```

2. Find the computer's LAN IPv4 address with `ipconfig`. Put the ESP32 boards and the computer on the same trusted Wi-Fi network. The device URL is `http://<computer-LAN-IP>:5000/api/device-readings`.
3. Open an ESP-IDF terminal in this firmware folder. Use the following target if the module is an original ESP32 such as ESP32-WROOM-32. Choose the actual chip target for another module.

   ```powershell
   idf.py set-target esp32
   idf.py menuconfig
   ```

4. In **CO2 monitor node**, select the role and enter the Wi-Fi name, password, and device URL. Keep the generated local `sdkconfig` private. Build and flash the first board, substituting its COM port:

   ```powershell
   idf.py build
   idf.py -p COM3 flash monitor
   ```

5. Exit the serial monitor with **Ctrl+]**. Change the role in `menuconfig`, then build and flash the second board using its port. Keep both boards powered. HTTP 202 means Flask is waiting for the other side. HTTP 201 means it stored a matched pair. The dashboard should show 800 ppm inlet, 560 ppm outlet, and 30% reduction.

Flask matches one pending reading from each side if they arrive within 15 seconds. Pending readings are held in the Flask process, so use one Flask process for this local test. Each stored pair consumes both readings. The dashboard shows the last saved pair while it waits for a newer one.
