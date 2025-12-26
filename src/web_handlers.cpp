#include "web_handlers.h"
#include <Update.h>
#include "ClientIdentity.h"

WebHandlers::WebHandlers(AsyncWebServer *webServer, SensorManager *sensorMgr, ClientIdentity *clientIdentity)
    : server(webServer), sensorManager(sensorMgr), clientIdentity(clientIdentity) {}

String WebHandlers::getContentType(String filename)
{
    if (filename.endsWith(".html"))
        return "text/html";
    if (filename.endsWith(".css"))
        return "text/css";
    if (filename.endsWith(".js"))
        return "application/javascript";
    if (filename.endsWith(".json"))
        return "application/json";
    return "text/plain";
}

bool WebHandlers::sendFile(String path, AsyncWebServerRequest *request)
{
    if (!SPIFFS.exists(path))
    {
        request->send(404, "text/plain", "File not found");
        return false;
    }

    String contentType = getContentType(path);
    request->send(SPIFFS, path, contentType);
    return true;
}

bool WebHandlers::isValidFileExtension(String filename)
{
    return filename.endsWith(".html") || filename.endsWith(".css") || filename.endsWith(".js") || filename.endsWith(".bin");
}

void WebHandlers::sendJsonResponse(AsyncWebServerRequest *request, bool success, String message, String data)
{
    String json = "{\"success\":" + String(success ? "true" : "false");
    if (message.length() > 0)
        json += ",\"message\":\"" + message + "\"";
    if (data.length() > 0)
        json += "," + data;
    json += "}";
    request->send(success ? 200 : 400, "application/json", json);
}

void WebHandlers::handleRoot(AsyncWebServerRequest *request)
{
    sendFile("/index.html", request);
}

void WebHandlers::handleStaticFile(AsyncWebServerRequest *request)
{
    sendFile(request->url(), request);
}

void WebHandlers::handleSensorData(AsyncWebServerRequest *request)
{
    String ip = request->client()->remoteIP().toString();
    int touch = 0;
    float percent = 0.0;
    String clientId = "0";

    if (request->hasParam("touch"))
        touch = request->getParam("touch")->value().toInt();
    if (request->hasParam("batteryPercent"))
        percent = request->getParam("batteryPercent")->value().toFloat();
    if (request->hasParam("clientId"))
        clientId = request->getParam("clientId")->value();

    sensorManager->updateSensorData(ip, clientId, touch, percent);
    request->send(200, "text/plain", "OK");
}

void WebHandlers::handleGetSensorData(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", sensorManager->getSensorDataJSON());
}

void WebHandlers::handleGetLocalSensorData(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", sensorManager->getLocalSensorDataJSON());
}

void WebHandlers::handleSensorDataPage(AsyncWebServerRequest *request)
{
    sendFile("/sensor_data.html", request);
}

void WebHandlers::handleSetClientId(AsyncWebServerRequest *request)
{
    String idParam = "";

    // Check both POST body parameters and URL parameters
    if (request->hasParam("id", true))
    { // true = POST body parameter
        idParam = request->getParam("id", true)->value();
    }
    else if (request->hasParam("id", false))
    { // false = URL parameter
        idParam = request->getParam("id", false)->value();
    }

    Serial.printf("[CLIENT_ID] Received request, idParam: '%s'\n", idParam.c_str());

    if (idParam.isEmpty())
    {
        sendJsonResponse(request, false, "Missing ID parameter");
        Serial.println("[CLIENT_ID] Missing ID parameter");
        return;
    }

    int newId = idParam.toInt();
    if (newId < 0 || newId > 15)
    {
        sendJsonResponse(request, false, "ID must be between 0-15");
        Serial.printf("[CLIENT_ID] Invalid ID: %d\n", newId);
        return;
    }

    clientIdentity->set(newId);
    sendJsonResponse(request, true, "Client ID updated", "\"clientId\":" + String(newId));
    Serial.printf("[CLIENT_ID] Successfully updated to %d\n", newId);
}

void WebHandlers::handleUpload(AsyncWebServerRequest *request)
{
    sendFile("/file_manager.html", request);
}

void WebHandlers::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    static File uploadFile;

    if (index == 0) // Start of upload
    {
        if (!filename.startsWith("/"))
            filename = "/" + filename;
        if (!isValidFileExtension(filename))
        {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid file type\"}");
            return;
        }
        uploadFile = SPIFFS.open(filename, "w");
        if (!uploadFile)
        {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to create file\"}");
            return;
        }
    }

    if (uploadFile && len > 0)
    {
        uploadFile.write(data, len);
    }

    if (final) // End of upload
    {
        if (uploadFile)
        {
            uploadFile.close();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Upload complete\"}");
        }
        else
        {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Upload failed\"}");
        }
    }
}

void WebHandlers::handleDeleteFile(AsyncWebServerRequest *request)
{
    String filename = "";
    if (request->hasParam("file"))
        filename = request->getParam("file")->value();

    if (filename.isEmpty())
    {
        sendJsonResponse(request, false, "No file specified");
        return;
    }

    if (!filename.startsWith("/"))
        filename = "/" + filename;

    bool success = SPIFFS.remove(filename);
    sendJsonResponse(request, success, success ? "File deleted" : "Delete failed");
}

void WebHandlers::handleListFiles(AsyncWebServerRequest *request)
{
    String json = "[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;

    while (file)
    {
        if (!first)
            json += ",";
        json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
        first = false;
        file = root.openNextFile();
    }

    json += "]";
    request->send(200, "application/json", json);
}

void WebHandlers::handleFirmware(AsyncWebServerRequest *request)
{
    sendFile("/firmware_update.html", request);
}

void WebHandlers::handleFirmwareUpdate(AsyncWebServerRequest *request)
{
    String filename = "";
    if (request->hasParam("file"))
        filename = request->getParam("file")->value();

    if (!filename.startsWith("/"))
        filename = "/" + filename;
    if (!filename.endsWith(".bin"))
    {
        sendJsonResponse(request, false, "File must be .bin");
        Serial.println("[FW UPDATE] File must be .bin");
        return;
    }

    if (!SPIFFS.exists(filename))
    {
        sendJsonResponse(request, false, "Firmware file not found");
        Serial.println("[FW UPDATE] Firmware file not found");
        return;
    }

    File firmwareFile = SPIFFS.open(filename);
    if (!firmwareFile)
    {
        sendJsonResponse(request, false, "Failed to open firmware file");
        Serial.println("[FW UPDATE] Failed to open firmware file");
        return;
    }

    size_t firmwareSize = firmwareFile.size();
    if (!Update.begin(firmwareSize))
    {
        firmwareFile.close();
        sendJsonResponse(request, false, "Failed to begin update: " + String(Update.errorString()));
        Serial.print("[FW UPDATE] Update.begin failed: ");
        Serial.println(Update.errorString());
        return;
    }

    size_t written = Update.writeStream(firmwareFile);
    firmwareFile.close();

    if (written != firmwareSize)
    {
        Update.abort();
        sendJsonResponse(request, false, "Update write failed: " + String(Update.errorString()));
        Serial.print("[FW UPDATE] Update.writeStream failed: ");
        Serial.println(Update.errorString());
        return;
    }

    if (!Update.end(true))
    {
        sendJsonResponse(request, false, "Update end failed: " + String(Update.errorString()));
        Serial.print("[FW UPDATE] Update.end failed: ");
        Serial.println(Update.errorString());
        return;
    }

    sendJsonResponse(request, true, "Firmware update successful, restarting...");
    Serial.println("[FW UPDATE] Firmware update successful, restarting...");
    delay(200);
    ESP.restart();
}

void WebHandlers::setupRoutes()
{
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleRoot(request); });

    server->on("/sensorpage", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleSensorDataPage(request); });

    server->on("/upload", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleUpload(request); });

    server->on("/firmware", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleFirmware(request); });

    server->on("/sensor", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleSensorData(request); });

    server->on("/sensorData", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleGetSensorData(request); });

    server->on("/localSensorData", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleGetLocalSensorData(request); });

    server->on("/setClientId", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleSetClientId(request); });

    server->on("/getClientId", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        int id = clientIdentity->get();
        request->send(200, "application/json", "{\"clientId\":" + String(id) + "}"); });

    // File upload handler
    server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request)
               {
                   // This will be handled by the upload handler
               },
               [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
               { handleFileUpload(request, filename, index, data, len, final); });

    server->on("/delete", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleDeleteFile(request); });

    server->on("/list", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleListFiles(request); });

    server->on("/firmwareUpdate", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleFirmwareUpdate(request); });

    // Static file handler
    server->onNotFound([this](AsyncWebServerRequest *request)
                       {
        String path = request->url();
        if (path.endsWith(".html") || path.endsWith(".css") || path.endsWith(".js")) {
            handleStaticFile(request);
        } else {
            request->send(404, "text/plain", "Not found");
        } });
}