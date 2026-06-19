/*****************************************************************
 * CONFIG
 *****************************************************************/
#define FW_VERSION "1.0.0" // Change this as per your version

#define MODEM_RX 16
#define MODEM_TX 17
#define LED_PIN 19

const unsigned long OTA_INTERVAL = 3UL * 60UL * 60UL * 1000UL;   // 3 hours

/*****************************************************************
 * GLOBALS
 *****************************************************************/

#include <HardwareSerial.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <Update.h>

HardwareSerial modem(2);

size_t firmwareSize = 0;

String serverVersion = "";

unsigned long lastOtaCheck = 0;

/*****************************************************************
 * OTA STRUCTS
 *****************************************************************/

struct FirmwareChunk
{
    uint8_t data[1024];
    int len;
};

// Use the following appsetup and apploop for functional code:

/*****************************************************************
 * USER SETUP
 *****************************************************************/
void appSetup()
{
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(LED_PIN, LOW);
}

/*****************************************************************
 * USER LOOP
 *****************************************************************/
void appLoop()
{
    static bool ledState = false;
    static unsigned long lastBlink = 0;

    if (millis() - lastBlink >= 1000)
    {
        lastBlink = millis();

        ledState = !ledState;

        digitalWrite(
            LED_PIN,
            ledState
        );
    }
}

// Following is OTA and Modem functions:

/*****************************************************************
 * MODEM FUNCTIONS
 *****************************************************************/

bool sendAT(
    String cmd,
    String expected,
    int timeout = 5000
)
{
    Serial.print(">> ");
    Serial.println(cmd);

    modem.println(cmd);

    String response = "";

    unsigned long start = millis();

    while (millis() - start < timeout)
    {
        while (modem.available())
        {
            char c = modem.read();

            response += c;

            Serial.write(c);
        }

        if (response.indexOf(expected) >= 0)
        {
            return true;
        }
    }

    return false;
}


void modemInit()
{
    Serial.println("\n===== EC200U INIT =====");
    // MODEM RESET AT COMMAND
    // sendAT("AT+CFUN=1,1","OK",10000);
    // delay(30000);
    sendAT("AT", "OK");
    sendAT("ATI", "OK");
    sendAT("AT+CPIN?", "READY");
    sendAT("AT+CSQ", "OK");
    sendAT("AT+COPS?", "OK");
    sendAT("AT+CEREG?", "OK");
    sendAT("AT+CGREG?", "OK");
    sendAT("AT+CGATT?", "OK");
    sendAT("AT+CGDCONT?", "OK");
    sendAT("AT+QICSGP=1", "OK");
    sendAT("AT+CGACT?", "OK");
    sendAT("AT+CGPADDR=1", "OK");
    sendAT("AT+QCFG=\"usbnet\"", "OK");
    sendAT("AT+QIACT?", "OK");

    Serial.println("Waiting for network settle...");

    delay(10000);

    Serial.println("\n===== MODEM READY =====");
}
/*****************************************************************
 * OTA FUNCTIONS
 *****************************************************************/

//============ OTA HELPERS ==============

FirmwareChunk readFirmwareChunk(int handle)
{
    FirmwareChunk chunk;
    chunk.len = 0;

    String cmd = "AT+QFREAD=" + String(handle) + ",1024";

    modem.println(cmd);

    uint8_t raw[1400];
    int rawLen = 0;

    unsigned long start = millis();

    while(millis() - start < 100)
    {
        while(modem.available())
        {
            if(rawLen < sizeof(raw))
            {
                raw[rawLen++] = modem.read();
            }

            start = millis();
        }
    }

    // Parse CONNECT size
    String header = "";

    for(int i = 0; i < 30 && i < rawLen; i++)
    {
        header += (char)raw[i];
    }

    int payloadSize = 0;

    int pos = header.indexOf("CONNECT ");

    if(pos >= 0)
    {
        payloadSize = header.substring(pos + 8).toInt();
    }

    // Find payload start (after second CRLF)
    int payloadStart = -1;

    for(int i = 0; i < rawLen - 1; i++)
    {
        if(raw[i] == '\r' && raw[i + 1] == '\n')
        {
            i += 2;

            for(; i < rawLen - 1; i++)
            {
                if(raw[i] == '\r' && raw[i + 1] == '\n')
                {
                    payloadStart = i + 2;
                    break;
                }
            }

            break;
        }
    }

    if(payloadStart < 0)
    {
        Serial.println("Could not find payload start");
        return chunk;
    }

    chunk.len = payloadSize;

    if(chunk.len > sizeof(chunk.data))
    {
        chunk.len = sizeof(chunk.data);
    }
    if(payloadStart + chunk.len > rawLen)
    {
        chunk.len = rawLen - payloadStart;
    }
    
    memcpy(
        chunk.data,
        &raw[payloadStart],
        chunk.len
    );

    return chunk;
}

//========== OTA VERSION CHECK =============
String fetchVersion()
{
  String response = "";

  sendAT("AT+QSSLCFG=\"seclevel\",1,0", "OK");
  sendAT("AT+QHTTPCFG=\"sslctxid\",1", "OK");
  sendAT("AT+QHTTPCFG=\"contextid\",1", "OK");

  modem.println("AT+QHTTPURL=62,30");
  delay(1000);

  while(modem.available()) modem.read();

  modem.println("https://relation-armhole-uncounted.ngrok-free.dev/version.json");

  delay(2000);

  while(modem.available()) modem.read();

  modem.println("AT+QHTTPGET=60");
  delay(5000);

  while(modem.available()) modem.read();

  modem.println("AT+QHTTPREAD=60");

  unsigned long start = millis();

  while(millis() - start < 5000)
  {
    while(modem.available())
    {
      char c = modem.read();
      response += c;
      Serial.write(c);
    }
  }

  int pos = response.indexOf("\"version\"");

  if(pos >= 0)
  {
      int colon =
          response.indexOf(':', pos);

      int q1 =
          response.indexOf('"', colon);

      int q2 =
          response.indexOf('"', q1 + 1);

      serverVersion =
          response.substring(q1 + 1, q2);

      Serial.print("Server Version = ");
      Serial.println(serverVersion);
  }
  return response;
}

//========== OTA DOWNLOAD =============
void downloadFirmware()
{
  Serial.println("Downloading firmware...");

  sendAT("AT+QSSLCFG=\"seclevel\",1,0", "OK");
  sendAT("AT+QHTTPCFG=\"sslctxid\",1", "OK");
  sendAT("AT+QHTTPCFG=\"contextid\",1", "OK");

  modem.println("AT+QHTTPURL=62,30");
  delay(1000);

  while(modem.available())
  {
    Serial.write(modem.read());
  }

  modem.println("https://relation-armhole-uncounted.ngrok-free.dev/firmware.bin");

  delay(2000);

  while(modem.available())
  {
    Serial.write(modem.read());
  }

  String response = "";

  modem.println("AT+QHTTPGET=120");

  delay(10000);

  while(modem.available())
  {
      char c = modem.read();
      response += c;
      Serial.write(c);
  }

  int pos = response.indexOf("+QHTTPGET:");

  if(pos >= 0)
  {
      int lastComma = response.lastIndexOf(',');

      if(lastComma > pos)
      {
          firmwareSize =
              response.substring(lastComma + 1).toInt();

          Serial.print("Firmware Size = ");
          Serial.println(firmwareSize);
      }
  }

  // delete old file first
  modem.println("AT+QFDEL=\"UFS:update.bin\"");
  delay(2000);

  while(modem.available())
  {
    Serial.write(modem.read());
  }

  modem.println("AT+QHTTPREADFILE=\"update.bin\",120");

  delay(15000);

  while(modem.available())
  {
    Serial.write(modem.read());
  }

  delay(3000);

  Serial.println("Download complete");
  sendAT("AT+QFLST", "OK", 5000);
}

//========== OTA FLASH =============
void performOTA()
{
    Serial.println("\n===== OTA START =====");

    modem.println("AT+QFOPEN=\"UFS:update.bin\",0");

    delay(3000);

    String response = "";

    while(modem.available())
    {
        response += (char)modem.read();
    }

    int handle = -1;

    int pos = response.indexOf("+QFOPEN:");

    if(pos >= 0)
    {
        String temp = response.substring(pos + 8);
        temp.trim();
        handle = temp.toInt();
    }

    if(handle < 0)
    {
        Serial.println("Open failed");
        return;
    }

    const esp_partition_t* part =
        esp_ota_get_next_update_partition(NULL);

    if(part == NULL)
    {
        Serial.println("OTA partition not found");
        return;
    }

    if(firmwareSize > part->size)
    {
        Serial.println("Firmware too large for OTA partition");
        return;
    }

    if(firmwareSize == 0)
    {
        Serial.println("Invalid firmware size");
        return;
    }

    if(!Update.begin(firmwareSize))
    {
        Serial.println("Update.begin failed");
        return;
    }

    Serial.println("Update.begin OK");
    bool otaFailed = false;
    size_t totalWritten = 0;

    while(true)
    {
        FirmwareChunk chunk = readFirmwareChunk(handle);

        if(chunk.len <= 0)
        {
            Serial.println("Read failed");
            Update.abort();
            otaFailed = true;
            break;
        }

        size_t written =
            Update.write(
                chunk.data,
                chunk.len
            );

        if(written != chunk.len)
        {
            Serial.println("Write failed");
            Update.abort();
            otaFailed = true;
            break;
        }

        totalWritten += written;

        Serial.print("Written = ");
        Serial.println(totalWritten);

        if(chunk.len < 1024)
        {
            Serial.println("Last chunk");
            break;
        }
    }

    if(otaFailed)
    {
        sendAT(
            "AT+QFCLOSE=" + String(handle),
            "OK"
        );

        Serial.println("OTA aborted");
        return;
    }

    Serial.print("Final Written = ");
    Serial.println(totalWritten);

    sendAT(
        "AT+QFCLOSE=" + String(handle),
        "OK"
    );

    bool result = Update.end(true);

    Serial.print("Update.end = ");
    Serial.println(result);

    if(!result)
    {
        Serial.print("Error = ");
        Serial.println(Update.getError());
        return;
    }

    if(totalWritten != firmwareSize)
    {
        Serial.println("Size mismatch");
        return;
    }

    Serial.println("OTA SUCCESS");
    Serial.println("Rebooting in 5 seconds...");

    delay(5000);

    ESP.restart();
}

//========== OTA MANAGER =============
void checkFreeOtaSpace()
{
  const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);

  Serial.print("OTA partition size: ");

  if(part)
    Serial.println(part->size);
  else
    Serial.println("Not Found");
}

void checkForUpdates()
{
  String json = fetchVersion();

  if(serverVersion != FW_VERSION)
  {
    Serial.println();
    Serial.println("UPDATE AVAILABLE");

    downloadFirmware();
    performOTA();
    return;
  }
  else
  {
    Serial.println("NO UPDATE");
  }
}

/*****************************************************************
 * SYSTEM SETUP
 *****************************************************************/

void setup()
{
    Serial.begin(115200);

    modem.begin(
        115200,
        SERIAL_8N1,
        MODEM_RX,
        MODEM_TX
    );

    delay(10000);

    Serial.println();
    Serial.print("VERSION: ");
    Serial.println(FW_VERSION);

    modemInit();

    // Immediate OTA check on boot
    checkForUpdates();

    // Start OTA timer after boot check completes
    lastOtaCheck = millis();

    // User initialization
    appSetup();

    Serial.println();
    Serial.println("===== SYSTEM READY =====");
}

/*****************************************************************
 * SYSTEM LOOP
 *****************************************************************/

void loop()
{
    // User application code
    appLoop();

    // Periodic OTA check
    if (millis() - lastOtaCheck >= OTA_INTERVAL)
    {
        Serial.println("\n===== PERIODIC OTA CHECK =====");

        checkForUpdates();

        lastOtaCheck = millis();
    }
}
