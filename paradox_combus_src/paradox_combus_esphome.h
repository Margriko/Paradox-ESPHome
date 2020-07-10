#include "esphome.h"

#define CLK 5 // Keybus Yellow
#define DTA 4 // Keybus Green

String BusMessage = "";
unsigned long LastClkSignal = 0;
bool ClkPinTriggered = 0;
bool combusConnectionStatus = false;

void ICACHE_RAM_ATTR interuptClockFalling()
{

  //## Set last Clock time
  LastClkSignal = micros();
  //Set pin triggered
  ClkPinTriggered = true;
  //Trigger data read timer
  timer1_write(750); // 750 / 5 ticks per us from TIM_DIV16 == 150microseconds interval   (ithink)
}

void ICACHE_RAM_ATTR readDataPin()
{

  //If not triggered, just return  .. this should never happen, but who knows
  if (ClkPinTriggered == false)
  {
    ESP_LOGD("custom", "never");
    return;
  }

  //Serial.println("Reading Data Pin");

  /*
  * Code need to be updated to ignore the response from the keypad (Rising Edge Comms).
  * 
  * Panel to other is on Clock Falling edge, Reply is after keeping DATA low (it seems) and then reply on Rising edge
  */
  //delayMicroseconds(150);
  //Just add small delay to make sure DATA is already set, each clock is 500 ~microseconds, Seem to have about 50ms delay before Data goes high when keypad responce and creating garbage .

  //Just wait 150ms since last clk signal to stabilise .. should not happen as we triggered via timer
  while (micros() - LastClkSignal < 150)
  {
  }

  //Append pin state to bus message .. probably should make it binary, but we have enough memory to make debugging simpler
  if (!digitalRead(DTA))
    BusMessage += "1";
  else
    BusMessage += "0";

  //Set pin to not triggered
  ClkPinTriggered = false;

  if (BusMessage.length() > 200)
  {
    //Serial.println("String to long");
    //Serial.println((String) BusMessage);
    BusMessage = "";
    //    printSerialHex(BusMessage);
    return; // Do not overflow the arduino's little ram
  }
}

void disconnectCombus() {
  timer1_disable();
  timer1_detachInterrupt();
  detachInterrupt(CLK);
  combusConnectionStatus = false;
}

void connectCombus() {
  pinMode(CLK, INPUT);
  pinMode(DTA, INPUT);
  //## Interupt pin
  attachInterrupt(CLK, &interuptClockFalling, FALLING);
  //##Interupt timer 
  timer1_attachInterrupt(&readDataPin); // Add ISR Function
  timer1_enable(TIM_DIV16,TIM_EDGE,TIM_SINGLE);
  combusConnectionStatus = true;    ESP_LOGD("Status", "Init successful");

}

bool getCombusConnectionStatus() {
  return combusConnectionStatus;
}

class ParadoxCombusEsphome : public Component {
 public:
  std::function<void (uint8_t, bool)> zoneStatusChangeCallback;
  std::function<void (std::string)> alarmStatusChangeCallback;

  const std::string STATUS_UNAVAILABLE = "unavailable";
  const std::string STATUS_ARM = "armed_away";
  const std::string STATUS_SLEEP = "armed_night";
  const std::string STATUS_STAY = "armed_home";
  const std::string STATUS_OFF = "disarmed";

  /**
   * Zone status changed.
   * 
   * @param callback      callback to notify
   * @param zone          zone number [0..MAX]
   * @param isOpen        true if zone open (disturbed), false if zone closed
   */
  void onZoneStatusChange(std::function<void (uint8_t zone, bool isOpen)> callback) { zoneStatusChangeCallback = callback; }

  /**
   * Alarm status changed.
   * 
   * @param callback      callback to notify
   * @param statusCode    status code
   */
  void onAlarmStatusChange(std::function<void (std::string status)> callback) { alarmStatusChangeCallback = callback; }

  void setup() override {
    connectCombus();
  }

  void loop() override {
    if (!getCombusConnectionStatus()) {
      alarmStatusChangeCallback(STATUS_UNAVAILABLE);

      for (int i = 0; i < 32; i++) {
        zoneStatusChangeCallback(i + 1, false);
      }
      return;
    }

    // ## Check if there anything new on the Bus
    if (!checkClockIdle() or BusMessage.length() < 2)
    {
      return;
    }

    // ## Save message and clear buffer
    String Message = BusMessage;
    // ## Clear old message
    BusMessage = "";

    decodeMessage(Message);
  }

  /*******************************************************************************************************/
  /****************************** Process Zone Status connect *****************************************/
  /**
   * All ok
   * 11010000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 01100011
   * Zone 1
   * 11010000 00000000 01000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 10111001   
   * Zone 1 and 2 and 3
   * 11010000 00000000 01010100 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 1010 0001
   */
  void processZoneStatus(String &msg)
  {
    //Zone 1 = bit 17
    //Zone 2 = bit 19
    //Zone 3 = bit 21

    // Serial.println("ProcessingZones");

    for (int i = 0; i < 32; i++)
    {
      bool open = msg[17 + (i * 2)] == '1';
      zoneStatusChangeCallback(i + 1, open);
    }
  }

  /****************************** Process Alarm (D1) Status connect *****************************************/
  /*
  * Might be first 5 bytes are Partition1 and 2nd 5 bytes is partion 2??
  * Alarm Not set: 11010001 00000000 00000000 00010001 00000000 00000000 00000000 01000100 00000000 00000000 00000001 01001111
  * Alarm Set:     11010001 00000000 01000000 00010001 00000000 00000000 00000000 00000100 00000000 00000000 00000001 01110101  
  * Alarm STay     11010001 00000000 00000000 00010001 00000000 00000000 00000100 00000100 00000000 00000000 00000001 10110000
  * Alarm Sleep    11010001 00000000 00000100 00010001 00000000 00000000 00000000 00000100 00000000 00000000 00000001 00001101
  * 
  * 
  */

  void processAlarmStatus(String &msg)
  {
    //If set , then get status
    if (msg[((8 * 7) + 1)] == '0')
    {
      //Sleep
      if (msg[((8 * 2) + 5)] == '1')
      {
        //AlarmStatus.status = 20;
        alarmStatusChangeCallback(STATUS_STAY);
      }
      //Stay
      if (msg[((8 * 6) + 5)] == '1')
      {
        //AlarmStatus.status = 30;
        alarmStatusChangeCallback(STATUS_SLEEP);
      }
      //Full Arm
      if (msg[((8 * 2) + 1)] == '1')
      {
        //Exit Delay
        if (msg[((8 * 2) + 0)] == '1')
        {
          //AlarmStatus.status = 40;
          alarmStatusChangeCallback("exit");
        }
        //Full Alarm
        else if (msg[((8 * 2) + 0)] == '0')
        {
          //AlarmStatus.status = 49;
          alarmStatusChangeCallback("fullalarm");
        }
        else
        {
          //AlarmStatus.status = 45;
          alarmStatusChangeCallback(STATUS_ARM);
        }
      }
    }
    else
    {
      //Not Set
      //AlarmStatus.status = 10;
      alarmStatusChangeCallback(STATUS_OFF);
    }
    // alarmStatusChangeCallback(STATUS_OFF);
  }
  /*********************************************************************************************************/

  /****************************** decodeMessage ****************************************
   *  
   * Check Command type and call approriate function 
   * 
   */
  void decodeMessage(String &msg)
  {

    int cmd = GetIntFromString(msg.substring(0, 8));

    //Some commands have trailing "00 00 00 00" so remove
    if (cmd == 0xD0 || cmd == 0xD1)
    {
      //Strip last 00's
      msg = msg.substring(0, msg.length() - (4 * 8) - 1);

      if (!check_crc(msg))
      {
        // Serial.println("CRC Faied:");
        // printSerial(msg, HEX);
        return;
      }
    }
    else
    {

      if (!check_crc(msg))
      {
        // Serial.println("CRC Faied:");
        // printSerial(msg, HEX);
        return;
      }
    }

    switch (cmd)
    {
    case 0xd0: //Zone Status Message

      processZoneStatus(msg);

      break;
    case 0xD1: //Seems like Alarm status
      processAlarmStatus(msg);

      break;
    case 0xD2: //Action Message;

      break;

    case 0x20: //

      break;
    case 0xE0: // Status

      break;
    default:
      //Do Nothing
      break;
      ;
    }
    //Serial.print("Cmd=");
    //Serial.println(cmd,HEX);
  }

  /**
   * CRC8
   * 
   * Do Maxim crc8 calc
   */
  uint8_t crc8(uint8_t *addr, uint8_t len)
  {
    uint8_t crc = 0;

    for (uint8_t i = 0; i < len; i++)
    {
      uint8_t inbyte = addr[i];
      for (uint8_t j = 0; j < 8; j++)
      {
        uint8_t mix = (crc ^ inbyte) & 0x01;
        crc >>= 1;
        if (mix)
          crc ^= 0x8C;

        inbyte >>= 1;
      }
    }
    return crc;
  }

  /**
   * Check CRC
   * Check if CRC is valid
   * 
   */
  uint8_t check_crc(String &st)
  {

    // printSerial(st);
    //

    String val = "";
    int Bytes = (st.length()) / 8;
    uint8_t calcCRCByte;

    //Serial.print("Bytes :");Serial.println(Bytes);
    //printSerialHex(st);

    //Make byte array
    uint8_t *BinnaryStr = strToBinArray(st);

    uint8_t CRC = BinnaryStr[Bytes - 1];

    calcCRCByte = crc8(BinnaryStr, (int)Bytes - 1);

    //Serial.print("Crc :");Serial.print((int)CRC,HEX);

    //Serial.print("CrcCalc :");Serial.print((int)calcCRCByte,HEX);
    //Serial.println("");

    return calcCRCByte == CRC;
  }

  /*
  *   Check if clock idle, means end of message 
  *   Each messasge is split by  10 Millisecond (10 000 microsecond) delay, 
  *   So assume message been send if 4 clock signals (500us x 4  x 2 = 4000) is low
  *   
  */
  bool checkClockIdle()
  {

    unsigned long currentMicros = micros();
    //time diff in
    long idletime = (currentMicros - LastClkSignal);

    if (idletime > 8000)
    {

      //Serial.println("Idle:"+(String)idletime +":"+currentMicros+":"+ LastClkSignal);
      return true;
    }
    else
    {

      return false;
    }
  }

  /**
   * Get int from String
   * 
   * Convert the Binary 10001000 to a int
   * 
   */
  unsigned int GetIntFromString(String str)
  {
    int r = 0;
    // Serial.print("Received:");  Serial.println(str);
    int length = str.length();

    for (int j = 0; j < length; j++)
    {
      if (str[length - j - 1] == '1')
      {
        r |= 1 << j;
      }
    }

    return r;
  }

  /**
   * Convert str to binary array
   * 
   */
  uint8_t *strToBinArray(String &st)
  {
    int Bytes = (st.length()) / 8;
    uint8_t Data[Bytes];

    String val = "";

    for (int i = 0; i < Bytes; i++)
    {
      //String kk = "012345670123456701234567";
      val = st.substring((i * 8), ((i * 8)) + 8);

      Data[i] = GetIntFromString(val);
    }

    return Data;
  }

};