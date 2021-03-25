MQTT_CLIENT_ID = CentralAlarma

Subscriptions

/MQTT_CLIENT_ID/Alarm/Set
    0
    1
    2
    
/MQTT_CLIENT_ID/Options
    { 
    "pin":1234
    "inputs_names":["IN1","IN2","IN3","IN4","IN5","IN6","IN7","IN8"], 
    "inputs_function":[3,1,3,4,0,0,0,0], 
    "numbers":["0123456789","0123456789","0123456789","0123456789","0123456789"],
    "act_numbers":[0,0,0,0] 
    }

/MQTT_CLIENT_ID/SMS/send
{
  "number": 3516510632,
  "message": "Hola"
}    

Publications

/MQTT_CLIENT_ID/SMS/received
{
  "number": 3516510632,
  "message": "Hola"
}

/MQTT_CLIENT_ID/Alarm/Status
    disarmed
    armed_home
    armed_away
    armed_night
    armed_custom_bypass
    pending
    triggered
    arming
    disarming

 /MQTT_CLIENT_ID/Alarm/Inputs
{
    "sensors":[0,0,0,0,0,0,0,0],
    "rf": [0,0,0,0]
}

 /MQTT_CLIENT_ID/Sensors
{
    "trigger_cause": 1,
    "temp_int":99.99,
    "temp_ext":99.99,
    "hum_ext":99,
    "lcr":99,
    "v_bat":99.99,
    "vac":1,
    "voltage":999,
    "current":99.99,
    "power":9999.99,
    "energy":999999
    "gsm_status":1,
    "gsm_signal":99
}

/MQTT_CLIENT_ID/Alarm/Options
{
    "pin": 12345678,
    "inputs_names":["IN1","IN2","IN","IN4","IN5","IN6","IN7","01234567890123456789"],
    "inputs_function":[0,0,0,0,0,0,0,0],
    "numbers":["01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789"],
    "act_numbers":[0,0,0,0,0]
}

/MQTT_CLIENT_ID/WiFi
{
  "rssi": -53,
  "rssi": -54,
  "heap_free": 15648
}