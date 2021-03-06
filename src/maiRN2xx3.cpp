/*
 * A library for controlling a Microchip rn2xx3 LoRa radio.
 *
 * @Author JP Meijers
 * @Author Nicolas Schteinschraber
 * @Date 18/12/2015
 *
 */

#include "Arduino.h"
#include "maiRN2xx3.h"

extern "C" {
#include <string.h>
#include <stdlib.h>
}

/*
  @param serial Needs to be an already opened Stream ({Software/Hardware}Serial) to write to and read from.
*/
maiRN2xx3::maiRN2xx3(Stream& serial):
_serial(serial)
{
  _serial.setTimeout(2000);
}

//TODO: change to a boolean
void maiRN2xx3::autobaud()
{
  String response = "";

  // Try a maximum of 10 times with a 1 second delay
  for (uint8_t i=0; i<10 && response==""; i++)
  {
    delay(1000);
    _serial.write((byte)0x00);
    _serial.write(0x55);
    _serial.println();
    // we could use sendRawCommand(F("sys get ver")); here
    _serial.println("sys get ver");
    response = _serial.readStringUntil('\n');
  }
}


String maiRN2xx3::sysver()
{
  String ver = sendRawCommand(F("sys get ver"));
  ver.trim();
  return ver;
}

maiRN2xx3_t maiRN2xx3::configureModuleType()
{
  String version = sysver();
  String model = version.substring(2,6);
  switch (model.toInt()) {
    case 2903:
      _moduleType = RN2903;
      break;
    case 2483:
      _moduleType = RN2483;
      break;
    default:
      _moduleType = RN_NA;
      break;
  }
  return _moduleType;
}

String maiRN2xx3::hweui()
{
  return (sendRawCommand(F("sys get hweui")));
}

String maiRN2xx3::appeui()
{
  return ( sendRawCommand(F("mac get appeui") ));
}

String maiRN2xx3::appkey()
{
  // We can't read back from module, we send the one
  // we have memorized if it has been set
  return _appskey;
}

String maiRN2xx3::deveui()
{
  return (sendRawCommand(F("mac get deveui")));
}

bool maiRN2xx3::init()
{
  if(_appskey=="0") //appskey variable is set by both OTAA and ABP
  {
    return false;
  }
  else
  {
    return initOTAA(_appeui, _appskey);
  }
}


bool maiRN2xx3::initOTAA(String AppEUI, String AppKey, String DevEUI)
{
  _otaa = true;
  _nwkskey = "0";
  String receivedData;
  _rn2483Response = "";

  //clear serial buffer
  while(_serial.available())
    _serial.read();

  // detect which model radio we are using
  configureModuleType();

  // reset the module - this will clear all keys set previously
  switch (_moduleType)
  {
    case RN2903:
      sendRawCommand(F("mac reset"));
      break;
    case RN2483:
      sendRawCommand(F("mac reset 868"));
      break;
    default:
      // we shouldn't go forward with the init
      return false;
  }

  // If the Device EUI was given as a parameter, use it
  // otherwise use the Hardware EUI.
  if (DevEUI.length() == 16)
  {
    _deveui = DevEUI;
  }
  else
  {
    String addr = sendRawCommand(F("sys get hweui"));
    if( addr.length() == 16 )
    {
      _deveui = addr;
    }
    // else fall back to the hard coded value in the header file
  }

  sendRawCommand("mac set deveui "+_deveui);

  // A valid length App EUI was given. Use it.
  if ( AppEUI.length() == 16 )
  {
      _appeui = AppEUI;
      sendRawCommand("mac set appeui "+_appeui);
  }

  // A valid length App Key was give. Use it.
  if ( AppKey.length() == 32 )
  {
    _appskey = AppKey; //reuse the same variable as for ABP
    sendRawCommand("mac set appkey "+_appskey);
  }

  if (_moduleType == RN2903)
  {
    sendRawCommand(F("mac set pwridx 5"));
  }
  else
  {
    sendRawCommand(F("mac set pwridx 1"));
  }

  sendRawCommand(F("mac set dr 0"));

  // TTN does not yet support Adaptive Data Rate.
  // Using it is also only necessary in limited situations.
  // Therefore disable it by default.
  sendRawCommand(F("mac set adr on"));

  // Switch off automatic replies, because this library can not
  // handle more than one mac_rx per tx. See RN2483 datasheet,
  // 2.4.8.14, page 27 and the scenario on page 19.
  sendRawCommand(F("mac set ar off"));

  // Semtech and TTN both use a non default RX2 window freq and SF.
  // Maybe we should not specify this for other networks.
  // if (_moduleType == RN2483)
  // {
  //   sendRawCommand(F("mac set rx2 3 869525000"));
  // }
  // Disabled for now because an OTAA join seems to work fine without.

  _serial.setTimeout(30000);
  sendRawCommand(F("mac save"));

  bool joined = false;

  // Only try twice to join, then return and let the user handle it.
  for(int i=0; i<2 && !joined; i++)
  {
    _rn2483Response += sendRawCommand(F("mac join otaa"));
    // Parse 2nd response
    _serial.setTimeout(10000);
    receivedData = _serial.readStringUntil('\n');
    _rn2483Response += "->" + receivedData + ";";

    if(receivedData.startsWith("accepted"))
    {
      joined=true;
      delay(10000);
    }
    else
    {
      delay(10000);
    }
  }
  _serial.setTimeout(2000);
  //_rn2483Response += "->" + receivedData;
  return joined;
}

bool maiRN2xx3::joinOTAA()
{
  String receivedData;
  _rn2483Response = "";
  bool joined = false;
  // Only try twice to join, then return and let the user handle it.
  for(int i=0; i<2 && !joined; i++)
  {
    _rn2483Response += sendRawCommand(F("mac join otaa"));
    // Parse 2nd response
    _serial.setTimeout(10000);
    receivedData = _serial.readStringUntil('\n');
    _rn2483Response += "->" + receivedData + ";";

    if(receivedData.startsWith("accepted"))
    {
      joined=true;
      delay(1000);
    }
    else
    {
      delay(1000);
    }
  }
  _serial.setTimeout(2000);
  //_rn2483Response += "->" + receivedData;
  return joined;
}

TX_RETURN_TYPE maiRN2xx3::tx(String data, bool shouldEncode)
{
  return txUncnf(data, shouldEncode); //we are unsure which mode we're in. Better not to wait for acks.
}

TX_RETURN_TYPE maiRN2xx3::txBytes(const byte* data, uint8_t size)
{
  char msgBuffer[size*2 + 1];

  char buffer[3];
  for (unsigned i=0; i<size; i++)
  {
    sprintf(buffer, "%02X", data[i]);
    memcpy(&msgBuffer[i*2], &buffer, sizeof(buffer));
  }
  String dataToTx(msgBuffer);
  return txCommand("mac tx uncnf 1 ", dataToTx, false);
}

TX_RETURN_TYPE maiRN2xx3::txCnf(String data, bool shouldEncode)
{
  return txCommand("mac tx cnf 1 ", data, shouldEncode);
}

TX_RETURN_TYPE maiRN2xx3::txUncnf(String data, bool shouldEncode)
{
  return txCommand("mac tx uncnf 1 ", data, shouldEncode);
}

TX_RETURN_TYPE maiRN2xx3::txCommand(String command, String data, bool shouldEncode)
{
  uint8_t busy_count = 0;
  uint8_t retry_count = 0;

  //clear serial buffer
  while(_serial.available())
    _serial.read();

  _serial.print(command);
  if(shouldEncode)
  {
    sendEncoded(data);
  }
  else
  {
    _serial.print(data);
  }
  _serial.println();

  String receivedData = _serial.readStringUntil('\n');
  //TODO: Debug print on receivedData
  _rn2483Response = receivedData;

  if(receivedData.startsWith("ok"))
  {
    _serial.setTimeout(30000);
    receivedData = _serial.readStringUntil('\n');
    _serial.setTimeout(2000);
    _rn2483Response += "->" + receivedData;

    //TODO: Debug print on receivedData
    if(receivedData.startsWith("mac_tx_ok"))
    {
      //SUCCESS!!
      return TX_SUCCESS;
    }
    else if(receivedData.startsWith("mac_rx"))
    {
      //example: mac_rx 1 54657374696E6720313233
      _rxMessenge = receivedData.substring(receivedData.indexOf(' ', 7)+1);
      return TX_WITH_RX;
    }
    else if(receivedData.startsWith("mac_err"))
    {
      return TX_MAC_ERR;
    }
    else if(receivedData.startsWith("invalid_data_len"))
    {
      //this should never happen if the prototype worked
      return TX_FAIL;
    }
    else if(receivedData.startsWith("radio_tx_ok"))
    {
      //SUCCESS!!
      return TX_SUCCESS;
    }
    else if(receivedData.startsWith("radio_err"))
    {
      //This should never happen. If it does, something major is wrong.
      return TX_FAIL;
    }
    else
    {
      //unknown response
      return TX_FAIL;
    }
  }
  else if(receivedData.startsWith("invalid_param"))
  {
    //should not happen if we typed the commands correctly
    return TX_FAIL;
  }
  else if(receivedData.startsWith("not_joined"))
  {
    return TX_NOT_JOINED;
  }
  else if(receivedData.startsWith("no_free_ch"))
  {
    //retry
    return TX_FAIL;
  }
  else if(receivedData.startsWith("silent"))
  {
    return TX_FAIL;
  }
  else if(receivedData.startsWith("frame_counter_err_rejoin_needed"))
  {
    return TX_FAIL;
  }
  else if(receivedData.startsWith("busy"))
  {
    return TX_FAIL;
  }
  else if(receivedData.startsWith("mac_paused"))
  {
    return TX_FAIL;
  }
 else if(receivedData.startsWith("invalid_data_len"))
  {
    //should not happen if the prototype worked
    return TX_FAIL;
  }
  else
  {
    //unknown response after mac tx command
    return TX_FAIL;
  }
}

void maiRN2xx3::sendEncoded(String input)
{
  char working;
  char buffer[3];
  for (unsigned i=0; i<input.length(); i++)
  {
    working = input.charAt(i);
    sprintf(buffer, "%02x", int(working));
    _serial.print(buffer);
  }
}

String maiRN2xx3::base16encode(String input)
{
  char charsOut[input.length()*2+1];
  char charsIn[input.length()+1];
  input.trim();
  input.toCharArray(charsIn, input.length()+1);

  unsigned i = 0;
  for(i = 0; i<input.length()+1; i++)
  {
    if(charsIn[i] == '\0') break;

    int value = int(charsIn[i]);

    char buffer[3];
    sprintf(buffer, "%02x", value);
    charsOut[2*i] = buffer[0];
    charsOut[2*i+1] = buffer[1];
  }
  charsOut[2*i] = '\0';
  String toReturn = String(charsOut);
  return toReturn;
}

String maiRN2xx3::getRx() {
  return _rxMessenge;
}

String maiRN2xx3::getRN2483Response() {
  return _rn2483Response;
}

int maiRN2xx3::getSNR()
{
  String snr = sendRawCommand(F("radio get snr"));
  snr.trim();
  return snr.toInt();
}

String maiRN2xx3::base16decode(String input)
{
  char charsIn[input.length()+1];
  char charsOut[input.length()/2+1];
  input.trim();
  input.toCharArray(charsIn, input.length()+1);

  unsigned i = 0;
  for(i = 0; i<input.length()/2+1; i++)
  {
    if(charsIn[i*2] == '\0') break;
    if(charsIn[i*2+1] == '\0') break;

    char toDo[2];
    toDo[0] = charsIn[i*2];
    toDo[1] = charsIn[i*2+1];
    int out = strtoul(toDo, 0, 16);

    if(out<128)
    {
      charsOut[i] = char(out);
    }
  }
  charsOut[i] = '\0';
  return charsOut;
}

void maiRN2xx3::setDR(int dr)
{
  if(dr>=0 && dr<=5)
  {
    delay(100);
    while(_serial.available())
      _serial.read();
    _serial.print("mac set dr ");
    _serial.println(dr);
    _serial.readStringUntil('\n');
  }
}

void maiRN2xx3::sleep(long msec)
{
  _serial.print("sys sleep ");
  _serial.println(msec);
}


String maiRN2xx3::sendRawCommand(String command)
{
  delay(100);
  while(_serial.available())
    _serial.read();
  _serial.println(command);
  String ret = _serial.readStringUntil('\n');
  ret.trim();

  //TODO: Add debug print

  return ret;
}

maiRN2xx3_t maiRN2xx3::moduleType()
{
  return _moduleType;
}
