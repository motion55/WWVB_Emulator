
#if defined(ESP8266)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

#include <Wire.h>
#include <SPI.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "core_esp8266_waveform.h"

//#include <Time.h>
#include <TimeLib.h>

// Access point credentials
String ap_ssid("WWVB");
const char *ap_password = "12345678";

String sta_ssid("BST2.4G");	//  your network SSID (name)
String sta_pass;		        // your network password

unsigned int localPort = 2390;      // local port to listen for UDP packets

const char* ntpServerName = "pool.ntp.org";	//"ntp.pagasa.dost.gov.ph";
IPAddress timeServerIP(192, 168, 7, 1);	//IP address of "ntp.pagasa.dost.gov.ph"

// NTP time stamp is in the first 48 bytes of the message
const int NTP_PACKET_SIZE = 48; 

//buffer to hold incoming and outgoing packets
byte packetBuffer[NTP_PACKET_SIZE]; 

//A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

char TimeText[] = "00:00:00am \0";
//                   01234567890

void UpdateTime(void);
void sendNTPpacket(IPAddress& address);
void my_delay_ms(int msec);

uint32_t last_update = 0;
uint32_t update_interval = 1000;
bool first_hour = 0;

#define	WWVB_EnaPin 	D3	//GPIO0
#define	WWVB_OutPin 	D8	//GPIO15
#define	WWVB_OutInv 	D4	//GPIO2
#define	WWVB_60KHZ	 	D2	//GPIO4

#define MARK_ 	{stopWaveform(WWVB_60KHZ);	\
	digitalWrite(WWVB_60KHZ, LOW);	\
	digitalWrite(WWVB_OutPin, LOW); digitalWrite(WWVB_OutInv, HIGH);}
#define SPACE 	{	\
	startWaveformClockCycles(WWVB_60KHZ, 1000, 1000, F_CPU);	\
	digitalWrite(WWVB_OutPin, HIGH); digitalWrite(WWVB_OutInv, LOW);}

bool WWVB_Enable = 0;

/*///////////////////////////////////////////////////////////////////////////*/

void setup() {
	// put your setup code here, to run once:
	Serial.begin(115200);

	pinMode(WWVB_EnaPin, INPUT);
	pinMode(WWVB_OutPin, OUTPUT);
	digitalWrite(WWVB_OutPin, LOW);
	pinMode(WWVB_OutInv, OUTPUT);
	digitalWrite(WWVB_OutPin, HIGH);
	pinMode(WWVB_60KHZ, OUTPUT);
	digitalWrite(WWVB_60KHZ, LOW);

	IPAddress local_IP(192, 168, 25, 1);
	IPAddress gateway(192, 168, 25, 1);
	IPAddress subnet(255, 255, 255, 0);

	WiFi.softAPConfig(local_IP, gateway, subnet);

	String macAddr = WiFi.softAPmacAddress();
	ap_ssid += '_' + macAddr.substring(12, 14) + macAddr.substring(15);
	WiFi.softAP(ap_ssid.c_str(), ap_password);
	WiFi.hostname(ap_ssid);

	if (WiFi.SSID().length()>0)
	{
		sta_ssid = WiFi.SSID();
		sta_pass = WiFi.psk();
	}

	delay(1000);

	for (int i=0; i<100; i++)
	{
		Serial.println(F("Connecting to WiFi"));
		if (WiFi.status() == WL_CONNECTED)
		{
			Serial.println(F("WiFi connected"));
			Serial.print(F("IP address: "));
			Serial.println(WiFi.localIP());
			break;
		}
		delay(100);
	}

	delay(2000);

	Serial.println(F("Starting UDP"));
	udp.begin(localPort);
	Serial.print(F("Local port: "));
	Serial.println(udp.localPort());

	setSyncProvider(getNtpTime);

	webserver_setup();

	last_update = millis();
	update_interval = 1000;
}

uint32_t pulse_off_delay = 1000;

void loop() {
	// put your main code here, to run repeatedly:
	uint32_t current_millis = millis();
	if  ((current_millis - last_update) >= update_interval) 
	{
		last_update = current_millis;
		update_interval = 1000;

		UpdateTime();

//		if (digitalRead(WWVB_EnaPin)==LOW)
		{
			WWVB_Enable = 1;	
		}
	}
	else if  ((current_millis - last_update) >= pulse_off_delay) 
	{
		if (WWVB_Enable)
		{
			SPACE;
			WWVB_Enable = 0;	
		}
	}

	webserver_loop();

	my_delay_ms(50);
}

/*///////////////////////////////////////////////////////////////////////////*/

String GetDateStr(void)
{
	tmElements_t tm;
	breakTime(now(),tm);
	String DateStr(monthShortStr(tm.Month));
	DateStr += " " + String(tm.Day) + " " + dayShortStr(tm.Wday) + " ";
	return DateStr;
}

static  const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0
#define LEAP_YEAR(Y)     ( !(Y%4) && ((Y%100) || !(Y%400)) )

uint16_t GetDayoftheYear(time_t tm)
{
	int buwan = month(tm)-1;
	uint16_t DayoftheYear = 0;
	
	for (int i = 0; i<buwan; i++)
	{
		DayoftheYear += monthDays[i];
	}

	if (buwan>1) 
	{
		int taon = year(tm);
		if (LEAP_YEAR(taon))
		{
			DayoftheYear += 1;
		}
	}

	int Day = day(tm);
	DayoftheYear += Day;

	return DayoftheYear;
}

#define	PULSE_ZERO		{pulse_off_delay = 200; Serial.print(" 0 ");}
#define	PULSE_ONE		{pulse_off_delay = 500; Serial.print(" 1 ");}
#define	PULSE_MARKER	{pulse_off_delay = 800; Serial.print(" M ");}

void WWVB_Begin(time_t tm)
{
	MARK_;

	int sec = second(tm);
	int min = minute(tm);
	int min10 = min / 10;
	min = min % 10;
	int hr = hour(tm);
	int hr10 = hr/10;
	hr = hr % 10;
	int DoY = GetDayoftheYear(tm);
	int DoY10 = DoY/10;
	int DoY100 = DoY/100;
	DoY = DoY % 10;
	int taon = year(tm);
	bool LeapYear = LEAP_YEAR(taon);
	int taon10 = taon/10;
	taon10 = taon10 % 10;
	taon = taon % 10;

	DoY = DoY % 10;

	switch (sec)
	{
		default:
		case 4:
		case 10:
		case 11:
		case 14:
		case 20:
		case 21:
		case 24:
		case 34:
		case 35:
		case 44:
		case 54:
			PULSE_ZERO;
			break;
		case 0:
			WWVB_Enable = 0;
		case 9:
		case 19:
		case 29:
		case 39:
		case 49:
		case 59:
			PULSE_MARKER;
			break;
		case 1:
			if (min10&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 2:
			if (min10&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 3:
			if (min10&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 5:
			if (min&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 6:
			if (min&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 7:
			if (min&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 8:
			if (min&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 12:
			if (hr10&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 13:
			if (hr10&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 15:
			if (hr&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 16:
			if (hr&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 17:
			if (hr&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 18:
			if (hr&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 22:
			if (DoY100&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 23:
			if (DoY100&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 25:
			if (DoY10&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 26:
			if (DoY10&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 27:
			if (DoY10&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 28:
			if (DoY10&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 30:
			if (DoY&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 31:
			if (DoY&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 32:
			if (DoY&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 33:
			if (DoY&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 36:
		case 38:
		case 40:
		case 41:
			PULSE_ZERO;
			break;
		case 37:
		case 42:
		case 43:
			PULSE_ONE;
			break;
		case 45:
			if (taon10&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 46:
			if (taon10&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 47:
			if (taon10&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 48:
			if (taon10&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 50:
			if (taon&0x8)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 51:
			if (taon&0x4)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 52:
			if (taon&0x2)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 53:
			if (taon&0x1)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
		case 55:
			if (LeapYear)
				PULSE_ONE
			else
				PULSE_ZERO;
			break;
	}
}

void UpdateTime(void)
{
	time_t tm = now();

	WWVB_Begin(tm);

	int hour = hourFormat12(tm);
	if (hour < 10)
	{
		TimeText[0] = ' ';
		TimeText[1] = '0' + hour;
	}
	else
	{
		TimeText[0] = '1';
		TimeText[1] = '0' + (hour-10);
	}

	int min = minute(tm);
	int min10 = min / 10;
	TimeText[3] = '0' + min10;
	TimeText[4] = '0' + min - (min10 * 10);

	int sec = second(tm);
	int sec10 = sec / 10;
	TimeText[6] = '0' + sec10;
	TimeText[7] = '0' + sec - (sec10 * 10);

	if (isAM(tm))
	{
		TimeText[8] = 'a';
		TimeText[9] = 'm';
	}
	else
	{
		TimeText[8] = 'p';
		TimeText[9] = 'm';
	}

	Serial.print(" The time is ");
	Serial.print(TimeText);
	Serial.println("");
}

const int timeZone = 13 * SECS_PER_HOUR;     // PHT + EST = 13
#define MAX_PACKET_DELAY	1500
uint32_t send_Timestamp;

time_t getNtpTime()
{
	IPAddress addr;
	if (WiFi.hostByName(ntpServerName, addr))
	{
		timeServerIP = addr;
	}
#ifdef _DEBUG_NTP_
	Serial.println(F("Transmit NTP Request"));
#endif	
	sendNTPpacket(timeServerIP); // send an NTP packet to a time server
	setSyncInterval(300);	//retry after 5 minutes

	return 0;
}

void my_delay_ms(int msec)
{
	uint32_t delay_val = msec;
	uint32_t endWait = millis();
	uint32_t beginWait = endWait;
	while ((endWait - beginWait) < delay_val) 
	{
		CheckNtpPacket(endWait);
		delay(1);
		endWait = millis();
	}
}

void CheckNtpPacket(uint32_t endWait)
{
	uint32_t max_packet_delay = MAX_PACKET_DELAY;
	int size = udp.parsePacket();
	if ((endWait - send_Timestamp) < max_packet_delay)
	{
		if (size >= NTP_PACKET_SIZE)
		{
#ifdef _DEBUG_NTP_
			Serial.println(F("Receive NTP Response"));
#endif			
			udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;

			// convert four bytes starting at location 40 to a long integer
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];

			uint32_t pingTime = (endWait - send_Timestamp)/2;
#ifdef _DEBUG_NTP_
			Serial.print("receive time = ");
			Serial.println(pingTime);
#endif
			uint32_t frac_sec = (unsigned long)packetBuffer[44] << 8;
			frac_sec += (unsigned long)packetBuffer[45];
			frac_sec *= 1000;
			frac_sec /= 65536;
			frac_sec += pingTime;

			secsSince1900 += 1;
			if (frac_sec>=1500)
				secsSince1900 += 1;

			int32_t delta = -update_interval;

			update_interval = endWait - last_update;
#ifdef _DEBUG_NTP_
			Serial.print("endWait - last_update = ");
			Serial.println(update_interval);
#endif
			update_interval += frac_sec;
			delta += update_interval;

			if ((endWait>3600000L)||first_hour)
			{
				setSyncInterval(3600);	//Update after 1 hour.
				first_hour = 1;
			}
			else
			{
#ifdef _DEBUG_NTP_
				setSyncInterval(30);	//Update after 30 for the 1st hourt.
#else
				setSyncInterval(300);	//Update after 300 for the 1st hourt.
#endif				
			}

			time_t tm = secsSince1900 - 2208988800UL + timeZone;
			setTime(tm);
#ifdef _DEBUG_NTP_
			Serial.print("frac_sec = ");
			Serial.print(frac_sec);
			Serial.print(" delta = ");
			Serial.print(delta);
			Serial.print(" new update_interval = ");
			Serial.println(update_interval);
#endif			
		}
	}
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address)
{
	if (WiFi.status() == WL_CONNECTED)
	{
#ifdef _DEBUG_NTP_
		Serial.print(F("sending NTP packet to "));
		Serial.println(address);
#endif		
		// set all bytes in the buffer to 0
		memset(packetBuffer, 0, NTP_PACKET_SIZE);
		// Initialize values needed to form NTP request
		// (see URL above for details on the packets)
		packetBuffer[0] = 0b11100011;   // LI, Version, Mode
		packetBuffer[1] = 0;     // Stratum, or type of clock
		packetBuffer[2] = 6;     // Polling Interval
		packetBuffer[3] = 0xEC;  // Peer Clock Precision
								 // 8 bytes of zero for Root Delay & Root Dispersion
		packetBuffer[12] = 49;
		packetBuffer[13] = 0x4E;
		packetBuffer[14] = 49;
		packetBuffer[15] = 52;

		send_Timestamp = millis();

		// all NTP fields have been given values, now
		// you can send a packet requesting a timestamp:
		udp.beginPacket(address, 123); //NTP requests are to port 123
		udp.write(packetBuffer, NTP_PACKET_SIZE);
		udp.endPacket();
	}
}

