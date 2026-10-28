#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <AccelStepper.h>

// ===================== KONFIGURASI BLE =====================
#define DEVICE_NAME "WS_BT"
#define PIN_STATUS     2    // LED bawaan: kedip = menunggu koneksi, nyala = terhubung

// ===================== KONFIGURASI STEPPER =====================
#define MotorInterfaceType 4
// Urutan pin ESP32 ke driver ULN2003 (IN1, IN3, IN2, IN4 sesuai komentar kodenya)
AccelStepper myStepper(MotorInterfaceType, 16, 18, 17, 19);

const int stepsPerRevolution = 2048 / 2; // Sesuaikan dengan kebutuhan putaran (misal buka tutup)
#define ACTUATE_MS 8000UL               // Waktu penutup terbuka sebelum menutup kembali (ms)

// =======================================================

enum Bin { BIN_NONE = 0, BIN_ORGANIK, BIN_ANORGANIK };
enum StepperState { STATE_IDLE, STATE_OPENING, STATE_HOLDING, STATE_CLOSING };

BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

// Perintah dari callback BLE diserahkan ke loop() via spinlock
portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;
volatile char pendingCmd = 0;

char lastCmd = '0';          
Bin activeBin = BIN_NONE;
StepperState stepperState = STATE_IDLE;
unsigned long actionTimer = 0;

// ---------------------------------------------------------------------

const char *binName(Bin b) {
    switch (b) {
        case BIN_ORGANIK:   return "ORGANIK";
        case BIN_ANORGANIK: return "ANORGANIK";
        default:            return "-";
    }
}

void triggerAction(Bin b) {
    if (stepperState != STATE_IDLE) return; // Jangan tumpuk perintah jika sedang bergerak

    activeBin = b;
    stepperState = STATE_OPENING;
    
    if (b == BIN_ORGANIK) {
        Serial.println("Membuka Tempat Sampah ORGANIK...");
        myStepper.moveTo(stepsPerRevolution); // Putar ke kanan / buka
    } else if (b == BIN_ANORGANIK) {
        Serial.println("Membuka Tempat Sampah ANORGANIK...");
        myStepper.moveTo(-stepsPerRevolution); // Putar ke kiri / buka (balik arah jika perlu)
    }
}

void handleCommand(char cmd) {
    if (cmd == '1' || cmd == '2') {
        if (cmd != lastCmd) {
            triggerAction(cmd == '1' ? BIN_ORGANIK : BIN_ANORGANIK);
        }
    }
    lastCmd = cmd;
}

// ---------------------------------------------------------------------
// BLE Callbacks
// ---------------------------------------------------------------------

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        deviceConnected = true;
        Serial.println("Terhubung ke aplikasi!");
    }

    void onDisconnect(BLEServer *server) override {
        deviceConnected = false;
        Serial.println("Terputus dari aplikasi!");
    }
};

class CharCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        auto value = characteristic->getValue();
        if (value.length() == 0) return;
        char cmd = value[0];

        // Respon PING ('p') dari aplikasi
        if (cmd == 'p') {
            static uint8_t pong[] = {'x'};
            characteristic->setValue(pong, sizeof(pong));
            characteristic->notify();
            return;
        }

        // Perintah Kontrol ('0', '1', '2')
        if (cmd == '0' || cmd == '1' || cmd == '2') {
            portENTER_CRITICAL(&cmdMux);
            pendingCmd = cmd;
            portEXIT_CRITICAL(&cmdMux);
        }
    }
};

// ---------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    pinMode(PIN_STATUS, OUTPUT);

    // Konfigurasi Stepper
    myStepper.setMaxSpeed(1500.0);
    myStepper.setAcceleration(1000.0);

    // Inisialisasi BLE
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(BLEUUID((uint16_t)0x180C));
    pCharacteristic = pService->createCharacteristic(
        BLEUUID((uint16_t)0x2A56),
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    pCharacteristic->setCallbacks(new CharCallbacks());
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    BLEDevice::startAdvertising();

    Serial.println("BLE & Stepper Aktuator Siap. Menunggu koneksi...");
}

void loop() {
    const unsigned long now = millis();

    // 1) Ambil perintah terbaru dari callback BLE secara aman
    char cmd;
    portENTER_CRITICAL(&cmdMux);
    cmd = pendingCmd;
    pendingCmd = 0;
    portEXIT_CRITICAL(&cmdMux);

    if (cmd != 0) handleCommand(cmd);

    // Fail-safe: Jika BLE terputus, pastikan reset state jika diperlukan
    if (!deviceConnected) {
        lastCmd = '0';
    }

    // 2) Logika State Machine untuk Stepper (Non-blocking)
    switch (stepperState) {
        case STATE_IDLE:
            // Menunggu perintah dari BLE
            break;

        case STATE_OPENING:
            myStepper.run(); // Jalankan motor ke posisi buka
            if (myStepper.distanceToGo() == 0) {
                Serial.printf("Aktuator %s terbuka. Menahan posisi...\n", binName(activeBin));
                actionTimer = now;
                stepperState = STATE_HOLDING;
            }
            break;

        case STATE_HOLDING:
            myStepper.run(); // Biarkan motor menahan posisi
            // Tunggu hingga waktu ACTUATE_MS habis
            if (now - actionTimer >= ACTUATE_MS) {
                Serial.printf("Menutup kembali aktuator %s...\n", binName(activeBin));
                myStepper.moveTo(0); // Kembali ke posisi awal (0)
                stepperState = STATE_CLOSING;
            }
            break;

        case STATE_CLOSING:
            myStepper.run(); // Jalankan motor kembali ke posisi awal
            if (myStepper.distanceToGo() == 0) {
                Serial.println("Aktuator kembali ke posisi awal. Siap.");
                activeBin = BIN_NONE;
                stepperState = STATE_IDLE;
            }
            break;
    }

    // 3) Indikator LED Status: Kedip (1 Hz) saat menunggu, Menyala penuh saat terhubung
    digitalWrite(PIN_STATUS,
                 deviceConnected ? HIGH : ((now / 500) % 2 ? HIGH : LOW));

    // 4) Auto-reconnect advertising handling
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);  
        pServer->startAdvertising();
        Serial.println("Advertising ulang...");
        oldDeviceConnected = false;
    }
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
    }

    // Sedikit delay agar task background ESP32 (WiFi/BT stack) tidak crash
    delay(2);
}