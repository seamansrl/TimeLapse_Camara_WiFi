#define FTP_SERVERESP_H

//#include "Streaming.h"
#include "SD_MMC.h"
#include <FS.h>
#include <WiFiClient.h>

#define FTP_SERVER_VERSION "FTP-2021-06-10"

#define FTP_CTRL_PORT    21
#define FTP_DATA_PORT_PASV 50009

#define FTP_TIME_OUT  5
#define FTP_CMD_SIZE 255 + 8
#define FTP_CWD_SIZE 255 + 8
#define FTP_FIL_SIZE 255

//#define FTP_BUF_SIZE 512
//#define FTP_BUF_SIZE 2*1460
#define FTP_BUF_SIZE 4096

class FtpServer
{
public:
  void    begin(String uname, String pword);
  void    handleFTP();

private:
 bool haveParameter();
bool    makeExistsPath( char * path, char * param = NULL );
  void    iniVariables();
  void    clientConnected();
  void    disconnectClient();
  boolean userIdentity();
  boolean userPassword();
  boolean processCommand();
  boolean dataConnect();
  boolean doRetrieve();
  boolean doStore();
  void    closeTransfer();
  void    abortTransfer();
  boolean makePath( char * fullname );
  boolean makePath( char * fullName, char * param );
  uint8_t getDateTime( uint16_t * pyear, uint8_t * pmonth, uint8_t * pday,
                       uint8_t * phour, uint8_t * pminute, uint8_t * second );
  char *  makeDateTimeStr( char * tstr, uint16_t date, uint16_t time );
  int8_t  readChar();

  IPAddress      dataIp;              // IP address of client for data
  WiFiClient client;
  WiFiClient data;

  File file;
  
  boolean  dataPassiveConn;
  uint16_t dataPort;
  char     buf[ FTP_BUF_SIZE ];       // data buffer for transfers
  char     cmdLine[ FTP_CMD_SIZE ];   // where to store incoming char from client
  char     cwdName[ FTP_CWD_SIZE ];   // name of current directory
  char     command[ 5 ];              // command sent by client
  boolean  rnfrCmd;                   // previous command was RNFR
  char *   parameters;                // point to begin of parameters sent by client
  uint16_t iCL;                       // pointer to cmdLine next incoming char
  int8_t   cmdStatus,                 // status of ftp command connexion
           transferStatus;            // status of ftp data transfer
  uint32_t millisTimeOut,             // disconnect after 5 min of inactivity
           millisDelay,
           millisEndConnection,       // 
           millisBeginTrans,          // store time of beginning of a transaction
           bytesTransfered;           //
  String   _FTP_USER;
  String   _FTP_PASS;

  

};
