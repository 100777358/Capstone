#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
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

// WiFi Settings (change to the current Wi-fi)
const char* ssid = "SM-S928W1371";
const char* password = "nd4txzdh7h5t37u";

// Web Server
WebServer server(80);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprint);

// Authentication variables
int fingerprintID = 0;
String usernameInput = "";
String passwordInput = "";
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

// Check credentials
bool checkCredentials() {
    return (usernameInput == "34678" && passwordInput == "8Jsyg54fs2D");
}

void setup() {
    Serial.begin(115200);
    fingerprint.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);

    // Initialize HuskyLens
    Wire.begin(HUSKY_SDA, HUSKY_SCL);
    if (!huskylens.begin(Wire)) {
        Serial.println("HuskyLens I2C initialization failed!");
    } else {
        Serial.println("HuskyLens connected via I2C!");
    }

    // Check Fingerprint Sensor
    if (finger.verifyPassword()) {
        Serial.println("Fingerprint sensor found!");
    } else {
        Serial.println("Fingerprint sensor not found!");
        while (1) { delay(1); }
    }

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

    // Web routes
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/login");
        server.send(303);
    });

    server.on("/login", HTTP_GET, []() {
        String page = "<h1>Power Utility Login</h1>";
        page += "<form action='/login' method='post'>Username: <input name='username'><br>";
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
            if (checkCredentials()) {
                loginAttemptCount = 0;
                fingerprintAttemptCount = maxTriesFingerprint; // Reset attempts on correct login
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
        String page = "<h1>Place your finger on the scanner</h1>";
        page += "<p>" + fingerprintMessage + "</p>";
        page += "<p><strong>Attempts left: " + String(fingerprintAttemptCount) + "</strong></p>";
        page += "<form action='/fingerprint' method='post'><button type='submit'>Scan</button></form>";
        server.send(200, "text/html", page);
    });

    server.on("/fingerprint", HTTP_POST, []() {
        fingerprintMessage = "Scanning...";
        bool verified = verifyFingerprint();

        if (verified) {
            fingerprintAttemptCount = maxTriesFingerprint; // Reset attempts on success
            server.sendHeader("Location", "/face");
        } else {
            fingerprintAttemptCount--; // Decrease only if incorrect
            if (fingerprintAttemptCount <= 0) {
                server.send(403, "text/html", "<p>Fingerprint verification failed too many times.</p>");
                return;
            }
            server.sendHeader("Location", "/fingerprint");
        }

        server.send(303);
    });

    server.on("/face", HTTP_GET, []() {
        String page = "<h1>Face Recognition</h1>";
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
        String page = "<h1>Protection Settings Files</h1>";
        page += "<p>Access granted!</p>";
        page += "<form action='/logout' method='post'><button type='submit'>Log Out</button></form>";
        server.send(200, "text/html", page);
    });

    server.on("/logout", HTTP_POST, []() {
        server.sendHeader("Location", "/login");
        server.send(303);
    });

    server.begin();
    Serial.println("Server started.");
}

void loop() {
    server.handleClient();
}

// Function to verify fingerprint
bool verifyFingerprint() {
    int result;
    result = finger.getImage();
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
    if (result == FINGERPRINT_OK) {
        fingerprintMessage = "Fingerprint verified!";
        return true;
    } else {
        fingerprintMessage = "Fingerprint not recognized.";
        return false;
    }
}

// Function to verify face using HuskyLens
bool verifyFace() {
    if (huskylens.request() && huskylens.available()) {
        HUSKYLENSResult result = huskylens.read();
        if (result.ID == 4) {
            faceMessage = "Face verified!";
            return true;
        }
    }
    faceMessage = "Unauthorized face detected!";
    return false;
}
