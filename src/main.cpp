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
// CONFIGURACIÓN DE IDENTIFICACIÓN DE SENSORES
// -----------------------------
// Pin ADC para leer el divisor de voltaje (empezamos con 1 puerto)
const int PIN_ID_SENSOR = 34; // Puerto RJ45 #1

// Pines para el DHT11 cuando se detecte
const int PIN_DHT_DATOS = 4;

// Rangos de voltaje para identificación (en mV)
// Con alimentación de 5V y divisor 330Ω + 330Ω = ~2500mV (2.5V nominal)
// Tolerancia: ±10% por variación en resistencias y voltaje de alimentación
const int VOLTAJE_MIN_DHT11 = 2100;  // Mínimo 2.1V (margen amplio)
const int VOLTAJE_MAX_DHT11 = 2900;  // Máximo 2.9V (margen amplio)
const int VOLTAJE_SIN_SENSOR = 300;   // Menor a 0.3V = sin sensor

// Estado del sensor
String tipoSensorActual = "NINGUNO";
DHT* sensorDHT = nullptr;
bool sensorActivo = false;

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

String ssidWiFi = "";
String contrasenaWiFi = "";
bool credencialesGuardadas = false;
String nombreDispositivoBLE;
BLECharacteristic *caracteristicaBLEEstado;

// -----------------------------
// DECLARACIÓN DE FUNCIONES
// -----------------------------
void detectarSensor();
String identificarSensor();
int leerVoltajeADC();
void configurarDHT11();
void responderPaginaInicio();
void responderDatosSensor();
void responderVoltaje();
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
            return;
        }
        contrasenaWiFi = pCaracteristica->getValue().c_str();
        Serial.println("Contraseña recibida");

        if (ssidWiFi != "" && contrasenaWiFi != "") {
            if (intentarConexionWiFi()) {
                credencialesGuardadas = true;
                String ip = WiFi.localIP().toString();
                caracteristicaBLEEstado->setValue(("IP:" + ip).c_str());
                caracteristicaBLEEstado->notify();
                configurarServidorWeb();
            } else {
                caracteristicaBLEEstado->setValue("Error WiFi");
                caracteristicaBLEEstado->notify();
            }
        }
    }
};

// -----------------------------
// CONFIGURACIÓN INICIAL
// -----------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=================================");
    Serial.println("SISTEMA DE DETECCIÓN DE SENSORES");
    Serial.println("=================================\n");

    // Configurar pin de identificación como entrada
    pinMode(PIN_ID_SENSOR, INPUT);
    
    if (!oledPantalla.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("Error al iniciar OLED");
        while (true);
    }

    oledPantalla.clearDisplay();
    oledPantalla.setTextSize(1);
    oledPantalla.setTextColor(SSD1306_WHITE);
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("Sistema Sensor");
    oledPantalla.println("Detectando...");
    oledPantalla.display();

    // Detectar sensor inicial
    Serial.println("Leyendo voltaje del divisor...");
    delay(500);
    detectarSensor();

    // Configurar BLE
    String mac = WiFi.macAddress();
    nombreDispositivoBLE = "ESP32_Sensor_" + mac.substring(mac.length() - 5);
    nombreDispositivoBLE.replace(":", "");

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
    BLEDevice::startAdvertising();

    Serial.println("\nBLE iniciado: " + nombreDispositivoBLE);
    
    oledPantalla.clearDisplay();
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("BLE Activo");
    oledPantalla.println(nombreDispositivoBLE);
    oledPantalla.println("");
    oledPantalla.println("Sensor: " + tipoSensorActual);
    oledPantalla.display();
}

// -----------------------------
// BUCLE PRINCIPAL
// -----------------------------
void loop() {
    // Detectar cambios en sensor cada 3 segundos
    static unsigned long ultimaDeteccion = 0;
    if (millis() - ultimaDeteccion > 3000) {
        detectarSensor();
        ultimaDeteccion = millis();
    }

    if (credencialesGuardadas) {
        servidorWeb.handleClient();
        if (WiFi.status() != WL_CONNECTED) {
            if (!intentarConexionWiFi()) {
                credencialesGuardadas = false;
            }
        }
    }
    delay(100);
}

// -----------------------------
// FUNCIONES DE DETECCIÓN DE SENSORES
// -----------------------------
void detectarSensor() {
    String tipoDetectado = identificarSensor();
    
    if (tipoDetectado != tipoSensorActual) {
        Serial.println("\n--- CAMBIO DE SENSOR DETECTADO ---");
        Serial.println("Anterior: " + tipoSensorActual);
        Serial.println("Nuevo: " + tipoDetectado);
        
        // Limpiar sensor anterior
        if (sensorDHT != nullptr) {
            delete sensorDHT;
            sensorDHT = nullptr;
        }
        
        tipoSensorActual = tipoDetectado;
        sensorActivo = false;
        
        // Configurar nuevo sensor
        if (tipoDetectado == "DHT11") {
            configurarDHT11();
            sensorActivo = true;
            Serial.println("DHT11 configurado y activo");
        }
        
        // Actualizar pantalla
        oledPantalla.clearDisplay();
        oledPantalla.setCursor(0, 0);
        
        if (credencialesGuardadas) {
            oledPantalla.println("WiFi OK");
            oledPantalla.println("IP:" + WiFi.localIP().toString().substring(0,15));
        } else {
            oledPantalla.println("BLE: " + nombreDispositivoBLE.substring(0,15));
        }
        
        oledPantalla.println("");
        oledPantalla.print("Sensor: ");
        oledPantalla.println(tipoSensorActual);
        
        int voltaje = leerVoltajeADC();
        oledPantalla.print("V: ");
        oledPantalla.print(voltaje);
        oledPantalla.println(" mV");
        
        oledPantalla.display();
    }
}

String identificarSensor() {
    int voltajeMV = leerVoltajeADC();
    
    // Mostrar voltaje en Serial cada vez
    Serial.print("Voltaje leído: ");
    Serial.print(voltajeMV);
    Serial.print(" mV (");
    Serial.print(voltajeMV / 1000.0, 2);
    Serial.println(" V)");
    
    // Identificar según rango
    if (voltajeMV < VOLTAJE_SIN_SENSOR) {
        Serial.println("  -> Sin sensor conectado");
        return "NINGUNO";
    } 
    else if (voltajeMV >= VOLTAJE_MIN_DHT11 && voltajeMV <= VOLTAJE_MAX_DHT11) {
        Serial.println("  -> DHT11 detectado!");
        return "DHT11";
    } 
    else {
        Serial.println("  -> Voltaje desconocido");
        Serial.println("  -> Ajusta los rangos en el código");
        return "DESCONOCIDO";
    }
}

int leerVoltajeADC() {
    // Hacer múltiples lecturas para mayor precisión
    long suma = 0;
    const int NUM_LECTURAS = 10;
    
    for (int i = 0; i < NUM_LECTURAS; i++) {
        suma += analogRead(PIN_ID_SENSOR);
        delay(10);
    }
    
    int valorADC = suma / NUM_LECTURAS;
    
    // Convertir a milivoltios
    // Si el ADC está saturado (>4000), el voltaje real es mayor a 3.3V
    int voltajeMV = (valorADC * 3300) / 4095;
    
    return voltajeMV;
}

void configurarDHT11() {
    sensorDHT = new DHT(PIN_DHT_DATOS, DHT11);
    sensorDHT->begin();
    delay(2000); // DHT11 necesita tiempo para inicializar
}

// -----------------------------
// FUNCIONES DEL SERVIDOR WEB
// -----------------------------
void configurarServidorWeb() {
    servidorWeb.on("/", responderPaginaInicio);
    servidorWeb.on("/datos", responderDatosSensor);
    servidorWeb.on("/voltaje", responderVoltaje);
    servidorWeb.begin();
    Serial.println("\n=== Servidor web iniciado ===");
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("Prueba:");
    Serial.println("  http://" + WiFi.localIP().toString() + "/");
    Serial.println("  http://" + WiFi.localIP().toString() + "/datos");
    Serial.println("  http://" + WiFi.localIP().toString() + "/voltaje");
}

void responderPaginaInicio() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>ESP32 Sensor Hub</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
    html += ".card{background:white;padding:20px;margin:10px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;}a{color:#007bff;text-decoration:none;font-size:18px;display:block;margin:10px 0;}";
    html += "a:hover{text-decoration:underline;}</style></head><body>";
    html += "<div class='card'><h1>🔌 ESP32 Sensor Hub</h1>";
    html += "<p><strong>Sensor detectado:</strong> " + tipoSensorActual + "</p>";
    html += "<p><strong>Estado:</strong> " + String(sensorActivo ? "Activo ✅" : "Inactivo ❌") + "</p>";
    html += "</div><div class='card'>";
    html += "<h2>📊 Endpoints disponibles:</h2>";
    html += "<a href='/datos'>📈 Ver datos del sensor</a>";
    html += "<a href='/voltaje'>⚡ Ver voltaje del divisor</a>";
    html += "</div></body></html>";
    servidorWeb.send(200, "text/html", html);
}

void responderDatosSensor() {
    String json = "{";
    json += "\"sensor\": \"" + tipoSensorActual + "\",";
    json += "\"activo\": " + String(sensorActivo ? "true" : "false");
    
    if (sensorActivo && tipoSensorActual == "DHT11" && sensorDHT != nullptr) {
        float temp = sensorDHT->readTemperature();
        float hum = sensorDHT->readHumidity();
        
        if (!isnan(temp) && !isnan(hum)) {
            json += ",\"temperatura\": " + String(temp, 1);
            json += ",\"humedad\": " + String(hum, 1);
            json += ",\"unidades\": {\"temperatura\": \"°C\", \"humedad\": \"%\"}";
        } else {
            json += ",\"error\": \"Error al leer DHT11\"";
        }
    } else if (!sensorActivo) {
        json += ",\"mensaje\": \"No hay sensor activo\"";
    }
    
    json += "}";
    servidorWeb.send(200, "application/json", json);
}

void responderVoltaje() {
    int voltaje = leerVoltajeADC();
    String json = "{";
    json += "\"voltaje_mv\": " + String(voltaje);
    json += ",\"voltaje_v\": " + String(voltaje / 1000.0, 3);
    json += ",\"sensor_detectado\": \"" + tipoSensorActual + "\"";
    json += ",\"rango_dht11\": {\"min\": " + String(VOLTAJE_MIN_DHT11) + ", \"max\": " + String(VOLTAJE_MAX_DHT11) + "}";
    json += "}";
    servidorWeb.send(200, "application/json", json);
}

bool intentarConexionWiFi() {
    WiFi.disconnect(true);
    WiFi.begin(ssidWiFi.c_str(), contrasenaWiFi.c_str());
    
    Serial.print("\nConectando a WiFi");
    oledPantalla.clearDisplay();
    oledPantalla.setCursor(0, 0);
    oledPantalla.println("Conectando WiFi...");
    oledPantalla.println(ssidWiFi);
    oledPantalla.display();
    
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 20) {
        delay(500);
        Serial.print(".");
        intentos++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi conectado!");
        Serial.println("IP: " + WiFi.localIP().toString());
        return true;
    } else {
        Serial.println("\n❌ Error al conectar WiFi");
        return false;
    }
}