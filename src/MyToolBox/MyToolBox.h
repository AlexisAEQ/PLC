#ifndef MyToolBox_h
#define MyToolBox_h

#include <Arduino.h>
#include <IPAddress.h>

#define TOOLBOX_VERSION "ToolBox version 2.1"

#define DELAY 3000

#define MAX_ERROR_MESSAGE_SIZE 40

#define PRESS_ANY_KEY "Press any key"

typedef union{
    unsigned int unsignedIntPoint;
    byte binary[2];
} binaryUnsignedInt;

typedef union {
   float floatingPoint;
   byte binary[4];
 } binaryFloat;

 class MyToolBox {

 public:	
	void begin();
#ifndef REALTIME	
	static void pressAnyKey();
	
	static void emptySerialIn();
		
	static void clearScreen();
	
	static bool getTrueOrFalseResponse();
	
	static void readString(String *myString);
	
	static bool getMacAddress(byte *tab);
	static bool getIpAddress(IPAddress *ipAddress);
	static void printIPAddress(const char *name, IPAddress ip, bool ret = false);
	
	static int16_t convUINT_TO_INT(uint16_t val);
	
	static byte asciiToChar(byte asciiVar);
#endif

	static void carriageReturnPrint(char nb);		
	static float readAnalogValue();	
	static long readDigitalValue();
	static char readChar();
	
	bool addItem(const char *item, char *msg, unsigned int len);
	bool addItem(const char *itemName, int value, char *msg, unsigned int len);
	bool addItem(const char *itemName, float value, char *msg, unsigned int len);
	
	static void unsignedIntToBytes(unsigned int intValue, byte *bytes);
	static void floatToBytes(float floatValue, byte *bytes);
	static void bytesToHex(byte *data, unsigned char nbBytes, char *hex);  // convert bytes to hex (hex must be suffisient
	static void stringToURL(String source, String *dest);
#ifndef REALTIME	
	static bool yes_or_no();
#endif
	static unsigned int lengthOfcharArray(const char *);
	static bool checkSizeInsideEEprom(char *ptr, uint8_t maxSize);
		
	void setTimeStampDelta(uint32_t time);
	void printTimeStamp(char *);
	void printTimeStamp(const __FlashStringHelper *text);
	void printTimeStamp(long value);
	
	static unsigned int CRC16(unsigned int crc, unsigned char *buf, int len);
	
 private:
	static bool splitFields(byte *tab, unsigned char nbFields);
	
    Print* printer;
	uint32_t timeStampOrig;
	
	void printST(const char *);
};
#endif