import serial
import pyaudio
import time

COM_PORT = 'COM13'
BAUD = 115200
SAMPLE_RATE = 8000

ser = serial.Serial(COM_PORT, BAUD, timeout=0.1)

p = pyaudio.PyAudio()
stream = p.open(format=pyaudio.paUInt8,
                channels=1,
                rate=SAMPLE_RATE,
                output=True,
                frames_per_buffer=256)

print("=== HC-12 Ses Dinleme (4'lü paket) başladı ===")
print("Konuşmaya başla...")

buffer = bytearray()

try:
    while True:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            # 4'ün katı kadar veri birikirse oynat
            while len(buffer) >= 4:
                chunk = buffer[:256]          # max 256 byte oynat
                stream.write(bytes(chunk))
                buffer = buffer[256:]

        time.sleep(0.001)

except KeyboardInterrupt:
    print("\nKapatılıyor...")
finally:
    stream.stop_stream()
    stream.close()
    p.terminate()
    ser.close()