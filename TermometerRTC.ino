
extern "C" {
#include "user_interface.h"
}
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Time.h>
#include <stdlib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WebServer.h>
#include <WidgetRTC.h>

// Data wire is plugged into port 2
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// arrays to hold device address
DeviceAddress insideThermometer;

float tempC;
char ssid[] = "Termometer"; // Name of the access point
char auth[] = "874241f382a24ffab44897dbbbe3d7ba";//"2e46004acfa446649327e04bad56fe22"; // Authentication key to Blynk
char Timestring[14]; // Format the time output to the LCD
String message, t_ssdi, t_pw, st, content;

char epromdata[500];
uint32_t *eprom_crc;

const int led = 0;
int pinValue = 1;
int ReadStatus = 0;
int BlynkSTimeout = 0;
float BatteryV;

//Timer instantiate
BlynkTimer SleepTimer;
struct
{
	unsigned long currentSecond;
	uint32_t PWonFlag; // Flag to get NTP on power on or every hour
	uint32_t DoNotConnect; // Flag for not connecting
	uint32_t MINUTES;
	uint32_t TimeSpent;
	uint32_t crc;
}rtcData;

int wifipoints, i, j, numnets, buf_pointer, wifi_cause;
ESP8266WebServer server(80);
String qsid, qpass; //Holds the new credentials from AP

					// LCD Object
Adafruit_PCD8544 MyLcd = Adafruit_PCD8544(12, 5, 4); //software SPI - is it better? For hardware: Adafruit_PCD8544(12, 5, 4)

WidgetRTC rtc;

void setup()
{
	Serial.begin(74880);
	eprom_crc = (uint32_t *) epromdata + 496;
	WiFi.softAPdisconnect(); // Cleanup - might be up from previuos session
	ESP.rtcUserMemoryRead(0, (uint32_t *)&rtcData, sizeof(rtcData));
	if (rtcData.crc != calculateCRC32((uint8_t *)&rtcData, sizeof(rtcData) - 4)) // Check data integrity
	{
		// Data not consistent - clear all
		rtcData.currentSecond = 0;
		rtcData.DoNotConnect = 0;
		rtcData.PWonFlag = 12;
		rtcData.MINUTES = 5;
		rtcData.TimeSpent = 0;
	}
	rtcData.currentSecond += rtcData.MINUTES * 60;
	setTime(rtcData.currentSecond); // set internal timer
	EEPROM.begin(512);
	EEPROM.get(0, epromdata);
	Serial.println("start");
	Serial.println(*eprom_crc);
	if (*eprom_crc != calculateCRC32((uint8_t *)epromdata, 496)) // Initial state of the EEPROM
		epromdata[0] = 0;
	numnets = epromdata[0];
	pinMode(led, OUTPUT);
	digitalWrite(led, 1);
	Wire.begin();
	SetupTemeratureSensor();

	// Nokia Display
	MyLcd.begin();
	MyLcd.setContrast(60);
	MyLcd.setTextColor(BLACK);
	MyLcd.clearDisplay();
	MyLcd.setFont();
	MyLcd.setCursor(0, 0);

	int n = WiFi.scanNetworks(); //  Check if any WiFi in grange
	if (!n || rtcData.DoNotConnect == 5)
	{
		// No access points in range - just be a thermometer
		message = "No WiFi around";
		SleepTimer.setInterval(5 * 1000, SleepTFunc);
		wifi_cause = 5;
		return;
	}
	// Check for known access points to connect
	wifi_cause = ConnectWiFi();
	switch (wifi_cause)
	{
	case 0: //Everything with normal WiFi connection goes here
	{
		MyLcd.clearDisplay();
		MyLcd.setFont();
		Blynk.config(auth, IPAddress(84,40,82,37));
	}
	break;
	case 1: //A known network does not connect
	{
		message = "No " + t_ssdi;
		MyLcd.clearDisplay();
		MyLcd.setCursor(0, 0);
		MyLcd.print(message);
		MyLcd.print("Starting AP");
		MyLcd.setCursor(0, 12);
		MyLcd.display();
		setupAP();
	}
	break;
	case 2: //No known networks
	{
		message = "No known net's";
		MyLcd.setCursor(0, 0);
		MyLcd.print(message);
		MyLcd.print("Starting AP");
		MyLcd.setCursor(0, 12);
		MyLcd.display();
		setupAP();
	}
	break;
	}

	SleepTimer.setInterval(1 * 1000, SleepTFunc);

}

void loop()
{
	if (wifi_cause == 5) // No need to start AP
	{
		SleepTimer.run();
	}
	else if (wifi_cause)
		server.handleClient();
	else
	{
		SleepTimer.run();
		Blynk.run();
		ShowDisplay();
		BatteryV = float(analogRead(A0) / float(1024)*8.86);
	}
}

void SetupTemeratureSensor()
{
	sensors.begin();
	sensors.getDeviceCount();
	sensors.getAddress(insideThermometer, 0);
	sensors.setResolution(insideThermometer, 12);
}


void ShowDisplay(void)
{
	sprintf(Timestring, "%02d:%02d   %02d.%02d", hour(), minute(), day(), month());
	MyLcd.clearDisplay();
	MyLcd.setTextSize(1);
	MyLcd.setFont();
	MyLcd.setCursor(2, 0);
	MyLcd.print(Timestring);
	MyLcd.setFont(&FreeSansBold12pt7b);
	sensors.requestTemperatures();
	tempC = floor(sensors.getTempC(insideThermometer) * 10 + 0.5) / 10;
	MyLcd.setCursor(16, 27);
	if (tempC < 0) MyLcd.setCursor(12, 27);
	MyLcd.print(tempC, 1);
	MyLcd.setFont();
	MyLcd.setCursor(68, 8);
	MyLcd.setTextSize(2);
	MyLcd.print("o");
	MyLcd.setTextSize(1);
	MyLcd.setCursor((84 - 6 * message.length()) / 2, 31);
	MyLcd.print(message);
	MyLcd.setCursor(0, 40);
	MyLcd.print(String("Battery ") + String(BatteryV, 2));
	MyLcd.setCursor(78, 40);
	MyLcd.print("V");
	MyLcd.display();
}

BLYNK_WRITE(V0)
{
	pinValue = param.asInt(); // assigning incoming value from pin V0 to a variable
	digitalWrite(led, pinValue);
	ReadStatus++;
	Serial.println("Got the button");
}

BLYNK_WRITE(V1)
{
	rtcData.MINUTES = param.asInt(); // read sleep time
	ReadStatus++;
	//	Serial.println(String("Minutes: ") + rtcData.MINUTES);
}

//BLYNK_WRITE(V7)
//{
//	MyLcd.setContrast(param.asInt()); // Adjust contrast from Blynk
//	MyLcd.display();
//	ReadStatus++;
//}

BLYNK_CONNECTED()
{
	Blynk.syncAll();
	rtc.begin();
}

void SleepTFunc()
{

	// No inernet connection - just sleep
	if (rtcData.DoNotConnect == 5)
	{
		rtcData.crc = calculateCRC32((uint8_t *)&rtcData, sizeof(rtcData) - 4);
		ESP.rtcUserMemoryWrite(0, (uint32_t *)&rtcData, sizeof(rtcData)); // Store the persistent data before sleep
		ESP.deepSleep(rtcData.MINUTES * 60 * 1000 * 1000); // deep sleep for MINUTES
		delay(500);
	}
	if (!Blynk.connected()) // Not yet connected to server
	{
		if (BlynkSTimeout >= 15) // Blynk server is down - just sleep
		{
			GoSleep();
		}
		BlynkSTimeout++;
		return;
	}
	// Now push the values
	//	Blynk.virtualWrite(V3, rtcData.TimeSpent);
	Blynk.virtualWrite(V5, tempC);
	Blynk.virtualWrite(V4, BatteryV);
	Blynk.virtualWrite(V6, Timestring);
	//	Serial.println(String("ReadStatus: ") + ReadStatus);

	if (ReadStatus < 2) return; // No value of the button yet
	if (pinValue && rtcData.MINUTES) // Light is not ON - going to sleep
	{
		GoSleep();
	}
	return;
}

int ConnectWiFi()
{
	int n = WiFi.scanNetworks();
	buf_pointer = 1;
	if (numnets == 0) return 2;
	for (i = 0; i < numnets; i++)
	{
		t_ssdi = String(epromdata + buf_pointer);
		buf_pointer += t_ssdi.length() + 1;
		t_pw = String(epromdata + buf_pointer);
		buf_pointer += t_pw.length() + 1;
		for (j = 0; j < n; j++)
		{
			if (t_ssdi == String(WiFi.SSID(j)))
			{
				int c = 0;
				WiFi.begin(t_ssdi.c_str(), t_pw.c_str());
				while (c < 20)
				{
					if (WiFi.status() == WL_CONNECTED)
					{
						MyLcd.clearDisplay();
						MyLcd.setCursor(0, 0);
						MyLcd.print("Connected to:");
						MyLcd.setCursor(0, 14);
						MyLcd.print(t_ssdi.c_str());
						MyLcd.display();
						delay(1000);
						return 0;
					}
					MyLcd.clearDisplay();
					MyLcd.print("Connecting ");
					MyLcd.setCursor(0, 14);
					MyLcd.print(c);
					MyLcd.display();
					delay(500);
					c++;
				}
				return 1;
			}

		}

	}
	return 2;
}

void setupAP(void)
{
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	delay(100);
	int n = WiFi.scanNetworks();
	st = "<ol>";
	for (int i = 0; i < n; ++i)
	{
		// Print SSID and RSSI for each network found
		st += "<li>";
		st += WiFi.SSID(i);
		st += "</li>";
	}
	st += "</ol>";
	delay(100);
	WiFi.softAP(ssid);
	launchWeb();
}

void launchWeb(void)
{

	server.on("/", []() {
		IPAddress ip = WiFi.softAPIP();
		String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
		content = "<!DOCTYPE HTML>\r\n";
		content += "<head>\r\n";
		content += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\r\n";
		content += "<title>Точка за достъп</title>";
		content += "<head>\r\n";
		content += ipStr;
		content += "<p>";
		content += st;
		content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
		content += "</html>";
		server.send(200, "text/html; charset=utf-8", content);
	});
	server.on("/setting", []() {
		qsid = server.arg("ssid");
		qpass = server.arg("pass");
		if (qsid.length() > 0 && qpass.length() > 0)
		{
			// Should write qsid & qpass to EEPROM
			if (wifi_cause == 1) remove_ssdi();
			if (!append_ssdi())
			{
				content = "No more room for access points";
			}
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>saved to eeprom... reset to boot into new wifi</p></html>";
		}
		else {
			content = "Въведете правилни креденции на предишната страница\n\r";
			content += "Или изберете без връзка\n\r";
			content += "</p><form method='get' action='setting'><input name='confirm' length=0><input type='submit'></form>";
		}
		server.send(200, "text/html; charset=utf-8", content);
	});

	server.on("/setting", []() {
		qsid = server.arg("confirm");
		rtcData.DoNotConnect = 5;
	});
	server.begin();
	SleepTimer.setTimeout(5 * 60 * 1000, GOrestart); // just reset if no answer

}

bool append_ssdi(void)
{
	epromdata[0]++;
	if (epromdata[0] > 10)
		return FALSE;
	for (i = 0; i < qsid.length(); i++)
		epromdata[i + buf_pointer] = qsid[i];
	buf_pointer += qsid.length();
	epromdata[buf_pointer] = 0;
	buf_pointer++;
	for (i = 0; i < qpass.length(); i++)
		epromdata[i + buf_pointer] = qpass[i];
	buf_pointer += qpass.length();
	epromdata[buf_pointer] = 0;
	buf_pointer++;
	*eprom_crc = calculateCRC32((uint8_t *)epromdata, 496);
	Serial.println("end");
	Serial.println(*eprom_crc);
	Serial.println((uint32_t)*(epromdata + 496));
	EEPROM.put(0, epromdata);
	EEPROM.commit();
	return TRUE;
}

void remove_ssdi(void)
{
	epromdata[0]--;
	if (epromdata[0] == 0)
	{
		buf_pointer = 1;
		return; // No saved networks left
	}
	int block = t_ssdi.length() + t_pw.length() + 2;

	int old_pointer = buf_pointer - block; //Dest. pointer - points the ssdi to be removed
	for (i = 0; i < 512 - buf_pointer; i++)
		epromdata[old_pointer + i] = epromdata[buf_pointer + i];
	// Adjust the pointer
	buf_pointer = 1;
	for (i = 0; i < epromdata[0]; i++)
	{
		t_ssdi = String(epromdata + buf_pointer);
		buf_pointer += t_ssdi.length() + 1;
		t_pw = String(epromdata + buf_pointer);
		buf_pointer += t_pw.length() + 1;
	}
}

uint32_t calculateCRC32(const uint8_t *data, size_t length)
{
	uint32_t crc = 0xffffffff;
	while (length--) {
		uint8_t c = *data++;
		for (uint32_t i = 0x80; i > 0; i >>= 1) {
			bool bit = crc & 0x80000000;
			if (c & i) {
				bit = !bit;
			}
			crc <<= 1;
			if (bit) {
				crc ^= 0x04c11db7;
			}
		}
	}
	return crc;
}


void GOrestart()
{
	ESP.restart();
}

void GoSleep(void)
{
	rtcData.TimeSpent = now() - rtcData.currentSecond;
	rtcData.currentSecond = now();
	rtcData.crc = calculateCRC32((uint8_t *)&rtcData, sizeof(rtcData) - 4);
	ESP.rtcUserMemoryWrite(0, (uint32_t *)&rtcData, sizeof(rtcData)); // Store the persistent data before sleep
	ESP.deepSleep(rtcData.MINUTES * 60 * 1000 * 1000); // deep sleep for MINUTES
	delay(100);
}