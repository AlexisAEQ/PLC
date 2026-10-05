#include <Arduino.h>
#include "MyToolBox/MyToolBox.h"
// #include <EEPROMAnything.h>
	
	void MyToolBox::begin(){
	//	printer = &print; //operate on the address of print	  	
	} // begin
	
	void MyToolBox::stringToURL(String source, String *dest){
		for (unsigned int i = 0; i < source.length(); i++){
			unsigned char car = source.charAt(i);
			switch (car){ // ISO 8859-1
				case ' ': dest->concat("%20"); break; // space
				case 43 : dest->concat("%2b"); break; // +
				case 47 : dest->concat("%2f"); break; // /
				case 58 : dest->concat("%3a"); break; // :
				case 59 : dest->concat("%3b"); break; // ;
				case 61:  dest->concat("%3d"); break; // =
				case 224: dest->concat("%e0"); break; // à
				case 226: dest->concat("%e2"); break; // â
				case 232: dest->concat("%e8"); break; // è
				case 233: dest->concat("%e9"); break; // é
				case 234: dest->concat("%ea"); break; // ê
				case 235: dest->concat("%eb"); break; // ë
				case 238: dest->concat("%ee"); break; // î
				case 244: dest->concat("%f4"); break; // ô
				case 246: dest->concat("&f6"); break;  // ö
				case 249: dest->concat("%f9"); break; // ù
				case 251: dest->concat("%fb"); break; // û
				case 252: dest->concat("&fc"); break;  // ü
				
				default:  dest->concat(source.charAt(i));
			}			
		}	
	} // stringTohtml
	
#ifndef REALTIME	
	void MyToolBox::pressAnyKey(){
		carriageReturnPrint(2);
		Serial.println(F("Press any key to continue"));
		
		emptySerialIn();
		
		while (!Serial.available()) {  // Wait here until input buffer has a character
			;
		}  
	}
	
	void MyToolBox::emptySerialIn(){
		while (Serial.available() > 0) {
			Serial.read();
		}     
		Serial.flush();
	}
	
	void MyToolBox::carriageReturnPrint(char nb){
		for (char i = 0; i < nb; i++)
			Serial.println("");
	}
	
	void MyToolBox::clearScreen(){
		carriageReturnPrint(100);
	}

	bool MyToolBox::yes_or_no(){
		char inByte = 0; 
		emptySerialIn();
  
		while (inByte != 'y' && inByte != 'Y' && inByte != 'n' && inByte != 'N' && inByte != 'o' && inByte != 'O'){
			if (Serial.available() > 0) {
				inByte = Serial.read();
			}
		}
		     
		if (inByte == 'y' || inByte == 'Y' || inByte == 'O' || inByte == 'o'){
			return true;
		}
	
		return false;  
	} // boolean yes_or_no()
	
	bool MyToolBox::getTrueOrFalseResponse(){
		char inByte = 0; 
		emptySerialIn();
  
		while (inByte != 'V' && inByte != 'v' && inByte != 'F' && inByte != 'f' && inByte != 'T' && inByte != 't' && inByte != 'O' && inByte != 'o' && inByte != 'N'){
			if (Serial.available() > 0) {
				inByte = Serial.read();
			}
		} 
     
		if (inByte == 'T' || inByte == 't' || inByte == 'V' || inByte == 'v' || inByte == 'O' || inByte == 'o'){
			return true;
		}
	
		return false;
	} // getTrueOrFalseResponse

		
	float MyToolBox::readAnalogValue(){
		emptySerialIn();
     	Serial.setTimeout(10000); 
		while (Serial.available() == 0) ;  // Wait here until input buffer has a character
		
    	return Serial.parseFloat();        // new command in 1.0 forward 
	}
	
	long MyToolBox::readDigitalValue(){
		emptySerialIn();
     
		while (Serial.available() == 0){;}   // Wait here until input buffer has a character
 
	return Serial.parseInt();        // new command in 1.0 forward 
	}
	
	char MyToolBox::readChar(){
		emptySerialIn();
     
		while (Serial.available() == 0){;}   // Wait here until input buffer has a character
 
	return Serial.read();		
	} // readChar
		
	bool MyToolBox::addItem(const char *item, char *msg, unsigned int len){
		if ((int)((int)len - (int)lengthOfcharArray(msg)) > (int)lengthOfcharArray(item) + 10){
			strcat(msg, item);
			strcat(msg, "\n");
			return false;
		} 
			return true;		
	}
	
	bool MyToolBox::addItem(const char *itemName, int value, char *msg, unsigned int len){		
		if ((int)((int)len - (int)lengthOfcharArray(msg)) > (int)lengthOfcharArray(itemName) + 15){
			strcat(msg, itemName);
			strcat(msg, ": ");
			sprintf(msg + lengthOfcharArray(msg), "%d", value);
			strcat(msg, "\n");
			return false;
		} 
			return true;		
	}
	
	bool MyToolBox::addItem(const char *itemName, float value, char *msg, unsigned int len){
		if (((int)(int)len - (int)lengthOfcharArray(msg)) > (int)lengthOfcharArray(itemName) + 20){
			strcat(msg, itemName);
			strcat(msg, ": ");
			dtostrf(value, 4, 2, msg + lengthOfcharArray(msg));
			strcat(msg, "\n");
			return false;
		} 
		return true;
	}
#endif
	
	unsigned int MyToolBox::lengthOfcharArray(const char *buffer){
		int i=0;
		while(buffer[i]){
			i++;
		}
		return i;
	 }
	 
#ifndef REALTIME	
	void MyToolBox::setTimeStampDelta(uint32_t time){
		#ifndef SILENT
			timeStampOrig = time;
		#endif
	}
	
	void MyToolBox::printTimeStamp(char *text){
		#ifndef SILENT
			printST(text);
		#endif
		;
	}	
	
	void MyToolBox::printTimeStamp(const __FlashStringHelper *ifsh){
#ifndef SILENT
			Serial.print(millis() - timeStampOrig);
			Serial.print(" ");
			Serial.println(ifsh);
#endif
	}
	
	void MyToolBox::printTimeStamp(long value){
		#ifndef SILENT
			char buff[20];		
			printST(itoa(value, buff, 10));
		#endif
	}
	
	void MyToolBox::printST(const char *text){	
		#ifndef SILENT
			Serial.print(millis() - timeStampOrig);
			Serial.print(" ");
			Serial.println(text);
		#endif	
	}
	
	byte MyToolBox::asciiToChar(byte asciiVar){
		if (asciiVar <= '9'){
				  return asciiVar - '0';
		} else {
				  return asciiVar - 'A' + 10;
		}
	} // asciiToChar
	
	bool MyToolBox::getMacAddress(byte *tab){
		 return splitFields((byte *)tab, 6);
	}
	
	bool MyToolBox::getIpAddress(IPAddress *ipAddress){
		byte tab[4];
		bool err = splitFields((byte *)tab, 4);
		if (err){
			*ipAddress = IPAddress(tab);
		}	
		return err; 
	} // getIpAddress
	
	int16_t MyToolBox::convUINT_TO_INT(uint16_t u){
		if (u <= INT16_MAX){ 		
    		 return (int)u;
  		} else {
    		return  -(UINT16_MAX - u) -1;
  		}
	}

	bool MyToolBox::splitFields(byte *tab, unsigned char nbFields){
		char separator;
		if (nbFields == 6){
			separator = ':';
			Serial.println(F("Enter the adress with the following format: 5E:FF:56:A2:AF:15"));
		} else {
			separator = '.';
			Serial.println(F("Enter the adress with the following format: 192.168.001.000"));
		}
				
		String myString;
		readString(&myString);
		if (((nbFields == 6) && (myString.length() != 17)) || ((nbFields == 4) && (myString.length() != 15))){
			Serial.println("Wrong string size !");
			return false;
		}
		
		for (unsigned char i = 0; i < nbFields; i++){
			String partString;
			char index = myString.indexOf(separator);
			if (index){
				partString = myString.substring(0, index);
			} else {
				partString = myString;
			}
					  
			// convert ASCII to hex
			if (nbFields == 6){
				tab[i] = asciiToChar(partString.charAt(0)) * 16 +  asciiToChar(partString.charAt(1));
			} else {
				 tab[i] = (partString.charAt(0) - '0') * 100 +  (partString.charAt(1) - '0') * 10 + partString.charAt(2)- '0';
			}
			 
			 myString.remove(0, index +1);
         } // for
			
		return true;
	} // splitFields
	
	void MyToolBox::readString(String *theString){
		theString->reserve(200);
		MyToolBox::emptySerialIn();
		while(1){
			if (Serial.available()){
				*theString = Serial.readStringUntil('\n');
				theString->trim();
				//Serial.print("Receive: "); Serial.println(*theString);
				
				 if (theString->indexOf('\r') != -1){	
					theString->remove(theString->indexOf('\r'));
				 }
				return;
			}
		} // while
	} // readString
	
	void  MyToolBox::unsignedIntToBytes(unsigned int intValue, byte *bytes){   
	   binaryUnsignedInt myValue;
	   myValue.unsignedIntPoint = intValue;
	   
	   // warning, revers order !!!!
		 bytes[1] = myValue.binary[0];
		 bytes[0] = myValue.binary[1];  
	} // unsignedIntToBytes

	
	void MyToolBox::floatToBytes(float floatValue, byte *bytes){
		 binaryFloat myValue;
		 myValue.floatingPoint = floatValue;  
		 for (unsigned char i = 0; i < 4; i++){
		   bytes[i] = myValue.binary[i];
		 }
	} // floatToBytes
	
	void MyToolBox::bytesToHex(byte *data, unsigned char nbBytes, char *hex){
		//char[] c = new char[nbBytes * 2]; 
		byte b; 
		int x = 0;
		for (int y = 0; y < nbBytes; ++y, ++x){ 
			b = ((byte)(data[y] >> 4)); 
//			hex[x] = (char)(b > 9 ? b + 0×37 : b + 0×30);
			if ((char)b > 9){
				hex[x] = b + 0x37;
			} else {
				hex[x] = b + 0x30;
			}
			b = ((byte)(data[y] & 0xF)); 
			x++;
			//hex[++x] = (char)(b > 9 ? b + 0×37 : b + 0×30); 
			if ((char)b > 9){
				hex[x] = b + 0x37;
			} else {
				hex[x] = b + 0x30;
			}		
		} // for 
		hex[x] = 0;
	}	// bytesToHex
	
	unsigned int MyToolBox::CRC16(unsigned int crc, unsigned char *buf, int len){
		for (int pos = 0; pos < len; pos++){
			crc ^= (unsigned int)buf[pos];    // XOR byte into least sig. byte of crc

			for (int i = 8; i != 0; i--) {    // Loop over each bit
				if ((crc & 0x0001) != 0) {      // If the LSB is set
					crc >>= 1;                    // Shift right and XOR 0xA001
					crc ^= 0xA001;
				}
				else                            // Else LSB is not set
					crc >>= 1;                    // Just shift right
			}
		}

	return crc;
}

bool MyToolBox::checkSizeInsideEEprom(char *ptr, uint8_t maxSize){
    // strlen ne fonctionne pas a l'intérieur de la flash
    for (uint8_t i = 0; i < maxSize; i++){
        if (ptr[i] == 0){
            return true;
        }
    }
    return false;
}

void MyToolBox::printIPAddress(const char *name, IPAddress ip, bool ret){
	Serial.printf("%s: %d.%d.%d.%d", name, ip[0], ip[1], ip[2], ip[3]);
	if (ret){
		Serial.println();
	}
}
			
#endif