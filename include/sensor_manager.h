#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <map>
#include <string>
#include <Arduino.h>
#include "ClientIdentity.h"

struct SensorData
{
    String clientId;
    int touchValue;
    float batteryPercent;
};

class SensorManager
{
private:
    std::map<String, SensorData> sensorDataMap;
    ClientIdentity *clientIdentity = nullptr;

public:
    void begin(ClientIdentity *identity); // Initialize sensor pins
    void updateSensorData(const String &senderIP, const String &clientId, int touchValue, float batteryPercent);
    String getSensorDataJSON() const;
    const std::map<String, SensorData> &getAllSensorData() const;
    void clearSensorData();
    bool hasSensorData() const;
    String getFormattedSensorData() const;
    String getFormattedSensorData(int minSensors) const;
    // Add for client mode:
    int getLocalTouchValue() const;
    float getLocalBatteryVoltage() const;
    float getLocalBatteryPercent() const;
    String getLocalSensorDataJSON() const;
};

#endif // SENSOR_MANAGER_H