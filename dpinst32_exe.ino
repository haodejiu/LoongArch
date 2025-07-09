#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <RtcDS1302.h>
#include <PubSubClient.h>  // MQTT客户端库
#define DEVICE_ID "ESP006" 


// 定义DS1302的引脚
#define DS1302_CLK_PIN D5
#define DS1302_DAT_PIN D6
#define DS1302_RST_PIN D7

// WIFI 相关信息
const char* WIFINAME = "N6-506-ARM";
const char* WIFIPASSWORD = "armn6-506qwg";

// MQTT服务器相关信息
const char* mqtt_server = "192.168.3.102"; // MQTT服务器IP
const char* mqtt_topic = "sensors/data";   // 发布的主题
const char* mqtt_clientId = "ESP8266_Sensor_Client"; // 客户端ID
const int mqtt_port = 1883;  // MQTT端口

// 创建 ThreeWire 对象
ThreeWire myWire(DS1302_DAT_PIN, DS1302_CLK_PIN, DS1302_RST_PIN);

// RTC 对象
RtcDS1302<ThreeWire> rtc(myWire);

// WiFi和MQTT客户端对象
WiFiClient espClient;
PubSubClient mqttClient(espClient); // 重命名为mqttClient避免冲突

// 光照传感器I2C地址（修正）
#define LIGHT_SENSOR_ADDR 0x23  // 避免地址冲突

// 参数一为 WIFI 名称,参数二为 WIFI 密码,参数三为设定的最长等待时间
void connectWifi(const char* wifiName, const char* wifiPassword, uint8_t waitTime) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(wifiName, wifiPassword);
    uint8_t count = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.printf("connect WIFI...%ds\r\n", ++count);
        if (count >= waitTime) {
            Serial.println("connect WIFI fail");
            while (1) {
                delay(1000);
                Serial.println("Stuck in WiFi failure");
            }
        }
    }
    Serial.printf("connect WIFI %s success, local IP is %s\r\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// AHTX0 传感器相关定义
Adafruit_AHTX0 aht;
Adafruit_Sensor *aht_humidity, *aht_temp;

// BMP280 传感器相关定义
#define BMP280_ADDRESS 0x76
Adafruit_BMP280 bmp;

// MQTT连接/重连函数
void reconnectMQTT() {
  int maxAttempts = 10;
  int attempts = 0;
  
  while (!mqttClient.connected() && attempts < maxAttempts) {
    Serial.print("Attempting MQTT connection as ");
    Serial.print(mqtt_clientId);
    Serial.print(" to ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);
    
    if (mqttClient.connect(mqtt_clientId)) {
      Serial.println("connected to MQTT broker");
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      
      // 输出详细的错误状态
      switch(mqttClient.state()) {
        case -4: Serial.println("MQTT_CONNECTION_TIMEOUT"); break;
        case -3: Serial.println("MQTT_CONNECTION_LOST"); break;
        case -2: Serial.println("MQTT_CONNECT_FAILED"); break;
        case -1: Serial.println("MQTT_DISCONNECTED"); break;
        case 1: Serial.println("MQTT_CONNECT_BAD_PROTOCOL"); break;
        case 2: Serial.println("MQTT_CONNECT_BAD_CLIENT_ID"); break;
        case 3: Serial.println("MQTT_CONNECT_UNAVAILABLE"); break;
        case 4: Serial.println("MQTT_CONNECT_BAD_CREDENTIALS"); break;
        case 5: Serial.println("MQTT_CONNECT_UNAUTHORIZED"); break;
      }
      
      delay(5000);
    }
    attempts++;
  }
  
  if (attempts >= maxAttempts) {
    Serial.println("Max MQTT connection attempts reached. Restarting...");
    ESP.restart();
  }
}

// 采集传感器数据
String collectSensorData() {
    Serial.println("== Collecting Sensor Data ==");
    
    // 声明所有变量在函数顶部，确保作用域正确
    float temp_value = 0;
    float humidity_value = 0;
    float bmp_temperature = 0;
    float pressure = 0;
    int lightLevel = 0;  // 将 lightLevel 声明在函数顶部

    // 读取 AHTX0 传感器的温湿度数据
    sensors_event_t humidity, temp;
    if (aht_humidity && aht_temp && aht_humidity->getEvent(&humidity) && aht_temp->getEvent(&temp)) {
        temp_value = temp.temperature;
        humidity_value = humidity.relative_humidity;
        Serial.print("AHTX0 温度： ");
        Serial.print(temp_value);
        Serial.println(" °C");
        Serial.print("AHTX0 湿度: ");
        Serial.print(humidity_value);
        Serial.println(" %RH");
    } else {
        Serial.println("Failed to read AHTX0 sensor!");
    }

    // 读取 BMP280 传感器的温度、气压数据
    bmp_temperature = bmp.readTemperature();
    pressure = bmp.readPressure();
    if (!isnan(bmp_temperature)) {
        Serial.print("BMP280 温度 = ");
        Serial.print(bmp_temperature);
        Serial.println(" *C");
    } else {
        Serial.println("Failed to read BMP280 temperature");
    }
    
    if (!isnan(pressure)) {
        Serial.print("BMP280 气压 = ");
        Serial.print(pressure);
        Serial.println(" Pa");
    } else {
        Serial.println("Failed to read BMP280 pressure");
    }

    // 光照值相关代码
    Wire.beginTransmission(LIGHT_SENSOR_ADDR);
    Wire.write(0x01); // 上电命令
    Wire.endTransmission();
    Wire.beginTransmission(LIGHT_SENSOR_ADDR);
    Wire.write(0x10); // 连续高分辨率模式（0x10）
    Wire.endTransmission();
    delay(180);       // 等待测量完成（H模式需120~180ms）
    Wire.requestFrom(LIGHT_SENSOR_ADDR, 2);
    
    if (Wire.available() >= 2) {
        uint8_t msb = Wire.read();
        uint8_t lsb = Wire.read();
        lightLevel = ((msb << 8) | lsb) / 1.2;  // 使用正确的变量名
        Serial.print("当前光照值: ");
        Serial.println(lightLevel);
    } else {
        Serial.println("Failed to read light sensor");
        lightLevel = 0;
    }

    // 构建JSON格式的数据字符串
    String data = "{";
    data += "\"id\":\"" + String(DEVICE_ID) + "\",";
    data += "\"AHTX0_Temp\":" + String(temp_value) + ",";
    data += "\"AHTX0_Humidity\":" + String(humidity_value) + ",";
    data += "\"BMP280_Temp\":" + String(bmp_temperature) + ",";
    data += "\"BMP280_Pressure\":" + String(pressure) + ",";
    data += "\"Light\":" + String(lightLevel);
    data += "}";
    
    return data;
}

// 初始化函数
void setup() {
    // 添加启动延时，防止早期串口通信问题
    delay(1000);
    
    Serial.begin(115200);
    Serial.println("\n\nESP8266 Sensor MQTT Publisher");
    
    Wire.begin();

    // 初始化 AHTX0 传感器
    if (!aht.begin()) {
        Serial.println("Could not find AHTX0 sensor! Will continue without it.");
    } else {
        aht_humidity = aht.getHumiditySensor();
        aht_temp = aht.getTemperatureSensor();
    }

    // 初始化 BMP280 传感器
    if (!bmp.begin(BMP280_ADDRESS)) {
        Serial.println("Could not find a valid BMP280 sensor, check wiring! Will continue without it.");
    } else {
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL);
    }

    // 初始化 RTC
    rtc.Begin();

    // 连接 WIFI
    connectWifi(WIFINAME, WIFIPASSWORD, 20);  // 增加等待时间
    
    // 设置MQTT服务器
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setBufferSize(1024);  // 增加缓冲区大小
}

// 循环函数
void loop() {
    // 检查MQTT连接
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    
    // 处理MQTT后台任务
    mqttClient.loop();
    
    // 打印当前连接状态
    Serial.println("WiFi status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
    Serial.println("MQTT status: " + String(mqttClient.connected() ? "Connected" : "Disconnected"));
    
    // 采集传感器数据
    String sensorData = collectSensorData();

    // 通过MQTT发送数据
    Serial.println("Publishing data to topic: " + String(mqtt_topic));
    if (mqttClient.publish(mqtt_topic, sensorData.c_str())) {
        Serial.println("Data sent to MQTT server");
        Serial.println(sensorData);
    } else {
        Serial.println("Failed to send data to MQTT server");
        Serial.println("Error code: " + String(mqttClient.state()));
    }

    // 每隔10分钟采集一次数据
    Serial.println("Waiting 10 minutes...");
    delay(600000);
}