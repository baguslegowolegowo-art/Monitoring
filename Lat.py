import serial
import matplotlib.pyplot as plt
import time

# Sesuaikan port serial ESP32 kamu di sini
PORT = "COM3"
BAUD = 115200

print(f"Menghubungkan ke port {PORT}...")
try:                                                          
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(2)
except Exception as e:
    print(f"Gagal membuka port serial: {e}")
    exit()

# Aktifkan interactive mode matplotlib
plt.ion()

# --- Jendela 1: Sensor DHT22 (Suhu & Kelembapan Udara) ---
fig_dht, (ax_temp, ax_hum) = plt.subplots(2, 1, figsize=(7, 6))
fig_dht.canvas.manager.set_window_title("Monitoring DHT22 - Suhu & Kelembapan Udara")

# --- Jendela 2: Sensor Soil Moisture (Kelembapan Tanah) ---
fig_soil, ax_soil = plt.subplots(1, 1, figsize=(7, 4))
fig_soil.canvas.manager.set_window_title("Monitoring Sensor Kelembapan Tanah (Soil)")

# Menampung data
x_data = []
temp_list, hum_list = [], []
soil_list = []
max_len = 30 # Maksimal titik data yang ditampilkan di grafik

counter = 0

print("Mulai membaca data sensor... (Tekan Ctrl+C untuk berhenti)")

try:
    while True:
        if ser.in_waiting > 0:
            # Tambahkan errors='ignore' untuk mengabaikan byte rusak dari serial
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                try:
                    # Pecah data berdasarkan koma: [temp, humidity, soil_percent]
                    parts = line.split(',')
                    if len(parts) == 3:
                        temp = float(parts[0])
                        hum = float(parts[1])
                        soil = float(parts[2])
                        
                        counter += 1
                        x_data.append(counter)
                        temp_list.append(temp)
                        hum_list.append(hum)
                        soil_list.append(soil)
                        
                        # Geser data jika melebihi batas maksimal
                        if len(x_data) > max_len:
                            x_data.pop(0)
                            temp_list.pop(0)
                            hum_list.pop(0)
                            soil_list.pop(0)
                        
                        # === Update Grafik DHT22 ===
                        ax_temp.clear()
                        ax_temp.plot(x_data, temp_list, 'r-o', label='Suhu Udara (°C)')
                        ax_temp.set_ylabel('Suhu (°C)')
                        ax_temp.set_title('DHT22 - Suhu Udara')
                        ax_temp.grid(True)
                        ax_temp.legend(loc='upper left')
                        
                        ax_hum.clear()
                        ax_hum.plot(x_data, hum_list, 'b-o', label='Kelembapan Udara (%)')
                        ax_hum.set_xlabel('Sampel Waktu')
                        ax_hum.set_ylabel('Kelembapan (%)')
                        ax_hum.set_title('DHT22 - Kelembapan Udara')
                        ax_hum.grid(True)
                        ax_hum.legend(loc='upper left')
                        
                        # === Update Grafik Soil Moisture ===
                        ax_soil.clear()
                        ax_soil.plot(x_data, soil_list, 'g-s', label='Kelembapan Tanah (%)')
                        ax_soil.set_xlabel('Sampel Waktu')
                        ax_soil.set_ylabel('Kelembapan (%)')
                        ax_soil.set_title('Sensor Soil Moisture - Kelembapan Tanah')
                        ax_soil.grid(True)
                        ax_soil.legend(loc='upper left')
                        
                        # Render ulang grafik secara realtime
                        fig_dht.canvas.draw()
                        fig_dht.canvas.flush_events()
                        fig_soil.canvas.draw()
                        fig_soil.canvas.flush_events()
                        
                        print(f"[Data #{counter}] DHT22 -> Suhu: {temp}°C, Lembab Udara: {hum}% | Soil: {soil:.1f}%")
                        
                except ValueError:
                    pass
except KeyboardInterrupt:
    print("\nProgram dihentikan oleh pengguna.")
    ser.close()