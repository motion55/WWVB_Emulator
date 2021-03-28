
#if defined(ESP8266)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

#include <Wire.h>
#include <SPI.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

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

/*///////////////////////////////////////////////////////////////////////////*/

void setup() {
	// put your setup code here, to run once:
	Serial.begin(115200);

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

void loop() {
	// put your main code here, to run repeatedly:
	uint32_t current_millis = millis();
	if  ((current_millis - last_update) >= update_interval) 
	{
		last_update = current_millis;
		update_interval = 1000;

		UpdateTime();
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

void UpdateTime(void)
{
	time_t tm = now();

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

const int timeZone = 8 * SECS_PER_HOUR;     // PHT
#define MAX_PACKET_DELAY	1500
uint32_t send_Timestamp;

time_t getNtpTime()
{
	IPAddress addr;
	if (WiFi.hostByName(ntpServerName, addr))
	{
		timeServerIP = addr;
	}

	Serial.println(F("Transmit NTP Request"));
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
			Serial.println(F("Receive NTP Response"));
			udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;

			// convert four bytes starting at location 40 to a long integer
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];

			uint32_t pingTime = (endWait - send_Timestamp)/2;
			Serial.print("receive time = ");
			Serial.println(pingTime);

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
			Serial.print("endWait - last_update = ");
			Serial.println(update_interval);

			update_interval += frac_sec;
			delta += update_interval;

			if ((endWait>3600000L)||first_hour)
			{
				setSyncInterval(3600);	//Update after 1 hour.
				first_hour = 1;
			}
			else
			{
				setSyncInterval(30);	//Update after 30 for the 1st hourt.
			}

			time_t tm = secsSince1900 - 2208988800UL + timeZone;
			setTime(tm);

			Serial.print("frac_sec = ");
			Serial.print(frac_sec);
			Serial.print(" delta = ");
			Serial.print(delta);
			Serial.print(" new update_interval = ");
			Serial.println(update_interval);
		}
	}
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address)
{
	if (WiFi.status() == WL_CONNECTED)
	{
		Serial.print(F("sending NTP packet to "));
		Serial.println(address);
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

