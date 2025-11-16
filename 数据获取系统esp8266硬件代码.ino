#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <RTClib.h>  // DS1307使用RTClib库（替代DS1302的专用库）
#include <PubSubClient.h>  // MQTT客户端库
#define DEVICE_ID "ESP006" 

// ==================== DS1307 RTC配置（I2C通信）====================
// ESP8266默认I2C引脚：SDA=D2，SCL=D1（无需额外定义，Wire库自动使用）
// 若需自定义引脚，需在Wire.begin()时指定，例如：Wire.begin(SDA_PIN, SCL_PIN)
RTC_DS1307 rtc;  // 创建DS1307对象
char timeStr[20];  // 存储格式化时间字符串

// ==================== 其他硬件配置 ====================
// WIFI 相关信息
const char* WIFINAME = "N6-506-ARM";
const char* WIFIPASSWORD = "armn6-506qwg";

// MQTT服务器相关信息
const char* mqtt_server = "192.168.3.102";
const char* mqtt_topic = "sensors/data";
const char* mqtt_clientId = "ESP8266_Sensor_Client";
const int mqtt_port = 1883;

// 传感器I2C地址
#define LIGHT_SENSOR_ADDR 0x23  // 光照传感器
#define BMP280_ADDRESS 0x76     // BMP280气压传感器

// ==================== 客户端对象 ====================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ==================== 传感器对象 ====================
Adafruit_AHTX0 aht;
Adafruit_Sensor *aht_humidity, *aht_temp;
Adafruit_BMP280 bmp;

// ==================== WIFI连接函数 ====================
void connectWifi(const char* wifiName, const char* wifiPassword, uint8_t waitTime) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(wifiName, wifiPassword);
    uint8_t count = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.printf("Connect WIFI...%ds\r\n", ++count);
        if (count >= waitTime) {
            Serial.println("Connect WIFI failed! Restarting...");
            delay(3000);
            ESP.restart();
        }
    }
    Serial.printf("WIFI connected! SSID: %s, IP: %s\r\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// ==================== MQTT重连函数 ====================
void reconnectMQTT() {
    int maxAttempts = 10;
    int attempts = 0;
    while (!mqttClient.connected() && attempts < maxAttempts) {
        Serial.printf("Attempt MQTT connection (%s@%s:%d)...\n", mqtt_clientId, mqtt_server, mqtt_port);
        if (mqttClient.connect(mqtt_clientId)) {
            Serial.println("MQTT connected successfully!");
        } else {
            Serial.printf("MQTT connect failed! RC: %d, Reason: ", mqttClient.state());
            switch(mqttClient.state()) {
                case -4: Serial.println("Connection timeout"); break;
                case -3: Serial.println("Connection lost"); break;
                case -2: Serial.println("Connect failed"); break;
                case 1: Serial.println("Bad protocol"); break;
                case 2: Serial.println("Bad client ID"); break;
                case 3: Serial.println("Server unavailable"); break;
                case 4: Serial.println("Bad credentials"); break;
                case 5: Serial.println("Unauthorized"); break;
                default: Serial.println("Unknown"); break;
            }
            delay(5000);
        }
        attempts++;
    }
    if (attempts >= maxAttempts) {
        Serial.println("Max MQTT attempts reached! Restarting...");
        ESP.restart();
    }
}

// ==================== 读取DS1307时间（格式化）====================
String getRTCTime() {
    if (!rtc.isrunning()) {
        Serial.println("DS1307 is not running! Please set time first.");
        return "2000-01-01 00:00:00";
    }
    DateTime now = rtc.now();
    // 格式化：年-月-日 时:分:秒
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    return String(timeStr);
}

// ==================== 采集传感器数据（含时间）====================
String collectSensorData() {
    float temp = 0, humidity = 0, pressure = 0;
    int light = 0;

    // 1. 读取AHTX0温湿度
    sensors_event_t hum_event, temp_event;
    if (aht_humidity && aht_temp && aht_humidity->getEvent(&hum_event) && aht_temp->getEvent(&temp_event)) {
        temp = temp_event.temperature;
        humidity = hum_event.relative_humidity;
    } else {
        Serial.println("Warning: AHTX0 read failed!");
    }

    // 2. 读取BMP280气压
    pressure = bmp.readPressure();
    if (isnan(pressure)) {
        Serial.println("Warning: BMP280 pressure read failed!");
        pressure = 0;
    }

    // 3. 读取光照传感器
    Wire.beginTransmission(LIGHT_SENSOR_ADDR);
    Wire.write(0x10);  // 连续高分辨率模式
    Wire.endTransmission();
    delay(180);  // 等待测量完成
    if (Wire.requestFrom(LIGHT_SENSOR_ADDR, 2) >= 2) {
        uint16_t light_raw = (Wire.read() << 8) | Wire.read();
        light = light_raw / 1.2;  // 转换为lux
    } else {
        Serial.println("Warning: Light sensor read failed!");
    }

    // 4. 读取RTC时间
    String time = getRTCTime();

    // 构建JSON（含时间戳，便于数据同步）
    String json = "{";
    json += "\"id\":\"" + String(DEVICE_ID) + "\",";
    json += "\"time\":\"" + time + "\",";
    json += "\"temperature\":" + String(temp, 2) + ",";  // 保留2位小数
    json += "\"humidity\":" + String(humidity, 2) + ",";
    json += "\"pressure\":" + String(pressure, 1) + ",";
    json += "\"light\":" + String(light);
    json += "}";

    return json;
}

// ==================== 初始化函数 ====================
void setup() {
    delay(1000);
    Serial.begin(115200);
    Serial.println("\n===== ESP8266 Sensor MQTT Publisher (DS1307) =====");

    // 1. 初始化I2C总线（DS1307/BMP280/AHTX0/光照传感器共用）
    Wire.begin();  // ESP8266默认SDA=D2，SCL=D1；若自定义引脚：Wire.begin(D4, D5)
    Serial.println("I2C bus initialized.");

    // 2. 初始化DS1307 RTC
    if (!rtc.begin()) {
        Serial.println("Error: Could not find DS1307 RTC!");
        while (1) {
            delay(1000);
            Serial.println("Stuck in RTC init failure.");
        }
    }
    if (!rtc.isrunning()) {
        Serial.println("DS1307 is not running, setting default time...");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // 使用编译时间初始化
    }
    Serial.println("DS1307 RTC initialized.");

    // 3. 初始化AHTX0
    if (!aht.begin()) {
        Serial.println("Warning: Could not find AHTX0 sensor!");
    } else {
        aht_humidity = aht.getHumiditySensor();
        aht_temp = aht.getTemperatureSensor();
        Serial.println("AHTX0 initialized.");
    }

    // 4. 初始化BMP280
    if (!bmp.begin(BMP280_ADDRESS)) {
        Serial.println("Warning: Could not find BMP280 sensor!");
    } else {
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL);
        Serial.println("BMP280 initialized.");
    }

    // 5. 连接WIFI
    connectWifi(WIFINAME, WIFIPASSWORD, 20);

    // 6. 配置MQTT
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setBufferSize(1024);  // 增大缓冲区，适配JSON数据
    Serial.println("MQTT client configured.");
}

// ==================== 主循环 ====================
void loop() {
    // 维持MQTT连接
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    // 采集并发送数据
    String data = collectSensorData();
    Serial.printf("Publishing data: %s\n", data.c_str());
    if (mqttClient.publish(mqtt_topic, data.c_str())) {
        Serial.println("Data published successfully!");
    } else {
        Serial.println("Error: Data publish failed!");
    }

    // 每隔10分钟采集一次
    Serial.println("Waiting 10 minutes...\n");
    delay(600000);
}
