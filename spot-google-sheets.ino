// TWELITE SPOT Google Sheets Example

// Arduino / ESP libraries
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// Third-party library
#include <ESP_Google_Sheet_Client.h>

// Mono Wireless TWELITE Wings API for 32-bit Arduinos
#include <MWings.h>

// Local configs
#include "config.h"

// Pin defs
const uint8_t TWE_RST = 5;
const uint8_t TWE_PRG = 4;
const uint8_t LED = 18;
const uint8_t ESP_RXD1 = 16;
const uint8_t ESP_TXD1 = 17;

// TWELITE defs
const uint8_t TWE_CH = 18;
const uint32_t TWE_APPID = 0x67720102;
const uint8_t TWE_RETRY = 2;
const uint8_t TWE_POWER = 3;

const char* SPREADSHEET_TITLE_PREFIX = "SPOT Sheet";
const char* SPREADSHEET_LOCALE = "ja_JP";
const char* SPREADSHEET_TIME_ZONE = "Asia/Tokyo";

const int MIN_REQUEST_INTERVAL = 1000;    // 60 requests per minute

const int SHEETS_DEFAULT_ROWS = 1000;    // Default length is 1000 rows
const int SHEETS_ROWS = 100000;          // Max 1,000,000 rows at 10 columns

const int ARIA_SHEET_ID = 1;
const char* ARIA_SHEET_TITLE = "ARIA";
constexpr int ARIA_BUFFER_PACKETS = 32;    // Max number of rows per addition request

// Type definitions
struct ParsedAppAriaPacketWithTime {
    ParsedAppAriaPacket packet;
    uint32_t timestamp;
};

// Global objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.nict.jp", 32400);    // For filenames

String spreadsheetIdString;    // Identifier of newly created file
bool readyForNewRequests = false;
uint32_t lastTimeRequestWasSent = UINT32_MAX;

QueueHandle_t ariaPacketQueue;       // Store received data from ARIA
uint32_t rowToAddNewAriaData = 2;    // Starting with the Row 2

// Function prototypes
void anotherLoop();

void waitUntilNewRequestsReady();
String createSpreadsheet();
bool formatSheet(const String spreadsheetId, const int sheetId);
bool extendSheetWithFormat(const String spreadsheetId, const int sheetId, const int rows);
bool addSheetAriaHeaderRow(const String spreadsheetId, const char* const sheetTitle);
bool addSheetsDataRow(const String spreadsheetId);


// Setup procedure
void setup() {
    // Init USB serial
    Serial.begin(115200);
    Serial.println("Hello, this is TWELITE SPOT.");

    // Init Queue
    Serial.println("Initializing queue...");
    ariaPacketQueue = xQueueCreate(ARIA_BUFFER_PACKETS, sizeof(ParsedAppAriaPacketWithTime));
    if (ariaPacketQueue == 0) { Serial.println("Failed to init a queue."); }
    Serial.println("Completed.");

    // Init TWELITE
    Serial2.begin(115200, SERIAL_8N1, ESP_RXD1, ESP_TXD1);
    if (Twelite.begin(Serial2,
                      LED, TWE_RST, TWE_PRG,
                      TWE_CH, TWE_APPID, TWE_RETRY, TWE_POWER)) {
        Serial.println("Started TWELITE.");
    }

    Twelite.on([](const ParsedAppAriaPacket& packet) {
        Serial.println("Got a new packet from ARIA.");
        ParsedAppAriaPacketWithTime packetWithTime;
        packetWithTime.timestamp = millis();
        packetWithTime.packet = packet;
        if (not(xQueueSend(ariaPacketQueue, &packetWithTime, 0) == pdPASS)) {
            Serial.println("Failed to add packet data to the queue.");
        }
    });

    // Init Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED) {
        static int count = 0;
        Serial.print('.');
        delay(500);
        // Retry every 5 seconds
        if (count++ % 10 == 0) {
            WiFi.disconnect();
            WiFi.reconnect();
            Serial.print('!');
        }
    }
    Serial.print("\nConnected. IP: ");
    Serial.println(WiFi.localIP().toString().c_str());

    // Init NTP
    Serial.print("Initializing NTP...");
    timeClient.begin();
    timeClient.update();
    Serial.print("Completed. UNIX time: ");
    Serial.println(timeClient.getEpochTime());

    // Init Google Sheets
    GSheet.setTokenCallback([](TokenInfo info) {
        // Print token initialization states
        if (info.status == esp_signer_token_status_error) {
            Serial.print("Token error ");
            Serial.println(GSheet.getTokenError(info));
        }
        Serial.print(GSheet.getTokenType(info));
        Serial.print(" ");
        Serial.println(GSheet.getTokenStatus(info));
    });
    GSheet.setPrerefreshSeconds(60);    // Set refresh rate for auth token

    Serial.println("Initializing sheets...");
    GSheet.begin(SERVICE_ACCOUNT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    Serial.println("Creating sheets...");
    waitUntilNewRequestsReady();    // Wait for token
    spreadsheetIdString = createSpreadsheet();
    if (not(spreadsheetIdString.length() > 0)) {
        Serial.println("Failed to create sheets.");
    }

    Serial.println("Adding headers for ARIA...");
    delay(MIN_REQUEST_INTERVAL);
    waitUntilNewRequestsReady();
    if (not addSheetAriaHeaderRow(spreadsheetIdString, ARIA_SHEET_TITLE)) {
        Serial.println("Failed to add headers.");
    }

    Serial.println("Formatting the sheet for ARIA...");
    delay(MIN_REQUEST_INTERVAL);
    waitUntilNewRequestsReady();
    if (not formatSheet(spreadsheetIdString, ARIA_SHEET_ID)) {
        Serial.println("Failed to format.");
    }

    Serial.println("Extending the sheet for ARIA...");
    delay(MIN_REQUEST_INTERVAL);
    waitUntilNewRequestsReady();
    if (not extendSheetWithFormat(spreadsheetIdString, ARIA_SHEET_ID, SHEETS_ROWS - SHEETS_DEFAULT_ROWS)) {
        Serial.println("Failed to extend.");
    }

    Serial.println("Completed.");

    // Assign a FreeRTOS task to the Core 0 for the another loop procedure updating TWELITE
    // Note: Core 0 is also used for the WiFi task, which priority is 19 (ESP_TASKD_EVENT_PRIO - 1)
    xTaskCreatePinnedToCore(
        [](void* params) {
            while (true) {
                anotherLoop();
                vTaskDelay(1);    // IMPORTANT for Watchdog
            }
        },
        "Task for anotherLoop()", 8192, nullptr, 18, nullptr, 0);    // Priority is 18 (lower than WiFi)
}

// loop procedure
void loop() {
    // Due to the API limitation, "Throttle" requests (like JavaScript)
    if (millis() - lastTimeRequestWasSent > MIN_REQUEST_INTERVAL) {
        // Add available data
        addSheetsDataRow(spreadsheetIdString);
    }
    readyForNewRequests = GSheet.ready();
    timeClient.update();
    // Twelite.update();    // No multi-tasking
}

// Another loop procedure; Priority is higher than loop()
void anotherLoop() {
    Twelite.update();    // With multi-tasking (async)
}


// Wait until the new request are ready
void waitUntilNewRequestsReady() {
    while (not readyForNewRequests) {
        readyForNewRequests = GSheet.ready();
    }
}

// Create a spreadsheet
// Note: Drive API is required in addition to Sheets API
// REST Reference: https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets/create
String createSpreadsheet() {
    if (not readyForNewRequests) { return String(""); }

    FirebaseJson spreadsheet;
    char titleCString[64];
    sprintf(titleCString, "%s (%d)", SPREADSHEET_TITLE_PREFIX, timeClient.getEpochTime());
    spreadsheet.set("properties/title", titleCString);
    spreadsheet.set("properties/locale", SPREADSHEET_LOCALE);
    spreadsheet.set("properties/timeZone", SPREADSHEET_TIME_ZONE);
    spreadsheet.set("properties/defaultFormat/horizontalAlignment", "RIGHT");

    spreadsheet.set("sheets/[0]/properties/sheetId", ARIA_SHEET_ID);
    spreadsheet.set("sheets/[0]/properties/title", ARIA_SHEET_TITLE);

    FirebaseJson response;
    Serial.println("Requesting to create...");
    // Pass (Response container, Spreadsheet, User to share)
    bool succeeded = GSheet.create(&response, &spreadsheet, USER_ACCOUNT_EMAIL);
    lastTimeRequestWasSent = millis();

    // For debugging
    // String responseString;
    // response.toString(responseString, true);
    // Serial.println(responseString);

    if (succeeded) {
        FirebaseJsonData result;
        response.get(result, "spreadsheetId");
        if (result.success and result.type == "string") {
            Serial.println("Succeeded.");
            return result.to<String>();
        }
    }
    Serial.println("Failed to request creation.");
    return String("");
}

// Format sheets
// REST Reference: https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets/batchUpdate
bool formatSheet(const String spreadsheetId, const int sheetId) {
    if (not readyForNewRequests) { return false; }

    FirebaseJsonArray requests;

    FirebaseJson columnShrinkRequest;
    columnShrinkRequest.set("deleteDimension/range/sheetId", sheetId);
    columnShrinkRequest.set("deleteDimension/range/dimension", "COLUMNS");
    columnShrinkRequest.set("deleteDimension/range/startIndex", 10);    // From the Column K
    requests.add(columnShrinkRequest);

    FirebaseJson headerFormatRequest;
    headerFormatRequest.set("repeatCell/range/sheetId", sheetId);
    headerFormatRequest.set("repeatCell/range/startRowIndex", 0);    // Only the header row
    headerFormatRequest.set("repeatCell/range/endRowIndex", 1);
    headerFormatRequest.set("repeatCell/cell/userEnteredFormat/horizontalAlignment", "LEFT");
    headerFormatRequest.set("repeatCell/cell/userEnteredFormat/textFormat/bold", true);
    headerFormatRequest.set("repeatCell/fields", "userEnteredFormat(horizontalAlignment, textFormat)");
    requests.add(headerFormatRequest);

    FirebaseJson headerFreezeRequest;
    headerFreezeRequest.set("updateSheetProperties/properties/sheetId", sheetId);
    headerFreezeRequest.set("updateSheetProperties/properties/gridProperties/frozenRowCount", 1);
    headerFreezeRequest.set("updateSheetProperties/fields", "gridProperties.frozenRowCount");
    requests.add(headerFreezeRequest);

    FirebaseJson formatRequest;
    formatRequest.set("repeatCell/range/sheetId", sheetId);
    formatRequest.set("repeatCell/range/startRowIndex", 1);    // Below the header row
    formatRequest.set("repeatCell/cell/userEnteredFormat/horizontalAlignment", "RIGHT");
    formatRequest.set("repeatCell/fields", "userEnteredFormat.horizontalAlignment");
    requests.add(formatRequest);

    FirebaseJson timeFormatRequest;
    timeFormatRequest.set("repeatCell/range/sheetId", sheetId);
    timeFormatRequest.set("repeatCell/range/startRowIndex", 1);
    timeFormatRequest.set("repeatCell/range/startColumnIndex", 3);
    timeFormatRequest.set("repeatCell/range/endColumnIndex", 4);
    timeFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    timeFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.0");
    timeFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(timeFormatRequest);

    FirebaseJson tempFormatRequest;
    tempFormatRequest.set("repeatCell/range/sheetId", sheetId);
    tempFormatRequest.set("repeatCell/range/startRowIndex", 1);
    tempFormatRequest.set("repeatCell/range/startColumnIndex", 4);
    tempFormatRequest.set("repeatCell/range/endColumnIndex", 5);
    tempFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    tempFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.00");
    tempFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(tempFormatRequest);

    FirebaseJson humidFormatRequest;
    humidFormatRequest.set("repeatCell/range/sheetId", sheetId);
    humidFormatRequest.set("repeatCell/range/startRowIndex", 1);
    humidFormatRequest.set("repeatCell/range/startColumnIndex", 5);
    humidFormatRequest.set("repeatCell/range/endColumnIndex", 6);
    humidFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    humidFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.00");
    humidFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(humidFormatRequest);

    String response;
    Serial.println("Requesting to format...");
    // Pass (Response container, Spreadsheet Id, ValueRange)
    bool succeeded = GSheet.batchUpdate(&response, spreadsheetId, &requests);
    lastTimeRequestWasSent = millis();

    // For debugging
    // Serial.println(response);

    if (succeeded) {
        Serial.println("Succeeded.");
        return true;
    }
    Serial.println("Failed to request formation.");
    return false;
}

// Extend sheets
// REST Reference: https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets/batchUpdate
bool extendSheetWithFormat(const String spreadsheetId, const int sheetId, const int rows) {
    if (not readyForNewRequests) { return false; }
    if (not(rows >= 0)) { return false; }

    FirebaseJsonArray requests;

    FirebaseJson extendRequest;
    extendRequest.set("appendDimension/sheetId", sheetId);
    extendRequest.set("appendDimension/dimension", "ROWS");
    extendRequest.set("appendDimension/length", rows);
    requests.add(extendRequest);

    FirebaseJson formatRequest;
    formatRequest.set("repeatCell/range/sheetId", sheetId);
    formatRequest.set("repeatCell/range/startRowIndex", 1);    // Below the header row
    formatRequest.set("repeatCell/cell/userEnteredFormat/horizontalAlignment", "RIGHT");
    formatRequest.set("repeatCell/fields", "userEnteredFormat.horizontalAlignment");
    requests.add(formatRequest);

    FirebaseJson timeFormatRequest;
    timeFormatRequest.set("repeatCell/range/sheetId", sheetId);
    timeFormatRequest.set("repeatCell/range/startRowIndex", 1);
    timeFormatRequest.set("repeatCell/range/startColumnIndex", 3);
    timeFormatRequest.set("repeatCell/range/endColumnIndex", 4);
    timeFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    timeFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.0");
    timeFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(timeFormatRequest);

    FirebaseJson tempFormatRequest;
    tempFormatRequest.set("repeatCell/range/sheetId", sheetId);
    tempFormatRequest.set("repeatCell/range/startRowIndex", 1);
    tempFormatRequest.set("repeatCell/range/startColumnIndex", 4);
    tempFormatRequest.set("repeatCell/range/endColumnIndex", 5);
    tempFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    tempFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.00");
    tempFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(tempFormatRequest);

    FirebaseJson humidFormatRequest;
    humidFormatRequest.set("repeatCell/range/sheetId", sheetId);
    humidFormatRequest.set("repeatCell/range/startRowIndex", 1);
    humidFormatRequest.set("repeatCell/range/startColumnIndex", 5);
    humidFormatRequest.set("repeatCell/range/endColumnIndex", 6);
    humidFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/type", "NUMBER");
    humidFormatRequest.set("repeatCell/cell/userEnteredFormat/numberFormat/pattern", "#.00");
    humidFormatRequest.set("repeatCell/fields", "userEnteredFormat.numberFormat(type, pattern)");
    requests.add(humidFormatRequest);

    String response;
    Serial.println("Requesting to extend...");
    // Pass (Response container, Spreadsheet Id, ValueRange)
    bool succeeded = GSheet.batchUpdate(&response, spreadsheetId, &requests);
    lastTimeRequestWasSent = millis();

    // For debugging
    // Serial.println(response);

    if (succeeded) {
        Serial.println("Succeeded.");
        return true;
    }
    Serial.println("Failed to request expansion.");
    return false;
}

// Add a header row for ARIA data
// REST Reference: https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/batchUpdate
bool addSheetAriaHeaderRow(const String spreadsheetId, const char* const sheetTitle) {
    if (not readyForNewRequests) { return false; }

    FirebaseJsonArray valueRanges;

    FirebaseJson ariaHeaderValueRange;
    char rangeCString[13];
    sprintf(rangeCString, "%s!A1:I1", sheetTitle);
    ariaHeaderValueRange.add("range", rangeCString);    // Target range (A1 format)
    ariaHeaderValueRange.add("majorDimension", "COLUMNS");
    ariaHeaderValueRange.set("values/[0]/[0]", "Serial ID");      // Column A
    ariaHeaderValueRange.set("values/[1]/[0]", "Logical ID");     // Column B
    ariaHeaderValueRange.set("values/[2]/[0]", "Packet");         // Column C
    ariaHeaderValueRange.set("values/[3]/[0]", "Time RX [s]");    // Column D
    ariaHeaderValueRange.set("values/[4]/[0]", "Temp [°C]");      // Column E
    ariaHeaderValueRange.set("values/[5]/[0]", "Humid [%]");      // Column F
    ariaHeaderValueRange.set("values/[6]/[0]", "Magnet");         // Column G
    ariaHeaderValueRange.set("values/[7]/[0]", "LQI");            // Column H
    ariaHeaderValueRange.set("values/[8]/[0]", "Power [mV]");     // Column I

    valueRanges.add(ariaHeaderValueRange);

    String response;
    Serial.println("Requesting to add header...");
    // Pass (Response container, Spreadsheet Id, ValueRange)
    bool succeeded = GSheet.values.batchUpdate(&response, spreadsheetId, &valueRanges);
    lastTimeRequestWasSent = millis();

    // For debugging
    // Serial.println(response);

    if (succeeded) {
        Serial.println("Succeeded.");
        return true;
    }
    Serial.println("Failed to request header addition.");
    return false;
}

// Add data received
// REST Reference: https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/batchUpdate
bool addSheetsDataRow(const String spreadsheetId) {
    if (not readyForNewRequests) { return false; }

    FirebaseJsonArray valueRanges;
    uint32_t availableAriaPackets = uxQueueMessagesWaiting(ariaPacketQueue);

    if (not(availableAriaPackets > 0)) {
        Serial.println("There's no ARIA packet. Skip.");
        return false;
    }

    // ARIA
    int ariaRowCount = 0;
    for (int i = 0; i < availableAriaPackets; i++) {
        ParsedAppAriaPacketWithTime packetWithTime;
        if (not(xQueueReceive(ariaPacketQueue, &packetWithTime, 0) == pdPASS)) {
            Serial.println("Failed to get packet data from the queue.");
            break;
        }

        FirebaseJson ariaValueRange;
        // Target range (A1 format)
        char rangeCString[31];
        sprintf(rangeCString, "%s!A%d:I%d", ARIA_SHEET_TITLE, rowToAddNewAriaData + i, rowToAddNewAriaData + i);
        ariaValueRange.add("range", rangeCString);
        ariaValueRange.add("majorDimension", "COLUMNS");
        // Column A
        char serialIdCString[9];
        sprintf(serialIdCString, "%08X", packetWithTime.packet.u32SourceSerialId);
        ariaValueRange.set("values/[0]/[0]", serialIdCString);
        // Column B
        char logicalIdCString[6];
        sprintf(logicalIdCString, "%3d", packetWithTime.packet.u8SourceLogicalId);
        ariaValueRange.set("values/[1]/[0]", logicalIdCString);
        // Column C
        char packetNumberCString[6];
        sprintf(packetNumberCString, "%d", packetWithTime.packet.u16SequenceNumber);
        ariaValueRange.set("values/[2]/[0]", packetNumberCString);
        // Column D
        char timeReceivedCString[11];
        sprintf(timeReceivedCString, "%.2f", packetWithTime.timestamp / 1000.0);
        ariaValueRange.set("values/[3]/[0]", timeReceivedCString);
        // Column E
        char temperatureCString[7];
        sprintf(temperatureCString, "%5.2f", packetWithTime.packet.i16Temp100x / 100.0f);
        ariaValueRange.set("values/[4]/[0]", temperatureCString);
        // Column F
        char humidityCString[6];
        sprintf(humidityCString, "%5.2f", packetWithTime.packet.u16Humid100x / 100.0f);
        ariaValueRange.set("values/[5]/[0]", humidityCString);
        // Column G
        char magnetCString[4];
        switch (packetWithTime.packet.u8MagnetState) {
        case 0x00: {
            sprintf(magnetCString, "%s", "N/A");
            break;
        }
        case 0x01: {
            sprintf(magnetCString, "%s", "N");
            break;
        }
        case 0x02: {
            sprintf(magnetCString, "%s", "S");
            break;
        }
        default:
            sprintf(magnetCString, "%s", "ERR");
            break;
        }
        ariaValueRange.set("values/[6]/[0]", magnetCString);
        // Column H
        char lqiCString[4];
        sprintf(lqiCString, "%d", packetWithTime.packet.u8Lqi);
        ariaValueRange.set("values/[7]/[0]", lqiCString);
        // Column I
        char supplyVoltageCString[5];
        sprintf(supplyVoltageCString, "%d", packetWithTime.packet.u16SupplyVoltage);
        ariaValueRange.set("values/[8]/[0]", supplyVoltageCString);

        valueRanges.add(ariaValueRange);
        ariaRowCount++;
    }

    if (availableAriaPackets > 0) {
        String response;
        Serial.println("Requesting to add data...");
        // Pass (Response container, Spreadsheet Id, ValueRange)
        bool succeeded = GSheet.values.batchUpdate(&response, spreadsheetId, &valueRanges);
        lastTimeRequestWasSent = millis();

        // For debugging
        // Serial.println(response);

        if (succeeded) {
            Serial.println("Succeeded.");
            if (ariaRowCount > 0) {
                rowToAddNewAriaData += ariaRowCount;
            }
            return true;
        }
    }
    Serial.println("Failed to request data addition.");
    return false;
}

/*
 * Copyright (C) 2023 Mono Wireless Inc. All Rights Reserved.
 * Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT).
 */
