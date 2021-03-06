/*
 Name:		AdmiraltyTidalApiClient.h
 Created:	10/21/2018 10:43:30 AM
 Author:	Stewart McSporran
 Editor:	http://www.visualmicro.com

 A library that acts as a client to the UK Admiralty's
 Tidal forecaset API.

 See https://admiraltyapi.portal.azure-api.net

 Library hosted at https://github.com/Scratchydisk/ESP8266-Admiralty-Tidal-API

 Released under the MIT license.

Copyright (c) 2018 Stewart McSporran

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "AdmiraltyTidalApiClient.h"

// Default ctor
// Sets the SSL validation option to none.
AdmiraltyApiClient::AdmiraltyApiClient()
{
	jsonParser.setListener(this);
	validateSslThumbprint = false;
}

// Read tidal events from the Admiralty API for the supplied station and number of days forecast.
uint8_t AdmiraltyApiClient::FetchTidalEvents(WiFiClientSecure wifiClient, uint8_t numberDays)
{
	// Can we resolve the host IP?
	IPAddress hostIp;
	if (!WiFi.hostByName(ADMIRALTY_API_HOST.c_str(), hostIp))
	{
		DEBUGV("HostByName failed for " + String(ADMIRALTY_API_HOST));
		return ADMIRALTY_API_HOSTBYNAME_FAILED;
	}

	DEBUGV("IP of " + ADMIRALTY_API_HOST + " is " + hostIp.toString());
	DEBUGV("\nStarting connection to server...");

	// ESP8266 API's TLS library has changed from 
	// axTLS to BearSSL, so needs this...
	if (!validateSslThumbprint)
		wifiClient.setInsecure();

	if (!wifiClient.connect(hostIp, 443))
	{
		DEBUGV("Connection failed!");
		return ADMIRALTY_API_CONNECT_FAILED;
	}

	if (validateSslThumbprint)
	{
		bool sslValid = wifiClient.verify(SSL_THUMBRPRINT_API, ADMIRALTY_API_HOST.c_str());
		if (!sslValid)
		{
			DEBUGV("SSL Validation failed");
			return ADMIRALTY_API_SSL_VALIDATION_FAILED;
		}
	}

	DEBUGV("Connected to server.  Calling...");
	String fullGet = "GET " + ADMIRALTY_STATIONS_URL + stationId + "/TidalEvents?duration=" + String(numberDays) + " HTTP/1.1";

	DEBUGV(fullGet);

	// Make the HTTP request:
	wifiClient.println(fullGet);
	wifiClient.println("Host: " + ADMIRALTY_API_HOST);
	wifiClient.println("Ocp-Apim-Subscription-Key:" + apiSubscriptionKey);
	wifiClient.println("Connection: close");
	wifiClient.println();

	// Read through the headers...
	// Two \r in a row indicate headers complete
	while (wifiClient.connected())
	{
		String line = wifiClient.readStringUntil('\n');
		if (line == "\r")
		{
			DEBUGV("Finished Receiving Headers");
			break;
		}
		DEBUGV(line);
	}

	// Read and parse the body of the message
	while (wifiClient.available())
	{
		char c = wifiClient.read();
		jsonParser.parse(c);
	}

	wifiClient.stop();

	return ADMIRALTY_API_SUCCESS;
}

TidalEvent AdmiraltyApiClient::PreviousTidalEvent(time_t time)
{
	TidalEvent bestMatch = TidalEvent();

	// events are stored in time order
	for (int i = 0; i < numberEvents; i++)
	{
		TidalEvent te = tidalEvents[i];

		if (te.epochTime <= time)
		{
			bestMatch = te;
		}
		else
		{
			break;
		}
	}

	return bestMatch;
}

TidalEvent AdmiraltyApiClient::NextTidalEvent(time_t time)
{
	TidalEvent bestMatch = TidalEvent();

	// events are stored in time order
	for (int i = 0; i < numberEvents; i++)
	{
		TidalEvent te = tidalEvents[i];

		if (te.epochTime > time)
		{
			bestMatch = te;
			break;
		}
	}

	return bestMatch;
}

// Converts the ISO8601 string time representation from the API
// into time elements.  Fractional parts of seconds are dropped.
void AdmiraltyApiClient::convertFromIso8601(String time_string, tmElements_t& tm_data)
{
	// 2018-10-17T17:25:00
	// substring offsets:
	// Y - 0 4
	// M - 5 7
	// D - 8 10
	// H - 11 13
	// MM - 14 16
	// S - 17 19
	// Can be fractional seconds but we'll ignore them

	tm_data.Year = time_string.substring(0, 4).toInt() - 1970;
	tm_data.Month = time_string.substring(5, 7).toInt();
	tm_data.Day = time_string.substring(8, 10).toInt();
	tm_data.Hour = time_string.substring(11, 13).toInt();
	tm_data.Minute = time_string.substring(14, 16).toInt();
	tm_data.Second = time_string.substring(17, 19).toInt();
}

/******
* JSON Parser overrides
*******/

void AdmiraltyApiClient::whitespace(char c)
{
	DEBUGV("whitespace");
}

void AdmiraltyApiClient::startDocument() {
	DEBUGV("start document");
	eventIndex = 0;
	numberEvents = 0;
}

void AdmiraltyApiClient::key(String key) {
	DEBUGV("key: " + key);
	currentKey = key;
}

void AdmiraltyApiClient::value(String value) {
	DEBUGV("value: " + value);
	if (currentKey == "EventType")
	{
		tidalEvents[eventIndex].isHighTide = value == "HighWater";
	}
	else if (currentKey == "DateTime")
	{
		tidalEvents[eventIndex].dateTime = value;
		memset(&(tidalEvents[eventIndex].tm), 0, sizeof(tmElements_t));
		convertFromIso8601(value, tidalEvents[eventIndex].tm);
	}
	else if (currentKey == "Height")
	{
		tidalEvents[eventIndex].heightM = value.toFloat();
	}
}

void AdmiraltyApiClient::endArray() {
	DEBUGV("end array. ");
}

void AdmiraltyApiClient::endObject() {
	DEBUGV("end object. ");

	// Set the epoch time and validity of the current tidal event
	// If we're in here then it's safe to assume we have a valid tidal event
	tidalEvents[eventIndex].epochTime = makeTime(tidalEvents[eventIndex].tm);
	tidalEvents[eventIndex].isValid = true;

	if (eventIndex < (MAX_COUNT_TIDAL_EVENTS - 1))
	{
		eventIndex++;
	}
}

void AdmiraltyApiClient::endDocument() {
	DEBUGV("end document. ");
	numberEvents = eventIndex;
}

void AdmiraltyApiClient::startArray() {
	DEBUGV("start array. ");
}

void AdmiraltyApiClient::startObject() {
	DEBUGV("start object. ");
}

/**********************
 TidalEvent
**********************/

// Number of hours and minutes between this event and the time parameter.
tmElements_t TidalEvent::TimeFrom(time_t time)
{
	tmElements_t result = tmElements_t();

	time_t diff = max(epochTime, time) - min(epochTime, time);

	// Take out the hours first
	result.Hour = numberOfHours(diff);

	// #define hoursToTime_t   ((H)) ( (H) * SECS_PER_HOUR)  
	// macro in TimeLib.h not working, needs changed to
	// #define hoursToTime_t(H) ( (H) * SECS_PER_HOUR)  
	// but I don't want to break other people's code...
	time_t hoursAsTimeT = result.Hour * SECS_PER_HOUR;

	result.Minute = numberOfMinutes((diff - hoursAsTimeT));

	return result;
}
