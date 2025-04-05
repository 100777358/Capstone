#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include "HUSKYLENS.h"

// Fingerprint Scanner Settings
#define RX_PIN 16
#define TX_PIN 17
HardwareSerial fingerprint(2);

// HuskyLens I2C Settings
HUSKYLENS huskylens;
#define HUSKY_SDA 21
#define HUSKY_SCL 22

// WiFi Settings
const char* ssid = "SM-S928W1371";
const char* wifiPassword = "nd4txzdh7h5t37u";

// Web Server
WebServer server(80);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprint);

// Database Server Settings
const char* serverURL = "http://192.168.198.32/test1/sensor.php";

// Authentication variables
String usernameInput = "";
String passwordInput = "";
int expectedFingerprintID = 0;
int expectedFaceID = 0;
int maxTriesPassword = 3;
int maxTriesFingerprint = 5;
int maxTriesFace = 5;
int loginAttemptCount = 0;
int fingerprintAttemptCount = maxTriesFingerprint;
int faceAttemptCount = 0;
bool isLockedOut = false;
bool fingerprintVerified = false;
bool faceVerified = false;
bool faceLockedOut = false;
String fingerprintMessage = "";
String faceMessage = "";

void setup() {
    Serial.begin(115200);
    fingerprint.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);

    // Initialize HuskyLens
    Wire.begin(HUSKY_SDA, HUSKY_SCL);
    if (!huskylens.begin(Wire)) {
        Serial.println("HuskyLens I2C initialization failed!");
    } else {
        Serial.println("HuskyLens connected via I2C!");
        huskylens.writeAlgorithm(ALGORITHM_FACE_RECOGNITION);
    }

    // Check Fingerprint Sensor
    if (finger.verifyPassword()) {
        Serial.println("Fingerprint sensor found!");
    } else {
        Serial.println("Fingerprint sensor not found!");
        while (1) { delay(1); }
    }

    // Connect to WiFi
    WiFi.begin(ssid, wifiPassword);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

    // Web routes
    setupWebServer();
    
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
}

void setupWebServer() {
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/login");
        server.send(303);
    });

    server.on("/login", HTTP_GET, []() {
        String page = "<html><head>";
        page += "<style>";
        page += "body { display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f0f0f0; margin: 0; }";
        page += ".container { text-align: center; padding: 20px; background: white; border: 3px solid black; border-radius: 10px; box-shadow: 5px 5px 15px rgba(0,0,0,0.3); }";
        page += "input, button { margin: 10px; padding: 10px; width: 80%; font-size: 16px; }";
        page += "</style></head><body>";
        page += "<div class='container'>";
        page += "<h1>Power Utility Login</h1>";
        page += "<form action='/login' method='post'>";
        page += "Username: <input name='username'><br>";
        page += "Password: <input name='password' type='password'><br>";
        page += "<button type='submit'>Login</button></form>";

        if (loginAttemptCount > 0 && !isLockedOut) {
            page += "<p style='color:red;'>Incorrect username or password. Try again.</p>";
        }
        if (isLockedOut) {
            page += "<p style='color:red;'>Locked out due to too many attempts.</p>";
        }

        server.send(200, "text/html", page);
    });

    server.on("/login", HTTP_POST, []() {
        if (isLockedOut) {
            server.send(403, "text/html", "<p>Locked out due to too many attempts.</p>");
            return;
        }

        if (server.hasArg("username") && server.hasArg("password")) {
            usernameInput = server.arg("username");
            passwordInput = server.arg("password");
            
            if (authenticateWithServer()) {
                loginAttemptCount = 0;
                fingerprintAttemptCount = maxTriesFingerprint;
                server.sendHeader("Location", "/fingerprint");
                server.send(303);
            } else {
                loginAttemptCount++;
                if (loginAttemptCount >= maxTriesPassword) {
                    isLockedOut = true;
                    server.send(403, "text/html", "<p>Access denied.</p>");
                } else {
                    server.sendHeader("Location", "/login");
                    server.send(303);
                }
            }
        } else {
            server.send(400, "text/html", "<p>Missing credentials.</p>");
        }
    });

    server.on("/fingerprint", HTTP_GET, []() {
        String page = "<html><head>";
        page += "<style>";
        page += "body { display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f0f0f0; margin: 0; }";
        page += ".container { text-align: center; padding: 20px; background: white; border: 3px solid black; border-radius: 10px; box-shadow: 5px 5px 15px rgba(0,0,0,0.3); }";
        page += "</style></head><body>";
        page += "<div class='container'>";
        page += "<h1>Please place finger on scanner</h1>";
        page += "<p>" + fingerprintMessage + "</p>";
        page += "<p><strong>Attempts left: " + String(fingerprintAttemptCount) + "</strong></p>";
        page += "<form action='/fingerprint' method='post'><button type='submit'>Scan</button></form>";
        server.send(200, "text/html", page);
    });

    server.on("/fingerprint", HTTP_POST, []() {
        fingerprintMessage = "Scanning...";
        bool verified = verifyFingerprint();

        if (verified) {
            fingerprintAttemptCount = maxTriesFingerprint;
            server.sendHeader("Location", "/face");
        } else {
            fingerprintAttemptCount--;
            if (fingerprintAttemptCount <= 0) {
                server.send(403, "text/html", "<p>Fingerprint verification failed too many times.</p>");
                return;
            }
            server.sendHeader("Location", "/fingerprint");
        }
        server.send(303);
    });

    server.on("/face", HTTP_GET, []() {
        String page = "<html><head>";
        page += "<style>";
        page += "body { display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f0f0f0; margin: 0; }";
        page += ".container { text-align: center; padding: 20px; background: white; border: 3px solid black; border-radius: 10px; box-shadow: 5px 5px 15px rgba(0,0,0,0.3); }";
        page += "</style></head><body>";
        page += "<div class='container'>";
        page += "<h1>Please look at the camera</h1>";
        page += "<p><strong>Attempts left: " + String(maxTriesFace-faceAttemptCount) + "</strong></p>";
        if (faceLockedOut) {
            page += "<p style='color:red;'>Too many failed attempts. Face recognition is locked.</p>";
        } else {
            page += "<p>" + faceMessage + "</p>";
            page += "<form action='/face' method='post'><button type='submit'>Scan Face</button></form>";
        }
        server.send(200, "text/html", page);
    });

    server.on("/face", HTTP_POST, []() {
        if (faceLockedOut) {
            server.send(403, "text/html", "<p>Face recognition is locked due to too many failed attempts.</p>");
            return;
        }

        faceMessage = "Scanning face...";
        bool verified = verifyFace();

        if (verified) {
            server.sendHeader("Location", "/protected");
        } else {
            faceAttemptCount++;
            if (faceAttemptCount >= maxTriesFace) {
                faceLockedOut = true;
                faceMessage = "Too many failed attempts. Face recognition is now locked.";
            }
            server.sendHeader("Location", "/face");
        }
        server.send(303);
    });

    server.on("/protected", HTTP_GET, []() {
        String page = "<html><head><style>";
        page += "body { margin: 0; padding: 0; background-color: #f0f0f0; font-family: Arial, sans-serif; }";
        page += ".container { display: grid; grid-template-rows: auto 1fr auto; height: 100vh; text-align: center; }";
        page += ".header { background: #007BFF; color: white; padding: 15px; font-size: 24px; }";
        page += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; padding: 30px; }";
        page += ".section { background: white; padding: 20px; border-radius: 10px; border: 2px solid #007BFF; box-shadow: 5px 5px 15px rgba(0,0,0,0.3); }";
        page += "h2 { color: #007BFF; }";
        page += ".button { background: #007BFF; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-size: 18px; cursor: pointer; text-decoration: none; display: inline-block; margin-top: 10px; }";
        page += ".button:hover { background: #0056b3; }";
        page += ".footer { background: #333; color: white; padding: 15px; font-size: 16px; }";
        page += "</style></head><body>";

        page += "<div class='container'>";
        page += "<div class='header'>Protection Settings</div>";
        
        page += "<div class='grid'>";
        page += "<div class='section'><h2>Station A</h2><p>Adjust settings for Station A.</p>";
        page += "<a href='/stationA' class='button'>Configure</a></div>";

        page += "<div class='section'><h2>Station B</h2><p>Adjust settings for Station B.</p>";
        page += "<a href='/stationB' class='button'>Configure</a></div>";

        page += "<div class='section'><h2>Station C</h2><p>Adjust settings for Station C.</p>";
        page += "<a href='/stationC' class='button'>Configure</a></div>";
        page += "</div>";
        page += "<form action='/logout' method='post'><button type='submit'>Log Out</button></form>";

        server.send(200, "text/html", page);
    });

    server.on("/logout", HTTP_POST, []() {
        // Reset authentication variables
        loginAttemptCount = 0;
        fingerprintAttemptCount = maxTriesFingerprint;
        faceAttemptCount = 0;
        isLockedOut = false;
        fingerprintVerified = false;
        faceVerified = false;
        faceLockedOut = false;
        fingerprintMessage = "";
        faceMessage = "";
        server.sendHeader("Location", "/login");
        server.send(303);
    });
}

bool authenticateWithServer() {
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "username=" + usernameInput + "&password=" + passwordInput;
    int httpResponseCode = http.POST(postData);
    
    if (httpResponseCode == 200) {
        String payload = http.getString();
        http.end();

        if (payload.indexOf("User authenticated!") >= 0) {
            // Extract biometric IDs from response
            int fpIndex = payload.indexOf("|FingerprintID=");
            int faceIndex = payload.indexOf("|FaceID=");
            
            if (fpIndex != -1 && faceIndex != -1) {
                expectedFingerprintID = payload.substring(fpIndex + 15, payload.indexOf('|', fpIndex + 1)).toInt();
                expectedFaceID = payload.substring(faceIndex + 8).toInt();
                return true;
            }
        }
    }
    
    http.end();
    return false;
}

bool verifyFingerprint() {
    int result = finger.getImage();
    if (result != FINGERPRINT_OK) {
        fingerprintMessage = "Image capture failed.";
        return false;
    }

    result = finger.image2Tz();
    if (result != FINGERPRINT_OK) {
        fingerprintMessage = "Processing error.";
        return false;
    }

    result = finger.fingerSearch();
    if (result == FINGERPRINT_OK && finger.fingerID == expectedFingerprintID) {
        fingerprintMessage = "Fingerprint verified!";
        return true;
    }
    
    fingerprintMessage = "Fingerprint not recognized.";
    return false;
}

bool verifyFace() {
    if (!huskylens.request() || !huskylens.available()) {
        faceMessage = "Camera error!";
        return false;
    }
    
    HUSKYLENSResult result = huskylens.read();
    if (result.ID == expectedFaceID) {
        faceMessage = "Face verified!";
        return true;
    }
    
    faceMessage = "Unauthorized face detected!";
    return false;
}