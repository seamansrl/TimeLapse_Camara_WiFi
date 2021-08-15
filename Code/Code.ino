// PROYECTO DESARROLLADO POR SEAMAN SRL
// EL PRESENTE CODIGO SE ENTREGA SIN GARANTIA ALGUNA

// Segun el tipo de camara la distribucion de pines varia por lo cual usando como referencia el archivo CAMARA_PINS.H deberemos elegir la que corresponda
#define CAMERA_MODEL_AI_THINKER

// Defino las variables a usar en la EEPROM para almacenar el fotograma por el que estamos en caso de que se reinicie la camara
#define ID_ADDRESS            0x00
#define COUNT_ADDRESS         0x01
#define ID_BYTE               0xAA
#define EEPROM_SIZE           0x0F

// Declaramos las librerias, salvando ArduinoJson.h todas las demas estan incluidas en el pack de la ESP32
#include "esp_camera.h"
#include <FS.h>
#include "SD_MMC.h"
#include "EEPROM.h"

// Se instala siguiendo estos pasos: https://arduinojson.org/v6/doc/installation/
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include "ESP32FtpServer.h"
#include "camera_pins.h"

// Defino como calor por defecto entre cuadros 10 segundos, este valor lo podremos modificar en tiempo de ejecucion usando el archivo JSON
int TIME_TO_SLEEP = 10;
double Contador = 0;

uint16_t nextImageNumber = 0;

// Inicio la variable para el servidor FTP
FtpServer ftpSrv;

// Creo las variables de configuracion estandar para la camara, estas podran ser modificadas en tiempo de ejecucion usando el archivo JSON
int brightness = 0;         // -2 to 2
int contrast = 0;           // -2 to 2
int saturation = 0;         // -2 to 2
int special_effect = 0;     // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
int whitebal = 1;           // 0 = disable , 1 = enable
int awb_gain = 1;           // 0 = disable , 1 = enable
int wb_mode = 0;            // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
int exposure_ctrl = 1;      // 0 = disable , 1 = enable
int aec2 = 0;               // 0 = disable , 1 = enable
int ae_level = 0;           // -2 to 2
int aec_value = 300;       // 0 to 1200
int gain_ctrl = 1;          // 0 = disable , 1 = enable
int agc_gain = 0;          // 0 to 30
int gainceiling = 0;        // 0 to 6
int bpc = 0;                // 0 = disable , 1 = enable
int wpc = 1;                // 0 = disable , 1 = enable
int raw_gma = 1;            // 0 = disable , 1 = enable
int lenc = 1;               // 0 = disable , 1 = enable
int hmirror = 0;            // 0 = disable , 1 = enable
int vflip = 0;              // 0 = disable , 1 = enable
int dcw = 1;                // 0 = disable , 1 = enable
int colorbar = 0;           // 0 = disable , 1 = enable

// Creo las variables de configuracion para el AP WiFi, estas podran ser modificadas en tiempo de ejecucion usando el archivo JSON
String ssid = "TimeLapse_Camara";
String password = "TimeLapse2021";

String ConfigFile = "";

// Inicio el servidor WEB
WiFiServer server(80);

// Funcion de recurso, sirve para separar STRINGS segun un caracter especificado
String Split(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length()-1;
    
    for(int i=0; i<=maxIndex && found<=index; i++)
    {
          if(data.charAt(i)==separator || i==maxIndex)
          {
                found++;
                strIndex[0] = strIndex[1]+1;
                strIndex[1] = (i == maxIndex) ? i+1 : i;
          }
    }
    
    return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// Funcion de recurso, sirve para convertir variables de tipo String a tipo *char
char* string2char(String command)
{
    if(command.length()!=0)
    {
        char *p = const_cast<char*>(command.c_str());
        return p;
    }
}

// Funcion de sistema: Borra la memoria EEPROM para volver al contador a cero
void CleanEEPROM()
{
  Serial.println("Erasing EEPROM...");
  for(int i = 0; i < EEPROM_SIZE; i++)
  {
    EEPROM.write(i, 0xFF);
    EEPROM.commit();
    delay(20);
  }
  Serial.println("Erased");
}

// Funcion de sistema: Borra todos los archivos .JPG de la SD
void deleteImages(File dir, int numTabs)
{
  while (true)
  {
    File entry =  dir.openNextFile();
    if (! entry)
    {
      break;
    }

    if (entry.name() != "")
    {
      String FileType = Split(entry.name(), '.', 1);
      FileType.toUpperCase();
      
      if (FileType == "JPG")
        SD_MMC.remove(entry.name());
    }
    entry.close();
  }

  Serial.println("Images deleted");
}

// Funcion de sistema: Cuanta la cantidad de imagenes en la SD
double CountImages(File dir, int numTabs)
{
  double Images = 0;
  
  while (true)
  {
    File entry =  dir.openNextFile();
    if (! entry)
    {
      break;
    }

    if (entry.name() != "")
    {
      String FileType = Split(entry.name(), '.', 1);
      FileType.toUpperCase();
      
      if (FileType == "JPG")
        Images++;
    }
    entry.close();
  }

  return Images;
}

// Funcion de sistema: Obtiene la configuracion del archivo CONFIG.JSON y la asigna a las variables locales
void GetConfig(fs::FS &fs, const char * path)
{
  try
  {             
    File file = fs.open(path);
    if(!file)
    {
        Serial.println("CONFIG FAIL");
        return;
    }

    ConfigFile = "";
    
    while(file.available())
        ConfigFile = ConfigFile + String(char(file.read()));

    file.flush();
    file.close();
    
    const size_t bufferSize = ConfigFile.length() + 1024;
    
    DynamicJsonDocument doc(bufferSize);
    deserializeJson(doc, ConfigFile);
    JsonObject obj = doc.as<JsonObject>();

    if (ConfigFile != "")
    {

        if (obj["time_to_sleep"].as<int>() == NULL)
          TIME_TO_SLEEP = 10;
        else
        {
          TIME_TO_SLEEP = obj["time_to_sleep"].as<int>();

          if (TIME_TO_SLEEP < 10)
            TIME_TO_SLEEP = 10;
        }
        
        if (obj["brightness"].as<int>() == NULL) 
          brightness = 0;
        else
          brightness = obj["brightness"].as<int>();
        
        if (obj["contrast"].as<int>() == NULL) 
          contrast = 0; 
        else
          contrast = obj["contrast"].as<int>();
        
        if (obj["saturation"].as<int>() == NULL) 
          saturation = 0;
        else
          saturation = obj["saturation"].as<int>();
        
        if (obj["special_effect"].as<int>() == NULL) 
          special_effect = 0;
        else
          special_effect = obj["special_effect"].as<int>();
        
        if (obj["whitebal"].as<int>() == NULL) 
          whitebal = 1;
        else
          whitebal = obj["whitebal"].as<int>();
        
        if (obj["awb_gain"].as<int>() == NULL) 
          awb_gain = 1;
        else
          awb_gain = obj["awb_gain"].as<int>();
        
        if (obj["wb_mode"].as<int>() == NULL) 
          wb_mode = 0;
        else
          wb_mode = obj["wb_mode"].as<int>();
        
        if (obj["exposure_ctrl"].as<int>() == NULL) 
          exposure_ctrl = 1;
        else
          exposure_ctrl = obj["exposure_ctrl"].as<int>();
        
        if (obj["aec2"].as<int>() == NULL) 
          aec2 = 0;
        else
          aec2 = obj["aec2"].as<int>();
        
        if (obj["ae_level"].as<int>() == NULL) 
          ae_level = 0; 
        else
          ae_level = obj["ae_level"].as<int>();
        
        if (obj["aec_value"].as<int>() == NULL) 
          aec_value = 300;
        else
          aec_value = obj["aec_value"].as<int>();
        
        if (obj["gain_ctrl"].as<int>() == NULL)   
          gain_ctrl = 1;
        else
          gain_ctrl = obj["gain_ctrl"].as<int>();
        
        if (obj["agc_gain"].as<int>() == NULL) 
          agc_gain = 0;
        else
          agc_gain = obj["agc_gain"].as<int>();
        
        if (obj["gainceiling"].as<int>() == NULL) 
          gainceiling = 0;
        else
          gainceiling = obj["gainceiling"].as<int>();
        
        if (obj["bpc"].as<int>() == NULL) 
          bpc = 0;
        else
          bpc = obj["bpc"].as<int>();
        
        if (obj["wpc"].as<int>() == NULL) 
          wpc = 1;
        else
          wpc = obj["wpc"].as<int>();
        
        if (obj["raw_gma"].as<int>() == NULL) 
          raw_gma = 1;
        else
          raw_gma = obj["raw_gma"].as<int>();
        
        if (obj["lenc"].as<int>() == NULL) 
          lenc = 1;
        else
          lenc = obj["lenc"].as<int>();
        
        if (obj["hmirror"].as<int>() == NULL) 
          hmirror = 0;
        else
          hmirror = obj["hmirror"].as<int>();
        
        if (obj["vflip"].as<int>() == NULL) 
          vflip = 0;
        else
          vflip = obj["vflip"].as<int>();
        
        if (obj["dcw"].as<int>() == NULL) 
          dcw = 1;
        else
          dcw = obj["dcw"].as<int>();
        
        if (obj["colorbar"].as<int>() == NULL) 
          colorbar = 0;
        else
          colorbar = obj["colorbar"].as<int>();  

        if (obj["ssid"].as<String>() == "null")
          ssid = "TimeLapse_Camara";
        else
          ssid = obj["ssid"].as<String>();
          
        if (obj["password"].as<String>() == "null")
          password = "TimeLapse2021";  
        else
          password = obj["password"].as<String>();
    }
    
    sensor_t * s = esp_camera_sensor_get();
  
    s->set_brightness(s, brightness);
    s->set_contrast(s, contrast);
    s->set_saturation(s, saturation);
    s->set_special_effect(s, special_effect);
    s->set_whitebal(s, whitebal);
    s->set_awb_gain(s, awb_gain);
    s->set_wb_mode(s, wb_mode);
    s->set_exposure_ctrl(s, exposure_ctrl);
    s->set_aec2(s, aec2);
    s->set_ae_level(s, ae_level);
    s->set_aec_value(s, aec_value);
    s->set_gain_ctrl(s, gain_ctrl);
    s->set_agc_gain(s, agc_gain);
    s->set_gainceiling(s, (gainceiling_t)gainceiling);
    s->set_bpc(s, bpc);
    s->set_wpc(s, wpc);
    s->set_raw_gma(s, raw_gma);
    s->set_lenc(s, lenc);
    s->set_hmirror(s, hmirror);
    s->set_vflip(s, vflip);
    s->set_dcw(s, dcw);
    s->set_colorbar(s, colorbar);
  }
  catch(const std::exception& ex) 
  {
    Serial.println("FAIL ON: GetConfig");
  }
}

// Funcion de sistema: Atiende las solicitudes de los usuarios WEB y ejecuta la funcion de limpiado de SD y EEPROM
void WiFi_Clients()
{
  WiFiClient client = server.available();
  String currentLine = "";

  if (client) 
  {
    while (client.connected()) 
    {
      if (client.available()) 
      {
        char c = client.read();
        Serial.write(c);
        IPAddress dataIp = WiFi.softAPIP();  
        
        if (c == '\n') 
        {
          if (currentLine.length() == 0) 
          {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();
            client.print("The Camara is active");
            client.print("<br>");
            client.print("Frames on SD: ");
            client.print(String(CountImages(SD_MMC.open("/"),0)));
            client.print("<br>");
            client.print("Lapse between frames: ");
            client.print(String(TIME_TO_SLEEP));
            client.print(" secons");
            client.print("<br>");
            client.print("Click <a href=\"ftp://" + ssid + ":" + password + "@" + String(dataIp[0]) + "." + String(dataIp[1]) + "." + String(dataIp[2]) + "." + String(dataIp[3]) + "\">here</a> to show files");
            client.print("<br>");
            client.print("Click <a href=\"/H\">here</a> to clean SD.<br>");
            client.println();
            break;
          } 
          else 
          {
            currentLine = "";
          }
        } 
        else if (c != '\r') 
        {
          currentLine += c;
        }

        if (currentLine.endsWith("GET /H")) 
        {
            deleteImages(SD_MMC.open("/"),0);
            CleanEEPROM();
    
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();
            client.print("The Camara is active");
            client.print("<br>");
            client.print("Frames on SD: ");
            client.print(String(CountImages(SD_MMC.open("/"),0)));
            client.print("<br>");
            client.print("Lapse between frames: ");
            client.print(String(TIME_TO_SLEEP));
            client.print(" secons");
            client.print("<br>");
            client.print("Click <a href=\"ftp://" + ssid + ":" + password + "@" + String(dataIp[0]) + "." + String(dataIp[1]) + "." + String(dataIp[2]) + "." + String(dataIp[3]) + "\">here</a> to show files");
            client.print("<br>");
            client.print("SD Cleaned<br>");
            client.println();

            break;
        }
      }
    }
    client.stop();
  }
}

void setup() 
{
  // Evita que la camara no inicie por caida de tension en la alimentación
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Inicio el serial, en si esto no se usa mas que para debug
  Serial.begin(57600);
  Serial.println();
  Serial.println("Booting...");

  // Configuro los pines de la camara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Configuro la resolucion de cuadro
  if(psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else 
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Inicio la camara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) 
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Inicio la EEPROM
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("Failed to initialise EEPROM"); 
    Serial.println("Exiting now"); 
    while(1);
  }

  // Inicio la SD
  if(SD_MMC.begin("/sdcard",false) == true)
  {
    if(SD_MMC.cardType() == CARD_NONE)
    {
      Serial.println("No SD card attached");
      return;
    }  
  }
  else
  {
    Serial.println("Card Mount Failed");

    CleanEEPROM();
  
    return;
  }

  // Obtengo la configuracion de la SD
  GetConfig(SD_MMC, "/config.json");

  // Inicio el AP WiFi
  WiFi.softAP(string2char(ssid), string2char(password));
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Inicio el servidor FTP
  ftpSrv.begin(ssid, password);

  // Inicio el servidor WEB
  server.begin();
}

void loop() 
{
  // Si el contaodor esta en 0 tomo un cuadro
  if (Contador == 0)
  {
    if(EEPROM.read(ID_ADDRESS) != ID_BYTE)
    {
      Serial.println("Initializing ID byte & restarting picture count");
      nextImageNumber = 0;
      EEPROM.write(ID_ADDRESS, ID_BYTE);  
      EEPROM.commit(); 
    }
    else
    {
      EEPROM.get(COUNT_ADDRESS, nextImageNumber);
      nextImageNumber +=  1;    
    }
  
    camera_fb_t * fb = NULL;
  
    fb = esp_camera_fb_get();
    if (!fb) 
    {
      Serial.println("Camera capture failed");
      Serial.println("Exiting now"); 
      while(1);
    }
    
    String path = "/IMG" + String(nextImageNumber) + ".jpg";
      
    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if(!file)
    {
      Serial.println("Failed to create file");
      Serial.println("Exiting now"); 
      while(1);   //wait here as something is not right    
    } 
    else 
    {
      file.write(fb->buf, fb->len); 
      EEPROM.put(COUNT_ADDRESS, nextImageNumber);
      EEPROM.commit();
    }
    file.close();

    esp_camera_fb_return(fb);
    Serial.printf("Image saved: %s\n", path.c_str());
  }

  delay(10);
  Contador++;

  // Atiendo solicitudes WEB y FTP
  ftpSrv.handleFTP();
  WiFi_Clients();

  // Si llego a la cantidad de segundos asignado pongo el contador en 0
  if (Contador > TIME_TO_SLEEP * 100)
    Contador = 0;
}
