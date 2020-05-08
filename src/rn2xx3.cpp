/*
 * A library for controlling a Microchip rn2xx3 LoRa radio.
 *
 * @Author JP Meijers
 * @Author Nicolas Schteinschraber
 * @Date 18/12/2015
 *
 */

#include "Arduino.h"
#include "rn2xx3.h"

extern "C" {
#include <string.h>
#include <stdlib.h>
}

// private method to read stream with timeout
int rn2xx3::_timedRead(Stream *stream, unsigned long timeout ) {
    int c;
    unsigned long _startMillis = millis();
    do {
        c = stream->read();
        if(c >= 0)
            return c;
    } while(millis() - _startMillis < timeout);
    return -1;     // -1 indicates timeout
}

const char *rn2xx3::readCharStringUntil( Stream *stream, unsigned long timeout, char terminator, char *outbuf, size_t bufsz ) {
    
    if ( outbuf == NULL ) {
        return NULL;
    }
    
//    Serial.print( F("bufsz: ") );
//    Serial.println( bufsz );
    
    memset( outbuf, 0, bufsz );

    uint8_t count = 0;
    char *obptr = outbuf;

    int c = _timedRead(stream, timeout);
//    Serial.print(F("*"));
//    Serial.print( (char )c );
//    Serial.println(F("*"));
    if ( c == terminator ) {
//        Serial.println( F("found terminator!") );
    } else {
        if ( c >= 32 && c <= 127 ) {
            *obptr = c;
            obptr++;
            count++;
        }
    }
    while( c >= 0 && c != terminator ) {
        c = _timedRead(stream, timeout);
        if ( c == terminator ) {
//            Serial.println( F("found terminator!") );
        }
//        Serial.print(F("*"));
//        Serial.print( (char )c );
//        Serial.println(F("*"));
        if ( c >= 32 && c <= 127 ) {
            count++;
            *obptr = c;
            if ( count < bufsz - 1 ) {
                obptr++;
            }
        }
    } 

    return buf;
}

/*
  @param serial Needs to be an already opened Stream ({Software/Hardware}Serial) to write to and read from.
*/
rn2xx3::rn2xx3(Stream *serial): _serial(serial)
{
  _timeout = 2000;
  _serial->setTimeout(_timeout);

  memset( _devAddr, 0, sizeof( _devAddr ) );
  memset( _deveui, 0, sizeof( _deveui ) );
  memset( _appeui, 0, sizeof( _appeui ) );
  memset( _nwkskey, 0, sizeof( _nwkskey ) );
  memset( _appskey, 0, sizeof( _appskey ) );

  _rxMessage[0] = '\0';
}

//TODO: change to a boolean
bool rn2xx3::autobaud()
{
    buf[0] = '\0';

    /** Drain the serial buffer */
    while( _serial->available() ) {
        _serial->read();
    }

    // Try a maximum of 10 times with a 1 second delay
    for (uint8_t i=0; i < 10 && strlen(buf) == 0 ; i++) {
        delay(1000);
        _serial->write((byte)0x00);
        delay(20);
        _serial->write(0x55);
        _serial->println();
        // we could use sendRawCommand(F("sys get ver")); here
        _serial->println(F("sys get ver"));
        readCharStringUntil( _serial, _timeout, '\n', buf, sizeof( buf ) );
        if ( strlen( buf ) > 0 && strstr( buf, "RN2483") != NULL ) {
            Serial.print( F("***") );
            Serial.print( buf );
            Serial.println( F("***") );
            return true;
        }
    }

    return false;
}


char *rn2xx3::sysver()
{
  sendRawCommand(F("sys get ver"));
  return buf;
}

RN2xx3_t rn2xx3::configureModuleType()
{
    const char *version = sysver();
    Serial.print( F("***") );
    Serial.print( version );
    Serial.println( F("***") );
    if ( strlen( version ) < 6 ) {
        return RN_NA;
    }
    if ( strncmp( version + 2, "2903", 4 ) == 0 ) {
        Serial.println( F("Module = RN2903") );
        _moduleType = RN2903;
    } else {
        if ( strncmp( version + 2, "2483", 4 ) == 0 ) {
            Serial.println( F("Module = RN2483") );
            _moduleType = RN2483;
        } else {
            Serial.println( F("Unknown Module") );
            _moduleType = RN_NA;
        }
    }
    
    return _moduleType;
}

char *rn2xx3::hweui()
{
    sendRawCommand( F("sys get hweui") );
    return buf;
}

char *rn2xx3::appeui()
{
  return ( sendRawCommand(F("mac get appeui") ));
}

char *rn2xx3::appkey()
{
  // We can't read back from module, we send the one
  // we have memorized if it has been set
  return _appskey;
}

char *rn2xx3::deveui()
{
  return (sendRawCommand(F("mac get deveui")));
}

void rn2xx3::setdeveui( const char *deveui ) {
    if ( deveui != NULL && strlen( deveui ) == 16 ) {
        strncpy( _deveui, deveui, 16 );
    }
}    

bool rn2xx3::initOTAA( const char *AppEUI, const char *AppKey ) {
    return initOTAA( AppEUI, AppKey, NULL );
}

bool rn2xx3::initOTAA( const char *AppEUI, const char *AppKey, const char *DevEUI) {

  bool joined = false;

  _otaa = true;
  strcpy( _nwkskey, "0" );

  //clear serial buffer
  while(_serial->available())
    _serial->read();

  // detect which model radio we are using
  configureModuleType();

  // reset the module - this will clear all keys set previously
  switch (_moduleType)
  {
      case RN2903: {
      sendRawCommand(F("mac reset"));
      break;
      }
      case RN2483: {
      sendRawCommand(F("mac reset 868"));
      break;
      }
      default: {
      // we shouldn't go forward with the init
          Serial.println( F("Unknown LoRaWAN module type") );
      return false;
      }
  }

  memset( _devAddr, 0, sizeof( _devAddr ) );
  memset( _deveui, 0, sizeof( _deveui ) );
  memset( _appeui, 0, sizeof( _appeui ) );
  memset( _nwkskey, 0, sizeof( _nwkskey ) );
  memset( _appskey, 0, sizeof( _appskey ) );

  // If the Device EUI was given as a parameter, use it
  // otherwise use the Hardware EUI.
  if (DevEUI != NULL && strlen(DevEUI) == 16)
  {
#ifdef ARDUINO
      Serial.print( F("setting _deveui: ") );
      Serial.println( DevEUI );
      Serial.flush();
#endif
      strncpy( _deveui, DevEUI, 16 );
  }
  else
  {
    const char *addr = sendRawCommand(F("sys get hweui"));
    if( strlen(addr) == 16 ) {
        strncpy( _deveui, addr, 16 );
    }
    // else fall back to the hard coded value in the header file
  }

  sendRawCommand( F("mac set deveui "), _deveui );

  // A valid length App EUI was given. Use it.
  if ( AppEUI != NULL && strlen(AppEUI) == 16 )
  {
      strncpy( _appeui, AppEUI, 16 );
      sendRawCommand( F("mac set appeui "), _appeui );
  }

  // A valid length App Key was give. Use it.
  if ( AppKey != NULL && strlen(AppKey) == 32 )
  {
    strncpy( _appskey, AppKey, 32 ); //reuse the same variable as for ABP
    sendRawCommand( F("mac set appkey "), _appskey );
  }

    /** Workaround for future ABP joins */
    /** Without this, ABP joins either fail, or worse, succeed but can't TX anything */
    sendRawCommand( F("mac set devaddr 00000000") );
    sendRawCommand( F("mac set nwkskey 00000000000000000000000000000000") );
    sendRawCommand( F("mac set appskey 00000000000000000000000000000000") );
    
  if (_moduleType == RN2903)
  {
    sendRawCommand(F("mac set pwridx 5"));
  }
  else
  {
    sendRawCommand(F("mac set pwridx 1"));
  }

  /** Disable ADR for OTAA */
  sendRawCommand(F("mac set adr off"));

  // Semtech and TTN both use a non default RX2 window freq and SF.
  // Maybe we should not specify this for other networks.
  // if (_moduleType == RN2483)
  // {
  //   sendRawCommand(F("mac set rx2 3 869525000"));
  // }
  // Disabled for now because an OTAA join seems to work fine without.

  _serial->setTimeout(120000);
  sendRawCommand(F("mac save"));

  // Only try twice to join, then return and let the user handle it.
  for(int i=0; i<2 && !joined; i++)
  {
    sendRawCommand(F("mac join otaa"));
    // Parse 2nd response
    const char *receivedData = readCharStringUntil(_serial, 30000, '\n', buf, sizeof( buf ) );
      Serial.print( F("***") );
      if ( receivedData != NULL ) {
          Serial.print( receivedData );
      } else {
          Serial.println( F("NO DATA") );
      }
      Serial.println( F("***") );
    if(strncmp( receivedData, "accepted", 8 ) == 0 )
    {
      joined=true;
      delay(1000);
    }
    else
    {
      delay(1000);
    }
  }
  _serial->setTimeout(2000);

  return joined;
}

bool rn2xx3::initABP( const char *devAddr, const char *AppSKey, const char *NwkSKey)
{
#ifdef PANTS  
  _otaa = false;
  _devAddr = devAddr;
  _appskey = AppSKey;
  _nwkskey = NwkSKey;
  String receivedData;

  //clear serial buffer
  while(_serial->available())
    _serial->read();

  configureModuleType();

  switch (_moduleType) {
    case RN2903:
      sendRawCommand(F("mac reset"));
      break;
    case RN2483:
      sendRawCommand(F("mac reset 868"));
      // sendRawCommand(F("mac set rx2 3 869525000"));
      // In the past we set the downlink channel here,
      // but setFrequencyPlan is a better place to do it.
      break;
    default:
      // we shouldn't go forward with the init
      return false;
  }

  sendRawCommand(F("mac set nwkskey "), _nwkskey.c_str() );
  sendRawCommand(F("mac set appskey "), _appskey.c_str() );
  sendRawCommand(F("mac set devaddr "), _devAddr.c_str() );
  sendRawCommand(F("mac set adr off"));

  // Switch off automatic replies, because this library can not
  // handle more than one mac_rx per tx. See RN2483 datasheet,
  // 2.4.8.14, page 27 and the scenario on page 19.
  sendRawCommand(F("mac set ar off"));

  if (_moduleType == RN2903)
  {
    sendRawCommand("mac set pwridx 5");
  }
  else
  {
    sendRawCommand(F("mac set pwridx 1"));
  }
  sendRawCommand(F("mac set dr 5")); //0= min, 7=max

  _serial->setTimeout(60000);
  sendRawCommand(F("mac save"));
  sendRawCommand(F("mac join abp"));
  receivedData = _serial->readStringUntil('\n');

  _serial->setTimeout(2000);
  delay(1000);

  if(receivedData.startsWith("accepted"))
  {
    return true;
    //with abp we can always join successfully as long as the keys are valid
  }
  else
  {
    return false;
  }
#endif

  return true;
}

/**
 * Rejoin via ABP. This assumes all network state is saved onto the EEPROM
 */
bool rn2xx3::rejoinABP()
{
    
  _serial->setTimeout(60000);
  const char *receivedData = sendRawCommand(F("mac join abp"));
    if ( receivedData == NULL ) {
        return false;
    }
    
    Serial.print( F("***") );
    Serial.print( receivedData );
    Serial.println( F("***") );

    if( strncmp( receivedData, "ok", 2 ) == 0 ) {
        receivedData = readCharStringUntil(_serial, 30000, '\n', buf, sizeof( buf ) );
        if ( receivedData == NULL ) {
            return false;
        }
        Serial.print( F("***") );
        Serial.print( receivedData );
        Serial.println( F("***") );
        if ( strncmp(receivedData, "accepted", 8) == 0 ) {
            return true;
            //with abp we can always join successfully as long as the keys are valid
        } else {
            return false;
        }
    }

  return false;
}

TX_RETURN_TYPE rn2xx3::tx( const char *data)
{
  return txUncnf(data); //we are unsure which mode we're in. Better not to wait for acks.
}

TX_RETURN_TYPE rn2xx3::txBytes(const byte* data, uint8_t size)
{
  char msgBuffer[size*2 + 1];

  char buffer[3];
  for (unsigned i=0; i<size; i++)
  {
    sprintf(buffer, "%02X", data[i]);
    memcpy(&msgBuffer[i*2], &buffer, sizeof(buffer));
  }
  return txCommand("mac tx uncnf 1 ", msgBuffer, false);
}

TX_RETURN_TYPE rn2xx3::txCnf(const char *data)
{
  return txCommand("mac tx cnf 1 ", data, true);
}

TX_RETURN_TYPE rn2xx3::txUncnf(const char *data)
{
  return txCommand("mac tx uncnf 1 ", data, false);
}

TX_RETURN_TYPE rn2xx3::txCommand(const char *command, const char *data, bool expectDownlink)
{
  bool send_success = false;
  uint16_t busy_count = 0;
  uint16_t retry_count = 0;
  uint8_t no_free_ch_count = 0;

  //clear serial buffer
  while(_serial->available())
    _serial->read();

    // Switch off automatic replies, because this library can not
    // handle more than one mac_rx per tx. See RN2483 datasheet,
    // 2.4.8.14, page 27 and the scenario on page 19.
      if ( expectDownlink ) {
          sendRawCommand(F("mac set ar on"));
      } else {
          sendRawCommand(F("mac set ar off"));
      }
    
  while(!send_success)
  {

    if ( no_free_ch_count > 5 ) {
        /** Exceeded duty cycle....just bail now */
        return TX_NO_FREE_CH;
    }
    //retransmit a maximum of 10 times
    retry_count++;
    if(retry_count>10)
    {
      return TX_FAIL;
    }

      Serial.print(command);
      Serial.print(data);
    _serial->print(command);
    // if(shouldEncode)
    // {
    //   sendEncoded(data);
    // }
    // else
    // {
      _serial->print(data);
    // }
    _serial->println();

    const char *receivedData = readCharStringUntil(_serial, 10000, '\n', buf, sizeof( buf ) );
    if ( receivedData == NULL ) {
        Serial.println( F("NULL data received") );
        Serial.flush();
        return TX_FAIL;
    }
    //TODO: Debug print on receivedData
    Serial.print( ("post-tx received data: ***" ) );
    Serial.print( receivedData );
    Serial.println( "***" );
    Serial.flush();

    if(strncmp(receivedData, "ok", 2) == 0)
    {
        Serial.println( F("ok received. waiting on post-uplink") );
        Serial.flush();
      _serial->setTimeout(30000);
      receivedData = readCharStringUntil(_serial, 120000, '\n', buf, sizeof( buf ) );
      _serial->setTimeout(2000);

        if ( receivedData == NULL ) {
            Serial.println( F("failed to receive uplink data") );
            Serial.flush();
            return NULL;
        }

      //TODO: Debug print on receivedData
      Serial.print( ("post-uplink received data: ***" ) );
      Serial.print( receivedData );
      Serial.println( "***" );
        Serial.flush();

      if(strncmp(receivedData, "mac_tx_ok", 9) == 0 )
      {
        //SUCCESS!!
          if ( expectDownlink ) {
                Serial.println( F("ok received. waiting on post-downlink") );
                Serial.flush();
              _serial->setTimeout(30000);
              receivedData = readCharStringUntil(_serial, 120000, '\n', buf, sizeof( buf ) );
              _serial->setTimeout(2000);

                if ( receivedData == NULL ) {
                    Serial.println( F("failed to receive downlink data") );
                    Serial.flush();
                    return NULL;
                }

              //TODO: Debug print on receivedData
              Serial.print( ("post-downlink received data: ***" ) );
              Serial.print( receivedData );
              Serial.println( "***" );
                Serial.flush();

              if ( strncmp( receivedData, "mac_rx", 6 ) == 0 ) {
                  send_success = true;
                  return TX_SUCCESS;
              } else {
                  send_success = false;
                  return TX_FAIL;
              }
          }
        send_success = true;
        return TX_SUCCESS;
      }

      else if(strncmp(receivedData, "mac_rx", 6) == 0 )
      {
        //example: mac_rx 1 54657374696E6720313233
//        FIXME_rxMessage = receivedData.substring(receivedData.indexOf(' ', 7)+1);
        send_success = true;
        return TX_WITH_RX;
      }

      else if(strncmp(receivedData, "mac_err", 7) == 0 )
      {
//        init();
          return TX_FAIL;
      }

      else if(strncmp(receivedData, "invalid_data_len", 16) == 0)
      {
        //this should never happen if the prototype worked
        send_success = true;
        return TX_FAIL;
      }

      else if(strncmp(receivedData, "radio_tx_ok", 11) == 0)
      {
        //SUCCESS!!
        send_success = true;
        return TX_SUCCESS;
      }

      else if(strncmp(receivedData, "radio_err", 9) == 0)
      {
        //This should never happen. If it does, something major is wrong.
        init();
      }

      else
      {
        //unknown response
        //init();
      }
    }

    else if(strncmp(receivedData, "invalid_param", 13) == 0)
    {
      //should not happen if we typed the commands correctly
      send_success = true;
      return TX_FAIL;
    }

    else if(strncmp(receivedData, "not_joined", 10) == 0)
    {
      init();
    }

    else if(strncmp(receivedData, "no_free_ch", 10) == 0)
    {
      no_free_ch_count++;
      delay(1000);
    }

    else if(strncmp(receivedData, "silent", 6) == 0)
    {
      init();
    }

    else if(strncmp(receivedData, "frame_counter_err_rejoin_needed", 31) == 0)
    {
      init();
    }

    else if(strncmp(receivedData, "busy", 4) == 0)
    {
      busy_count++;

      // Not sure if this is wise. At low data rates with large packets
      // this can perhaps cause transmissions at more than 1% duty cycle.
      // Need to calculate the correct constant value.
      // But it is wise to have this check and re-init in case the
      // lorawan stack in the RN2xx3 hangs.
      if(busy_count>=10)
      {
        init();
      }
      else
      {
        delay(1000);
      }
    }

    else if(strncmp(receivedData, "mac_paused", 10) == 0)
    {
      init();
    }

    else if(strncmp(receivedData, "invalid_data_len", 16) == 0)
    {
      //should not happen if the prototype worked
      send_success = true;
      return TX_FAIL;
    }

    else
    {
      //unknown response after mac tx command
      init();
    }
  }

  return TX_FAIL; //should never reach this
}

// void rn2xx3::sendEncoded(String input)
// {
//   char working;
//   char buffer[3];
//   for (unsigned i=0; i<input.length(); i++)
//   {
//     working = input.charAt(i);
//     sprintf(buffer, "%02x", int(working));
//     _serial->print(buffer);
//   }
// }

// String rn2xx3::base16encode(String input)
// {
//   char charsOut[input.length()*2+1];
//   char charsIn[input.length()+1];
//   input.trim();
//   input.toCharArray(charsIn, input.length()+1);

//   unsigned i = 0;
//   for(i = 0; i<input.length()+1; i++)
//   {
//     if(charsIn[i] == '\0') break;

//     int value = int(charsIn[i]);

//     char buffer[3];
//     sprintf(buffer, "%02x", value);
//     charsOut[2*i] = buffer[0];
//     charsOut[2*i+1] = buffer[1];
//   }
//   charsOut[2*i] = '\0';
//   String toReturn = String(charsOut);
//   return toReturn;
// }

char *rn2xx3::getRx() {
  return _rxMessage;
}

int rn2xx3::getSNR()
{
  String snr = sendRawCommand(F("radio get snr"));
  snr.trim();
  return snr.toInt();
}

// String rn2xx3::base16decode(String input)
// {
//   char charsIn[input.length()+1];
//   char charsOut[input.length()/2+1];
//   input.trim();
//   input.toCharArray(charsIn, input.length()+1);

//   unsigned i = 0;
//   for(i = 0; i<input.length()/2+1; i++)
//   {
//     if(charsIn[i*2] == '\0') break;
//     if(charsIn[i*2+1] == '\0') break;

//     char toDo[2];
//     toDo[0] = charsIn[i*2];
//     toDo[1] = charsIn[i*2+1];
//     int out = strtoul(toDo, 0, 16);

//     if(out<128)
//     {
//       charsOut[i] = char(out);
//     }
//   }
//   charsOut[i] = '\0';
//   return charsOut;
// }

void rn2xx3::setDR(int dr)
{
  if(dr>=0 && dr<=5)
  {
    delay(100);
    while(_serial->available())
      _serial->read();
    _serial->print("mac set dr ");
    _serial->println(dr);
    _serial->readStringUntil('\n');
  }
}

void rn2xx3::sleep(long msec)
{
  _serial->print("sys sleep ");
  _serial->println(msec);
}

char *rn2xx3::sendRawCommand( const __FlashStringHelper *command ) {
    delay(100);
#ifdef ARDUINO
  Serial.print( F("RAW: ***") );
  Serial.print( command );
  Serial.println( F("***") );
#endif
    while( _serial->available() ) {
        _serial->read();
    }
    _serial->println( command );
    //String ret = _serial->readStringUntil('\n');
    readCharStringUntil( _serial, _timeout, '\n', buf, sizeof( buf ) );
#ifdef ARDUINO
    Serial.print( F("***") );
    Serial.print( buf );
    Serial.println( F("***") );
#endif
    //ret.trim();

    //TODO: Add debug print

    return buf;
}

/** Command should have a space at the end... */
char *rn2xx3::sendRawCommand( const __FlashStringHelper *command, const char *arg ) {
  delay(100);
#ifdef ARDUINO
  Serial.print( F("RAW: ***") );
  Serial.print( command );
  Serial.print( arg );
  Serial.println( F("***") );
#endif
  while(_serial->available())
    _serial->read();
  _serial->print(command);
  _serial->println(arg);
  //String ret = _serial->readStringUntil('\n');
  readCharStringUntil( _serial, _timeout, '\n', buf, sizeof( buf ) );
#ifdef ARDUINO
    Serial.print( F("***") );
    Serial.print( buf );
    Serial.println( F("***") );
#endif
  //ret.trim();

  //TODO: Add debug print

  return buf;
}

char *rn2xx3::sendRawCommand( char *command ) {
  delay(100);
#ifdef ARDUINO
  Serial.print( F("RAW: ***") );
  Serial.print( command );
  Serial.println( F("***") );
#endif
  while(_serial->available())
    _serial->read();
  _serial->println(command);
  readCharStringUntil( _serial, _timeout, '\n', buf, sizeof( buf ) );
#ifdef ARDUINO
    Serial.print( F("***") );
    Serial.print( buf );
    Serial.println( F("***") );
#endif
  //ret.trim();

  return buf;
}

RN2xx3_t rn2xx3::moduleType()
{
  return _moduleType;
}

bool rn2xx3::setFrequencyPlan(FREQ_PLAN fp)
{
  bool returnValue;

  switch (fp)
  {
    case SINGLE_CHANNEL_EU:
    {
      if(_moduleType == RN2483)
      {
        //mac set rx2 <dataRate> <frequency>
        //sendRawCommand(F("mac set rx2 5 868100000")); //use this for "strict" one channel gateways
        sendRawCommand(F("mac set rx2 3 869525000")); //use for "non-strict" one channel gateways
        sendRawCommand(F("mac set ch dcycle 0 99")); //1% duty cycle for this channel
        sendRawCommand(F("mac set ch dcycle 1 65535")); //almost never use this channel
        sendRawCommand(F("mac set ch dcycle 2 65535")); //almost never use this channel

        returnValue = true;
      }
      else
      {
        returnValue = false;
      }
      break;
    }

    case TTN_EU:
    {
      if(_moduleType == RN2483)
      {
      /*
       * The <dutyCycle> value that needs to be configured can be
       * obtained from the actual duty cycle X (in percentage)
       * using the following formula: <dutyCycle> = (100/X) – 1
       *
       *  10% -> 9
       *  1% -> 99
       *  0.33% -> 299
       *  8 channels, total of 1% duty cycle:
       *  0.125% per channel -> 799
       *
       * Most of the TTN_EU frequency plan was copied from:
       * https://github.com/TheThingsNetwork/arduino-device-lib
       */

        //RX window 2
        sendRawCommand(F("mac set rx2 3 869525000"));

        //channel 0
        sendRawCommand(F("mac set ch dcycle 0 799"));

        //channel 1
        sendRawCommand(F("mac set ch drrange 1 0 6"));
        sendRawCommand(F("mac set ch dcycle 1 799"));

        //channel 2
        sendRawCommand(F("mac set ch dcycle 2 799"));

        //channel 3
        sendRawCommand(F("mac set ch freq 3 867100000"));
        sendRawCommand(F("mac set ch drrange 3 0 5"));
        sendRawCommand(F("mac set ch dcycle 3 799"));
        sendRawCommand(F("mac set ch status 3 on"));

        //channel 4
        sendRawCommand(F("mac set ch freq 4 867300000"));
        sendRawCommand(F("mac set ch drrange 4 0 5"));
        sendRawCommand(F("mac set ch dcycle 4 799"));
        sendRawCommand(F("mac set ch status 4 on"));

        //channel 5
        sendRawCommand(F("mac set ch freq 5 867500000"));
        sendRawCommand(F("mac set ch drrange 5 0 5"));
        sendRawCommand(F("mac set ch dcycle 5 799"));
        sendRawCommand(F("mac set ch status 5 on"));

        //channel 6
        sendRawCommand(F("mac set ch freq 6 867700000"));
        sendRawCommand(F("mac set ch drrange 6 0 5"));
        sendRawCommand(F("mac set ch dcycle 6 799"));
        sendRawCommand(F("mac set ch status 6 on"));

        //channel 7
        sendRawCommand(F("mac set ch freq 7 867900000"));
        sendRawCommand(F("mac set ch drrange 7 0 5"));
        sendRawCommand(F("mac set ch dcycle 7 799"));
        sendRawCommand(F("mac set ch status 7 on"));

        returnValue = true;
      }
      else
      {
        returnValue = false;
      }

      break;
    }

    case TTN_US:
    {
    /*
     * Most of the TTN_US frequency plan was copied from:
     * https://github.com/TheThingsNetwork/arduino-device-lib
     */
      if(_moduleType == RN2903)
      {
        for(int channel=0; channel<72; channel++)
        {
          // Build command string. First init, then add int.
#ifdef PANTS
          String command = F("mac set ch status ");
          command += channel;

          if(channel>=8 && channel<16)
          {
            sendRawCommand(command+F(" on"));
          }
          else
          {
            sendRawCommand(command+F(" off"));
          }
#endif
        }
        returnValue = true;
      }
      else
      {
        returnValue = false;
      }
      break;
    }

    case DEFAULT_EU:
    {
      if(_moduleType == RN2483)
      {
        //fix duty cycle - 1% = 0.33% per channel
        sendRawCommand(F("mac set ch dcycle 0 799"));
        sendRawCommand(F("mac set ch dcycle 1 799"));
        sendRawCommand(F("mac set ch dcycle 2 799"));

        //disable non-default channels
        sendRawCommand(F("mac set ch status 3 on"));
        sendRawCommand(F("mac set ch status 4 on"));
        sendRawCommand(F("mac set ch status 5 on"));
        sendRawCommand(F("mac set ch status 6 on"));
        sendRawCommand(F("mac set ch status 7 on"));

        returnValue = true;
      }
      else
      {
        returnValue = false;
      }

      break;
    }
    default:
    {
      //set default channels 868.1, 868.3 and 868.5?
      returnValue = false; //well we didn't do anything, so yes, false
      break;
    }
  }

  return returnValue;
}

bool rn2xx3::isJoined() {
    sendRawCommand(F("mac get status"));
//    Serial.print( F("Status: ***") );
//    Serial.print( rv );
//    Serial.println( F("***") );

    /** Decode the status packet */
    /** We're only interested in bit0 */
    if ( strlen(buf) == 4 ) {
        /** Up to 1.0.3 RN2483 firmware */
        uint8_t bval3 = (int)buf[3] - '0';
        return (bval3 & 0x01) == 0x01;
    } else {
        if ( strlen(buf) == 8 ) {
            /** 1.0.4+ RN2483 firmware */
            uint8_t bval3 = (int)buf[6] - '0';
            return (bval3 & 0x01) == 0x01;
        } else {
            return false;
        }
    }

}

char *rn2xx3::factoryReset() {
    return sendRawCommand( F("sys factoryRESET") );
}

char *rn2xx3::getRadioPower() {
    return sendRawCommand( F("radio get pwr") );
}

bool rn2xx3::setRadioPower( int pwr ) {
    char b[32];
    sprintf( b, "radio set pwr %d", pwr );
    sendRawCommand( b );
    return true;
}
