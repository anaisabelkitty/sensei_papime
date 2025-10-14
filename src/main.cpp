#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// -----------------------------
// CONFIGURACIÓN DE PANTALLA OLED
// -----------------------------
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
#define OLED_RESET -1
Adafruit_SSD1306 oledPantalla(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, OLED_RESET);

// -----------------------------
// CONFIGURACIÓN DE PINES Y SENSORES
// -----------------------------
#define PIN_DHT 4
#define TIPO_DHT DHT11
const int PIN_TRIG = 5;
const int PIN_ECHO = 18;

DHT dhtSensor(PIN_DHT, TIPO_DHT);

// -----------------------------
// CONFIGURACIÓN DEL SERVIDOR WEB
// -----------------------------
WebServer servidorWeb(8080);

// -----------------------------
// CONFIGURACIÓN BLE
// -----------------------------
#define UUID_SERVICIO "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define UUID_CARACT_SSID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define UUID_CARACT_PASS "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define UUID_CARACT_ESTADO "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// Variables globales
String ssidWiFi = "";
String contrasenaWiFi = "";
bool credencialesGuardadas = false;
String nombreDispositivoBLE;

BLECharacteristic *caracteristicaBLEEstado;

// -----------------------------
// DECLARACIÓN DE FUNCIONES
// -----------------------------
void enviarDatosDHT();
void enviarDatosUltrasonico();
void responderPaginaInicio();
float obtenerDistanciaUltrasonico();
bool intentarConexionWiFi();
void configurarServidorWeb();

// -----------------------------
// CALLBACKS BLE
// -----------------------------
class ServidorBLECallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServidor) {
        Serial.println("Dispositivo conectado vía BLE");
        if (credencialesGuardadas) {
            String ip = WiFi.localIP().toString();
            caracteristicaBLEEstado->setValue(("IP:" + ip).c_str());
            Serial.println("Enviando IP por BLE: " + ip);
        }
    }

    void onDisconnect(BLEServer *pServidor) {
        Serial.println("Dispositivo desconectado, reiniciando publicidad BLE");
        BLEDevice::startAdvertising();
    }
};

class SSIDCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCaracteristica) {
        if (credencialesGuardadas) {
            String ip = WiFi.localIP().toString();
            caracteristicaBLEEstado->setValue(("IP:" + ip).c_str());
            caracteristicaBLEEstado->notify();
            Serial.println("Ya está configurado, enviando IP: " + ip);
            delay(100);
            return;
        }
        ssidWiFi = pCaracteristica->getValue().c_str();
        Serial.println("SSID recibido: " + ssidWiFi);
    }
};

class PasswordCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCaracteristica) {
        if (credencialesGuardadas) {
            String ip = WiFi.localIP().toString();
            caracteristicaBLEEstado->setValue(("IP:" + ip).c_str());
            caracteristicaBLEEstado->notify();
            Serial.println("Ya está configurado, enviando IP: " + ip);
            delay(100);
            return;
        }

        contrasenaWiFi = pCaracteristica->getValue().c_str();
        Serial.println("Contraseña recibida: " + contrasenaWiFi);

        if (ssidWiFi != "" && contrasenaWiFi != "") {
            if (intentarConexionWiFi()) {
                credencialesGuardadas = true;
                String ip = WiFi.localIP().toString();
                caracteristicaBLEEstado->setValue(("IP:" + ip).c_str());
                caracteristicaBLEEstado->notify();
                Serial.println("WiFi conectado, IP: " + ip);
                delay(100);
                configurarServidorWeb();
            } else {
                String error = "Error WiFi";
                caracteristicaBLEEstado->setValue(error.c_str());
                caracteristicaBLEEstado->notify();
                Serial.println("Fallo la conexión WiFi");
                oledPantalla.clearDisplay();
                oledPantalla.setCursor(0, 0);
                oledPantalla.println("Error WiFi");
                oledPantalla.println("Ver credenciales");
                oledPantalla.display();
                delay(100);
            }
        }
    }
};

// -----------------------------
// CONFIGURACIÓN INICIAL
// -----------------------------
void setup() {
    Serial.begin(115200);
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    dhtSensor.begin();
    delay(1000);

    if (!oledPantalla.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("Error al iniciar OLED");
        while (true);
    }

    oledPantalla.clearDisplay();
    oledPantalla.setTextSize(1);
    oledPantalla.setTextColor(SSD1306_WHITE);
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("Iniciando sistema...");
    oledPantalla.display();

    String mac = WiFi.macAddress();
    nombreDispositivoBLE = "ESP32_Sensor_" + mac.substring(mac.length() - 5);
    nombreDispositivoBLE.replace(":", "");
    Serial.println("Nombre BLE: " + nombreDispositivoBLE);

    BLEDevice::init(nombreDispositivoBLE.c_str());
    BLEServer *servidorBLE = BLEDevice::createServer();
    servidorBLE->setCallbacks(new ServidorBLECallbacks());
    BLEService *servicioBLE = servidorBLE->createService(UUID_SERVICIO);

    BLECharacteristic *caractSSID = servicioBLE->createCharacteristic(UUID_CARACT_SSID, BLECharacteristic::PROPERTY_WRITE);
    caractSSID->setCallbacks(new SSIDCallback());

    BLECharacteristic *caractPASS = servicioBLE->createCharacteristic(UUID_CARACT_PASS, BLECharacteristic::PROPERTY_WRITE);
    caractPASS->setCallbacks(new PasswordCallback());

    caracteristicaBLEEstado = servicioBLE->createCharacteristic(UUID_CARACT_ESTADO, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    caracteristicaBLEEstado->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2902)));
    caracteristicaBLEEstado->setValue("Esperando datos");

    servicioBLE->start();
    BLEAdvertising *publicidad = BLEDevice::getAdvertising();
    publicidad->addServiceUUID(UUID_SERVICIO);
    publicidad->setScanResponse(true);
    publicidad->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    Serial.println("BLE listo");
    oledPantalla.clearDisplay();
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("Conecta via BLE");
    oledPantalla.println(nombreDispositivoBLE);
    oledPantalla.display();
}

// -----------------------------
// BUCLE PRINCIPAL
// -----------------------------
void loop() {
    if (credencialesGuardadas) {
        servidorWeb.handleClient();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi desconectado, reintentando...");
            if (!intentarConexionWiFi()) {
                credencialesGuardadas = false;
                caracteristicaBLEEstado->setValue("WiFi desconectado");
                caracteristicaBLEEstado->notify();
                oledPantalla.clearDisplay();
                oledPantalla.setCursor(0, 0);
                oledPantalla.println("WiFi desconectado");
                oledPantalla.println("Reconfigura BLE");
                oledPantalla.display();
                delay(100);
            }
        }
    }
    delay(100);
}

// -----------------------------
// FUNCIONES DEL SERVIDOR WEB
// -----------------------------
void configurarServidorWeb() {
    Serial.println("Iniciando servidor web...");
    servidorWeb.on("/inicio", responderPaginaInicio);
    servidorWeb.on("/sensor/dht", enviarDatosDHT);
    servidorWeb.on("/sensor/ultrasonico", enviarDatosUltrasonico);
    servidorWeb.begin();
    Serial.println("Servidor iniciado en IP: " + WiFi.localIP().toString());

    oledPantalla.clearDisplay();
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("WiFi conectado");
    oledPantalla.print("IP: ");
    oledPantalla.println(WiFi.localIP().toString());
    oledPantalla.display();
}

void responderPaginaInicio() {
    servidorWeb.send(200, "text/plain", "Servidor ESP32 activo. Usa /sensor/dht o /sensor/ultrasonico.");
}

void enviarDatosDHT() {
    float humedad = dhtSensor.readHumidity();
    float temperatura = dhtSensor.readTemperature();
    if (isnan(humedad) || isnan(temperatura)) {
        servidorWeb.send(500, "application/json", "{\"error\": \"Error DHT11\"}");
        return;
    }
    String json = "{\"temperatura\": " + String(temperatura) + ", \"humedad\": " + String(humedad) + "}";
    servidorWeb.send(200, "application/json", json);
}

void enviarDatosUltrasonico() {
    float distancia = obtenerDistanciaUltrasonico();
    if (distancia == -1) {
        servidorWeb.send(500, "application/json", "{\"error\": \"Error ultrasónico\"}");
        return;
    }
    String json = "{\"distancia\": " + String(distancia) + "}";
    servidorWeb.send(200, "application/json", json);
}

float obtenerDistanciaUltrasonico() {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    int duracion = pulseIn(PIN_ECHO, HIGH);
    if (duracion == 0) return -1;
    return duracion * 0.0343 / 2;
}

bool intentarConexionWiFi() {
    WiFi.disconnect(true);
    WiFi.begin(ssidWiFi.c_str(), contrasenaWiFi.c_str());
    Serial.print("Conectando a WiFi: " + ssidWiFi + "...");
    oledPantalla.clearDisplay();
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("Conectando WiFi");
    oledPantalla.println(ssidWiFi);
    oledPantalla.display();

    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 20) {
        delay(500);
        Serial.print(".");
        intentos++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConectado. IP: " + WiFi.localIP().toString());
        return true;
    } else {
        Serial.println("\nError al conectar WiFi");
        oledPantalla.clearDisplay();
        oledPantalla.setCursor(0, 0);
        oledPantalla.println("Error WiFi");
        oledPantalla.println("Ver credenciales");
        oledPantalla.display();
        return false;
    }
}
