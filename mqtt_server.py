# coding: utf-8
import paho.mqtt.client as mqtt
import json
import sqlite3
import logging
import datetime
import csv
import os
from logging.handlers import RotatingFileHandler
import threading

# 配置日志记录
logger = logging.getLogger('mqtt_logger')
logger.setLevel(logging.INFO)
log_formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')

# 控制台日志
console_handler = logging.StreamHandler()
console_handler.setFormatter(log_formatter)
logger.addHandler(console_handler)

# 文件日志（滚动日志）
file_handler = RotatingFileHandler('mqtt_server.log', maxBytes=10 * 1024 * 1024, backupCount=5)
file_handler.setFormatter(log_formatter)
logger.addHandler(file_handler)

# MQTT服务器配置
MQTT_SERVER = "192.168.3.101"
MQTT_PORT = 1883
MQTT_TOPIC = "sensors/data"

# 数据库文件
DB_FILE = 'sensor_data.db'

# 创建数据库锁确保线程安全
db_lock = threading.Lock()


def init_database():
    """初始化数据库和表结构"""
    with db_lock:
        try:
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('''
                CREATE TABLE IF NOT EXISTS sensor_data (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    AHTX0_Temp REAL,
                    AHTX0_Humidity REAL,
                    BMP280_Temp REAL,
                    BMP280_Pressure REAL,
                    Light INTEGER
                )
            ''')
            conn.commit()
            logger.info("Database initialized successfully")
        except sqlite3.Error as e:
            logger.error(f"Database initialization error: %s", e)
        finally:
            if conn:
                conn.close()


def save_to_database(data):
    """将传感器数据保存到数据库"""
    timestamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    with db_lock:
        try:
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('''
                INSERT INTO sensor_data 
                (timestamp, AHTX0_Temp, AHTX0_Humidity, BMP280_Temp, BMP280_Pressure, Light)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (
                timestamp,
                data.get('AHTX0_Temp'),
                data.get('AHTX0_Humidity'),
                data.get('BMP280_Temp'),
                data.get('BMP280_Pressure'),
                data.get('Light')
            ))
            conn.commit()
            logger.info("Data saved to database successfully")
            return True
        except sqlite3.Error as e:
            logger.error(f"Database save error: %s", e)
            return False
        finally:
            if conn:
                conn.close()


def save_to_csv(data):
    """将数据保存到CSV文件作为日志备份"""
    timestamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    file_exists = os.path.isfile('sensor_data.csv')

    try:
        with open('sensor_data.csv', 'a', newline='') as csvfile:
            fieldnames = ['timestamp', 'AHTX0_Temp', 'AHTX0_Humidity',
                          'BMP280_Temp', 'BMP280_Pressure', 'Light']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

            if not file_exists:
                writer.writeheader()

            writer.writerow({
                'timestamp': timestamp,
                'AHTX0_Temp': data.get('AHTX0_Temp'),
                'AHTX0_Humidity': data.get('AHTX0_Humidity'),
                'BMP280_Temp': data.get('BMP280_Temp'),
                'BMP280_Pressure': data.get('BMP280_Pressure'),
                'Light': data.get('Light')
            })
        logger.info("Data saved to CSV backup")
    except Exception as e:
        logger.error(f"CSV save error: %s", e)


def on_connect(client, userdata, flags, rc):
    """MQTT连接回调函数"""
    if rc == 0:
        logger.info(f"Connected to MQTT broker at {MQTT_SERVER}:{MQTT_PORT}")
        client.subscribe(MQTT_TOPIC)
        logger.info(f"Subscribed to topic: {MQTT_TOPIC}")
    else:
        logger.error(f"Connection failed with result code {rc}")


def on_message(client, userdata, msg):
    """MQTT消息接收回调函数"""
    try:
        raw_data = msg.payload.decode()
        logger.info(f"Received raw data: {raw_data}")

        # 解析JSON数据
        data = json.loads(raw_data)

        logger.info("\n=== Parsed Sensor Data ===")
        logger.info(f"温度(AHTX0): {data['AHTX0_Temp']:.1f}°C")
        logger.info(f"湿度(AHTX0): {data['AHTX0_Humidity']:.1f}%RH")
        logger.info(f"温度(BMP280): {data['BMP280_Temp']:.1f}°C")
        logger.info(f"气压(BMP280): {data['BMP280_Pressure']:.1f} Pa")
        logger.info(f"光照强度: {data['Light']} lux")

        # 保存到数据库
        if not save_to_database(data):
            logger.warning("Saving to CSV backup due to database error")
            save_to_csv(data)
        else:
            # 同时保存到CSV作为备份
            save_to_csv(data)

    except json.JSONDecodeError as e:
        logger.error(f"JSON decode error: {e}, raw data: {msg.payload.decode()}")
    except KeyError as e:
        logger.error(f"Missing expected key in data: {e}")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")


def main():
    """主程序"""
    logger.info("Starting MQTT Data Logger Service")

    # 初始化数据库
    init_database()

    # 创建MQTT客户端
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        # 连接服务器
        client.connect(MQTT_SERVER, MQTT_PORT, 60)
        logger.info(f"Connecting to MQTT broker at {MQTT_SERVER}:{MQTT_PORT}")

        # 持续监听
        client.loop_forever()
    except Exception as e:
        logger.critical(f"Fatal error: %s", e)
    finally:
        client.disconnect()
        logger.info("MQTT client disconnected")


if __name__ == "__main__":
    main()