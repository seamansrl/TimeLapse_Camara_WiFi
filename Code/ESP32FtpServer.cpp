#include "ESP32FtpServer.h"

#include <WiFi.h>
#include <FS.h>
#include "SD_MMC.h"

WiFiServer ftpServer( FTP_CTRL_PORT );
WiFiServer dataServer( FTP_DATA_PORT_PASV );

void FtpServer::begin(String uname, String pword)
{
  // Tells the ftp server to begin listening for incoming connection
	_FTP_USER=uname;
	_FTP_PASS = pword;

	ftpServer.begin();
	delay(10);
	dataServer.begin();	
	delay(10);
	millisTimeOut = (uint32_t)FTP_TIME_OUT * 60 * 1000;
	millisDelay = 0;
	cmdStatus = 0;
    iniVariables();
}

void FtpServer::iniVariables()
{
  // Default for data port
  dataPort = FTP_DATA_PORT_PASV;
  
  // Default Data connection is Active
  dataPassiveConn = true;
  
  // Set the root directory
  strcpy( cwdName, "/" );

  rnfrCmd = false;
  transferStatus = 0;
  
}

void FtpServer::handleFTP()
{
  if((int32_t) ( millisDelay - millis() ) > 0 )
    return;

  if (ftpServer.hasClient()) {
	  client.stop();
	  client = ftpServer.available();
  }
  
  if( cmdStatus == 0 )
  {
    if( client.connected())
      disconnectClient();
    cmdStatus = 1;
  }
  else if( cmdStatus == 1 ) 
  {
    abortTransfer();
    iniVariables();
    cmdStatus = 2;
  }
  else if( cmdStatus == 2 )
  {
   		
    if( client.connected() )
    {
      clientConnected();      
      millisEndConnection = millis() + 10 * 1000 ;
      cmdStatus = 3;
    }
  }
  else if( readChar() > 0 ) 
  {
    if( cmdStatus == 3 ) 
      if( userIdentity() )
        cmdStatus = 4;
      else
        cmdStatus = 0;
    else if( cmdStatus == 4 )
      if( userPassword() )
      {
        cmdStatus = 5;
        millisEndConnection = millis() + millisTimeOut;
      }
      else
        cmdStatus = 0;
    else if( cmdStatus == 5 )
      if( ! processCommand())
        cmdStatus = 0;
      else
        millisEndConnection = millis() + millisTimeOut;
  }
  else if (!client.connected() || !client)
  {
	  cmdStatus = 1;
  }

  if( transferStatus == 1 )
  {
    if( ! doRetrieve())
      transferStatus = 0;
  }
  else if( transferStatus == 2 )
  {
    if( ! doStore())
      transferStatus = 0;
  }
  else if( cmdStatus > 2 && ! ((int32_t) ( millisEndConnection - millis() ) > 0 ))
  {
	  client.println("530 Timeout");
    millisDelay = millis() + 200;
    cmdStatus = 0;
  }
}

void FtpServer::clientConnected()
{
  client.println( "220--- Welcome to FTP for ESP32-CAM ---");
  client.println( "220---   By Fernando P. Maniglia   ---");
  client.println( "220 --   Version "+ String(FTP_SERVER_VERSION) +"   --");
  iCL = 0;
}

void FtpServer::disconnectClient()
{
  abortTransfer();
  client.println("221 Goodbye");
  client.stop();
}

boolean FtpServer::userIdentity()
{	
  if( strcmp( command, "USER" ))
    client.println( "500 Syntax error");
  if( strcmp( parameters, _FTP_USER.c_str() ))
    client.println( "530 user not found");
  else
  {
    client.println( "331 OK. Password required");
    strcpy( cwdName, "/" );
    return true;
  }
  millisDelay = millis() + 100;  // delay of 100 ms
  return false;
}

boolean FtpServer::userPassword()
{
  if( strcmp( command, "PASS" ))
    client.println( "500 Syntax error");
  else if( strcmp( parameters, _FTP_PASS.c_str() ))
    client.println( "530 ");
  else
  {
    client.println( "230 OK.");
    return true;
  }
  millisDelay = millis() + 100;  // delay of 100 ms
  return false;
}

boolean FtpServer::processCommand()
{
  if( ! strcmp( command, "CDUP" ) || ( ! strcmp( command, "CWD" ) && ! strcmp( parameters, ".." )))
  {
	 bool ok = false;
	 if( strlen( cwdName ) > 1 )            // do nothing if cwdName is root
    {
      // if cwdName ends with '/', remove it (must not append)
      if( cwdName[ strlen( cwdName ) - 1 ] == '/' )
        cwdName[ strlen( cwdName ) - 1 ] = 0;
      // search last '/'
      char * pSep = strrchr( cwdName, '/' );
      ok = pSep > cwdName;
      // if found, ends the string on its position
      if( ok )
      {
        * pSep = 0;
        ok = SD_MMC.exists( cwdName );
      }
    }
    // if an error appends, move to root
    if( ! ok )
      strcpy( cwdName, "/" );
   // client << F("250 Ok. Current directory is ") << cwdName << eol;
	 
	 client.println("250 Ok. Current directory is " + String(cwdName));
  }
  else if( ! strcmp( command, "CWD" ))
  { 
    
    
  char path[ FTP_CWD_SIZE ];
    if( haveParameter() && makeExistsPath( path ))
    {
      strcpy( cwdName, path );
       client.println( "250 Ok. Current directory is " + String(cwdName) );
    }  
  }
  else if( ! strcmp( command, "PWD" ))
    client.println( "257 \"" + String(cwdName) + "\" is your current directory");
  else if( ! strcmp( command, "QUIT" ))
  {
    disconnectClient();
    return false;
  }
  else if( ! strcmp( command, "MODE" ))
  {
    if( ! strcmp( parameters, "S" ))
      client.println( "200 S Ok");
    // else if( ! strcmp( parameters, "B" ))
    //  client.println( "200 B Ok\r\n";
    else
      client.println( "504 Only S(tream) is suported");
  }
  else if( ! strcmp( command, "PASV" ))
  {
    if (data.connected()) data.stop();
    
	  dataIp = WiFi.softAPIP();	
	  dataPort = FTP_DATA_PORT_PASV;

   client.println( "227 Entering Passive Mode ("+ String(dataIp[0]) + "," + String(dataIp[1])+","+ String(dataIp[2])+","+ String(dataIp[3])+","+String( dataPort >> 8 ) +","+String ( dataPort & 255 )+").");
   dataPassiveConn = true;
  }

  else if( ! strcmp( command, "PORT" ))
  {
	if (data) data.stop();
    // get IP of data client
    dataIp[ 0 ] = atoi( parameters );
    char * p = strchr( parameters, ',' );
    for( uint8_t i = 1; i < 4; i ++ )
    {
      dataIp[ i ] = atoi( ++ p );
      p = strchr( p, ',' );
    }
    // get port of data client
    dataPort = 256 * atoi( ++ p );
    p = strchr( p, ',' );
    dataPort += atoi( ++ p );
    if( p == NULL )
      client.println( "501 Can't interpret parameters");
    else
    {
      
		client.println("200 PORT command successful");
      dataPassiveConn = false;
    }
  }
  else if( ! strcmp( command, "STRU" ))
  {
    if( ! strcmp( parameters, "F" ))
      client.println( "200 F Ok");
    // else if( ! strcmp( parameters, "R" ))
    //  client.println( "200 B Ok\r\n";
    else
      client.println( "504 Only F(ile) is suported");
  }
  else if( ! strcmp( command, "TYPE" ))
  {
    if( ! strcmp( parameters, "A" ))
      client.println( "200 TYPE is now ASII");
    else if( ! strcmp( parameters, "I" ))
      client.println( "200 TYPE is now 8-bit binary");
    else
      client.println( "504 Unknow TYPE");
  }
  else if( ! strcmp( command, "ABOR" ))
  {
    abortTransfer();
    client.println( "226 Data connection closed");
  }
  else if( ! strcmp( command, "DELE" ))
  {
    char path[ FTP_CWD_SIZE ];
    if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( path ))
    {
      if( ! SD_MMC.exists( path ))
        client.println( "550 File " + String(parameters) + " not found");
      else
      {
        if( SD_MMC.remove( path ))
          client.println( "250 Deleted " + String(parameters) );
        else
          client.println( "450 Can't delete " + String(parameters));
      }
    }
  }
  else if( ! strcmp( command, "LIST" ))
  {
     if(dataConnect()){
     client.println( "150 Accepted data connection");
      uint16_t nm = 0;
      File dir=SD_MMC.open(cwdName);
     if((!dir)||(!dir.isDirectory()))
        client.println( "550 Can't open directory " + String(cwdName) );
      else
      {
        File file = dir.openNextFile();
        while( file)
        {
          String fn, fs;
          fn = file.name();
          int i = fn.lastIndexOf("/")+1;
          fn.remove(0, i);
          fs = String(file.size());
          if(file.isDirectory()){
            data.println( "01-01-2000  00:00AM <DIR> " + fn);
          } else {
            data.println( "01-01-2000  00:00AM " + fs + " " + fn);
          }
          nm ++;
          file = dir.openNextFile();
        }
        client.println( "226 " + String(nm) + " matches total");
        data.stop();
      }
      
      }
      else{
        client.println( "425 No data connection");
        data.stop();
        }    
  }
  else if( ! strcmp( command, "MLSD" ))
  {
    if( ! dataConnect())
      client.println( "425 No data connection MLSD");
    else
    {
	  client.println( "150 Accepted data connection");
      uint16_t nm = 0;
      File dir= SD_MMC.open(cwdName);
      char dtStr[ 15 ];
    //  if(!SD.exists(cwdName))
     if((!dir)||(!dir.isDirectory()))
        client.println( "550 Can't open directory " +String(cwdName) );
      else
      {
        File file = dir.openNextFile();
        while( file)
    		{
        
    			String fn,fs;
          fn = file.name();
          int pos = fn.lastIndexOf("/"); 
          fn.remove(0, pos+1); 
          fs = String(file.size());
          
          if(file.isDirectory())
          {
	          data.println(fn);
          } 
          else 
          {
	          data.println( fs + " " + fn);
          }
          nm ++;
          file = dir.openNextFile();
        }
        client.println( "226-options: -a -l");
        client.println( "226 " + String(nm) + " matches total");
      }
      data.stop();
    }
  }
  else if( ! strcmp( command, "NLST" ))
  {
    if( ! dataConnect())
      client.println( "425 No data connection");
    else
    {
      client.println( "150 Accepted data connection");
      uint16_t nm = 0;
      File dir= SD_MMC.open(cwdName);
      if( !SD_MMC.exists( cwdName ))
        client.println( "550 Can't open directory " + String(parameters));
      else
      {
          File file = dir.openNextFile();
          
        while( file)
        {
          data.println( file.name());
          nm ++;
          file = dir.openNextFile();
        }
        client.println( "226 " + String(nm) + " matches total");
      }
      data.stop();
    }
  }
  else if( ! strcmp( command, "NOOP" ))
  {
    client.println( "200 Zzz...");
  }
  else if( ! strcmp( command, "RETR" ))
  {
    char path[ FTP_CWD_SIZE ];
    if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( path ))
	{
		file = SD_MMC.open(path, "r");
      if( !file)
        client.println( "550 File " +String(parameters)+ " not found");
      else if( !file )
        client.println( "450 Can't open " +String(parameters));
      else if( ! dataConnect())
        client.println( "425 No data connection");
      else
      {
        client.println( "150-Connected to port "+ String(dataPort));
        client.println( "150 " + String(file.size()) + " bytes to download");
        millisBeginTrans = millis();
        bytesTransfered = 0;
        transferStatus = 1;
      }
    }
  }
  else if( ! strcmp( command, "STOR" ))
  {
    char path[ FTP_CWD_SIZE ];
    if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( path ))
    {
		file = SD_MMC.open(path, "w");
      if( !file)
        client.println( "451 Can't open/create " +String(parameters) );
      else if( ! dataConnect())
      {
        client.println( "425 No data connection");
        file.close();
      }
      else
      {
        client.println( "150 Connected to port " + String(dataPort));
        millisBeginTrans = millis();
        bytesTransfered = 0;
        transferStatus = 2;
      }
    }
  }
  else if( ! strcmp( command, "MKD" ))
  {
     char path[ FTP_CWD_SIZE ];
     if( haveParameter() && makePath( path )){
      if (SD_MMC.exists( path )){
        client.println( "521 Can't create \"" + String(parameters) + ", Directory exists");
        }
        else
        {
          if( SD_MMC.mkdir( path )){
            client.println( "257 \"" + String(parameters) + "\" created");
            }
            else{
              client.println( "550 Can't create \"" + String(parameters));
              }
          }
      
      }
	 
  }
  else if( ! strcmp( command, "RMD" ))
  {
	 char path[ FTP_CWD_SIZE ];
     if( haveParameter() && makePath( path ))
     {
      if( SD_MMC.rmdir( path ))
      {
        client.println( "250 \"" + String(parameters) + "\" deleted");
      }
      else
      {
        client.println( "550 Can't remove \"" + String(parameters) + "\". Directory not empty?");  
      }
    } 
  }
  else if( ! strcmp( command, "RNFR" ))
  {
    buf[ 0 ] = 0;
    if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( buf ))
    {
      if( ! SD_MMC.exists( buf ))
        client.println( "550 File " +String(parameters)+ " not found");
      else
      {
        client.println( "350 RNFR accepted - file exists, ready for destination");     
        rnfrCmd = true;
      }
    }
  }
  else if( ! strcmp( command, "RNTO" ))
  {  
    char path[ FTP_CWD_SIZE ];
    char dir[ FTP_FIL_SIZE ];
    if( strlen( buf ) == 0 || ! rnfrCmd )
      client.println( "503 Need RNFR before RNTO");
    else if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( path ))
    {
      if( SD_MMC.exists( path ))
        client.println( "553 " +String(parameters)+ " already exists");
      else
      {          
        if( SD_MMC.rename( buf, path ))
          client.println( "250 File successfully renamed or moved");
        else
  	      client.println( "451 Rename/move failure");                           
      }
    }
    rnfrCmd = false;
  }
  else if( ! strcmp( command, "FEAT" ))
  {
    client.println( "211-Extensions suported:");
    client.println( " MLSD");
    client.println( "211 End.");
  }
  else if (!strcmp(command, "MDTM"))
  {
	  client.println("550 Unable to retrieve time");
  }
  else if( ! strcmp( command, "SIZE" ))
  {
    char path[ FTP_CWD_SIZE ];
    if( strlen( parameters ) == 0 )
      client.println( "501 No file name");
    else if( makePath( path ))
	{
		file = SD_MMC.open(path, "r");
      if(!file)
         client.println( "450 Can't open " +String(parameters) );
      else
      {
        client.println( "213 " + String(file.size()));
        file.close();
      }
    }
  }
  else if( ! strcmp( command, "SITE" ))
  {
      client.println( "500 Unknow SITE command " +String(parameters) );
  }
  else
    client.println( "500 Unknow command");
  
  return true;
}

boolean FtpServer::dataConnect()
{
  unsigned long startTime = millis();
  //wait 5 seconds for a data connection
  if (!data.connected())
  {
    while (!dataServer.hasClient() && millis() - startTime < 10000)
	  {
		  delay(100);
		  yield();
	  }
    if (dataServer.hasClient()) {
		  data.stop();
		  data = dataServer.available();
	  }
  }

  return data.connected();

}

boolean FtpServer::doRetrieve()
{
if (data.connected())
{
  int16_t nb = file.readBytes(buf, FTP_BUF_SIZE);
  if (nb > 0)
    {
    data.write((uint8_t*)buf, nb);
    bytesTransfered += nb;
    return true;
  }
}
closeTransfer();
return false;
}


boolean FtpServer::doStore()
{
  if( data.connected() )
  {
    int16_t nb = data.readBytes((uint8_t*) buf, FTP_BUF_SIZE );
    if( nb > 0 )
    {
      file.write((uint8_t*) buf, nb );
      bytesTransfered += nb;
    }
    return true;
  }
  closeTransfer();
  return false;
}

void FtpServer::closeTransfer()
{
  uint32_t deltaT = (int32_t) ( millis() - millisBeginTrans );
  if( deltaT > 0 && bytesTransfered > 0 )
  {
    client.println( "226-File successfully transferred");
    client.println( "226 " + String(deltaT) + " ms, "+ String(bytesTransfered / deltaT) + " kbytes/s");
  }
  else
    client.println( "226 File successfully transferred");
  
  file.close();
  data.stop();
}

void FtpServer::abortTransfer()
{
  if( transferStatus > 0 )
  {
    file.close();
    data.stop(); 
    client.println( "426 Transfer aborted"  );
  }
  transferStatus = 0;
}

int8_t FtpServer::readChar()
{
  int8_t rc = -1;

  if( client.available())
  {
    char c = client.read();

    if( c == '\\' )
      c = '/';
    if( c != '\r' )
      if( c != '\n' )
      {
        if( iCL < FTP_CMD_SIZE )
          cmdLine[ iCL ++ ] = c;
        else
          rc = -2; //  Line too long
      }
      else
      {
        cmdLine[ iCL ] = 0;
        command[ 0 ] = 0;
        parameters = NULL;
        // empty line?
        if( iCL == 0 )
          rc = 0;
        else
        {
          rc = iCL;
          // search for space between command and parameters
          parameters = strchr( cmdLine, ' ' );
          if( parameters != NULL )
          {
            if( parameters - cmdLine > 4 )
              rc = -2; // Syntax error
            else
            {
              strncpy( command, cmdLine, parameters - cmdLine );
              command[ parameters - cmdLine ] = 0;
              
              while( * ( ++ parameters ) == ' ' )
                ;
            }
          }
          else if( strlen( cmdLine ) > 4 )
            rc = -2; // Syntax error.
          else
            strcpy( command, cmdLine );
          iCL = 0;
        }
      }
    if( rc > 0 )
      for( uint8_t i = 0 ; i < strlen( command ); i ++ )
        command[ i ] = toupper( command[ i ] );
    if( rc == -2 )
    {
      iCL = 0;
      client.println( "500 Syntax error");
    }
  }
  return rc;
}

boolean FtpServer::makePath( char * fullName )
{
  return makePath( fullName, parameters );
}

boolean FtpServer::makePath( char * fullName, char * param )
{
  if( param == NULL )
    param = parameters;

  if( strcmp( param, "/" ) == 0 || strlen( param ) == 0 )
  {
    strcpy( fullName, "/" );
    return true;
  }
  // If relative path, concatenate with current dir
  if( param[0] != '/' ) 
  {
    strcpy( fullName, cwdName );
    if( fullName[ strlen( fullName ) - 1 ] != '/' )
      strncat( fullName, "/", FTP_CWD_SIZE );
    strncat( fullName, param, FTP_CWD_SIZE );
  }
  else
    strcpy( fullName, param );
  // If ends with '/', remove it
  uint16_t strl = strlen( fullName ) - 1;
  if( fullName[ strl ] == '/' && strl > 1 )
    fullName[ strl ] = 0;
  if( strlen( fullName ) < FTP_CWD_SIZE )
    return true;

  client.println( "500 Command line too long");
  return false;
}

uint8_t FtpServer::getDateTime( uint16_t * pyear, uint8_t * pmonth, uint8_t * pday,
                                uint8_t * phour, uint8_t * pminute, uint8_t * psecond )
{
  char dt[ 15 ];

  if( strlen( parameters ) < 15 || parameters[ 14 ] != ' ' )
    return 0;
  for( uint8_t i = 0; i < 14; i++ )
    if( ! isdigit( parameters[ i ]))
      return 0;

  strncpy( dt, parameters, 14 );
  dt[ 14 ] = 0;
  * psecond = atoi( dt + 12 ); 
  dt[ 12 ] = 0;
  * pminute = atoi( dt + 10 );
  dt[ 10 ] = 0;
  * phour = atoi( dt + 8 );
  dt[ 8 ] = 0;
  * pday = atoi( dt + 6 );
  dt[ 6 ] = 0 ;
  * pmonth = atoi( dt + 4 );
  dt[ 4 ] = 0 ;
  * pyear = atoi( dt );
  return 15;
}

char * FtpServer::makeDateTimeStr( char * tstr, uint16_t date, uint16_t time )
{
  sprintf( tstr, "%04u%02u%02u%02u%02u%02u",
           (( date & 0xFE00 ) >> 9 ) + 1980, ( date & 0x01E0 ) >> 5, date & 0x001F,
           ( time & 0xF800 ) >> 11, ( time & 0x07E0 ) >> 5, ( time & 0x001F ) << 1 );            
  return tstr;
}

bool FtpServer::haveParameter()
{
  if( parameters != NULL && strlen( parameters ) > 0 )
    return true;
  client.println ("501 No file name");
  return false;  
}
bool FtpServer::makeExistsPath( char * path, char * param )
{
  if( ! makePath( path, param ))
    return false;
  if( SD_MMC.exists( path ))
    return true;
  client.println("550 " + String(path) + " not found.");

  return false;
}
