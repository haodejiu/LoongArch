# -*- coding: utf-8 -*-
from flask import Flask, jsonify, send_from_directory
from flask_cors import CORS
import sqlite3
import os

app = Flask(__name__)
CORS(app)  # 允许跨域访问，前端才能访问这个接口

DB_PATH = os.path.join(os.path.dirname(__file__), 'sensor_data.db')

@app.route('/api/data')
def get_data():
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        
        # 假设表名为 sensor_data，字段包括：timestamp, deviceId, AHTX0_Temp, AHTX0_Humidity, BMP280_Pressure, Light
        cursor.execute('''
            SELECT timestamp, device_id, AHTX0_Temp, AHTX0_Humidity, BMP280_Pressure, Light 
            FROM sensor_data 
            ORDER BY timestamp DESC
        ''')
        rows = cursor.fetchall()
        conn.close()

        data = []
        for row in rows:
            data.append({
                'timestamp': row[0],
                'id': row[1],
                'AHTX0_Temp': row[2],
                'AHTX0_Humidity': row[3],
                'BMP280_Pressure': row[4],
                'Light': row[5]
            })

        return jsonify(data[::-1])  # 倒序返回，最新在后面

    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/')
def serve_index():
    return send_from_directory('.', 'index.html')

@app.route('/<path:path>')
def serve_static(path):
    return send_from_directory('.', path)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)

