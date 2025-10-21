/*
BSD 3-Clause License

Copyright (c) 2025, Thomas Grunenberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "BMI088.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// For Device ID
#include <EEPROM.h>

// For Time/Date
#include <TimeLib.h>

/*
Size of Pre recorded Data-Sets (must be smaller than 255)
*/
#define PREDATALOG_SIZE 100

/*
Size of Recorded Data-Sets (do not use too much memory....)
*/
#define DATALOG_SIZE 1800


/*
Threshold value for autostart (total of x+y+z accalaration)
*/
#define AUTOSTART_VALUE 25

/* accel object */
Bmi088Accel accel(Wire,0x19);
/* gyro object */
Bmi088Gyro gyro(Wire,0x69);



// WLAN Zugangsdaten. Bitte hier ein ordentliches Passwort auswählen.
const char *ssid = "gsensor";
const char *password = "1234567890";
const char* hostname = "esp";


ESP8266WebServer server(80);

/**********************************************************/
// Global Memory 
/**********************************************************/
bool printmode = false;
unsigned char deviceID;


/**********************************************************/
// Global Memory for data logging
/**********************************************************/
struct logdata_t {
	signed short x;
	signed short y;
  signed short z;
};

logdata_t* accprelog = NULL;
logdata_t* gyroprelog = NULL;

logdata_t* acclog = NULL;
logdata_t* gyrolog = NULL;

volatile int16_t logpos = -1;
volatile int8_t prelogpos = 0;

volatile bool autostart = true;

// Recoring starting time
int recordstart_year;
int recordstart_month;
int recordstart_day;
int recordstart_hour;
int recordstart_minute;
int recordstart_second;




//--------------------------------------------------------------------------------------------------------------------------------
// ISR
// Interrupt runs 100 times per second (100Hz)
void ICACHE_RAM_ATTR onTimer() {
 
  // Update sensor data ..................................................................
 
  // read the accel
  accel.readSensor();

  // read the gyro
  gyro.readSensor();

  // Data logging ........................................................................
  
  // Start log on event if autostart is enabled
  if ( (logpos < 0) && (abs(accel.getAccelX_mss()) + abs(accel.getAccelY_mss()) + abs(accel.getAccelZ_mss()) > AUTOSTART_VALUE) && autostart ){
    logpos = 0;
    recordstart_year = year();
    recordstart_month = month();
    recordstart_day = day();
    recordstart_hour = hour();
    recordstart_minute = minute();
    recordstart_second = second();
  }
  
  // Log data until the memory is full
  if ( (logpos > -1) && (logpos < DATALOG_SIZE) ){
    acclog[logpos].x = accel.getAccelX_mss() * 100;
    acclog[logpos].y = accel.getAccelY_mss() * 100; 
    acclog[logpos].z = accel.getAccelZ_mss() * 100;
    gyrolog[logpos].x = gyro.getGyroX_rads() * 100;
    gyrolog[logpos].y = gyro.getGyroY_rads() * 100;
    gyrolog[logpos].z = gyro.getGyroZ_rads() * 100;

    logpos++;
  }

  // Before logging save pre data
  if ( logpos < 0 ){
    accprelog[prelogpos].x = accel.getAccelX_mss() * 100;
    accprelog[prelogpos].y = accel.getAccelY_mss() * 100;
    accprelog[prelogpos].z = accel.getAccelZ_mss() * 100;
    gyroprelog[prelogpos].x = gyro.getGyroX_rads() * 100;
    gyroprelog[prelogpos].y = gyro.getGyroY_rads() * 100;
    gyroprelog[prelogpos].z = gyro.getGyroZ_rads() * 100;
    if (++prelogpos >= PREDATALOG_SIZE)
      prelogpos = 0;
  }
}
//--------------------------------------------------------------------------------------------------------------------------------




void handleRoot() {
  String html = "<html><body>";
  html += "<form id=\"timeForm\" action=\"/settime\" method=\"GET\">";
  html += "Stunde: <input type=\"number\" id=\"hour\" name=\"hour\" min=\"0\" max=\"23\"><br>";
  html += "Minute: <input type=\"number\" id=\"minute\" name=\"minute\" min=\"0\" max=\"59\"><br>";
  html += "Sekunde: <input type=\"number\" id=\"second\" name=\"second\" min=\"0\" max=\"59\"><br>";
  html += "Tag: <input type=\"number\" id=\"day\" name=\"day\" min=\"1\" max=\"31\"><br>";
  html += "Monat: <input type=\"number\" id=\"month\" name=\"month\" min=\"1\" max=\"12\"><br>";
  html += "Jahr: <input type=\"number\" id=\"year\" name=\"year\" min=\"2020\" max=\"2100\"><br>";
  html += "<input type=\"submit\" value=\"Set Time\">";
  html += "</form>";

  // JavaScript zur automatischen Uhrzeiteingabe
  html += "<script>";
  html += "const now = new Date();";
  html += "document.getElementById('hour').value = now.getHours();";
  html += "document.getElementById('minute').value = now.getMinutes();";
  html += "document.getElementById('second').value = now.getSeconds();";
  html += "document.getElementById('day').value = now.getDate();";
  html += "document.getElementById('month').value = now.getMonth() + 1;";
  html += "document.getElementById('year').value = now.getFullYear();";
  html += "document.getElementById('timeForm').submit();";
  html += "</script>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}


void handleSetTime() {
  if (server.hasArg("hour") && server.hasArg("minute") && server.hasArg("second") &&
      server.hasArg("day") && server.hasArg("month") && server.hasArg("year")) {
    
    int currentHour = server.arg("hour").toInt();
    int currentMinute = server.arg("minute").toInt();
    int currentSecond = server.arg("second").toInt();
    int currentDay = server.arg("day").toInt();
    int currentMonth = server.arg("month").toInt();
    int currentYear = server.arg("year").toInt();


    // Uhrzeit setzen
    setTime(currentHour, currentMinute, currentSecond, currentDay, currentMonth, currentYear);
    Serial.printf("Timeset to: %02d:%02d:%02d am %02d.%02d.%d\n", 
                  currentHour, currentMinute, currentSecond, 
                  currentDay, currentMonth, currentYear);
  }
  
  String answer = "<html>\n";
  answer += "<head>\n";
  answer += "<meta http-equiv=\"refresh\" content=\"1; url=/ui\">\n";
  answer += "</head></body>\n";
  answer += "<h3>Zeit gesetzt</h3>";
  answer += "</body></html>\n"; 

  server.send(200, "text/html", answer);
}

void handleUI(){
  String answer = "<html>\n";
  answer += "<head>\n";
  answer += "<title>MPU 6050 Beschleunigungssensor</title>\n";
  answer += "<meta http-equiv=\"refresh\" content=\"2\" >\n";
  
  if (logpos < 0){
    if (autostart){
      answer += "</head><body bgcolor=green><h2>Data logger ready</h2>\n";
      answer += "<a href=\"/autostartoff\"><button>Autostart deaktivieren</button></a>\n";
    } else {
      answer += "</head><body bgcolor=white><h2>Data logger stopped</h2>\n";  
      answer += "<a href=\"/autostarton\"><button>Autostart aktivieren</button></a>\n";
    }

  } else {
    if (logpos < DATALOG_SIZE){
      answer += "</head><body bgcolor=red><h2>Recording data</h2>\n";
      answer +=  "<progress value=\"" + String(logpos) + "\" max=\"" + String(DATALOG_SIZE) + "\"></progress>";
    } else {
      answer += "</head><body bgcolor=blue><h2>Data recorded</h2>\n";
      answer += "<p>";
      answer += "<a href=\"/download\" download><button>Download data</button></a>\n";
      answer += "<a href=\"/reset\" onclick=\"return confirm('Are you sure?');\"><button>Reset data</button></a>\n";
      answer += "</p><p>";
    }
  }

  answer += "<h2>Beschleunigungen:</h2>\n";
  answer += "<h3>X-Achse: " + String(accel.getAccelX_mss()) + "</h3>\n";
  answer += "<h3>Y-Achse: " + String(accel.getAccelY_mss()) + "</h3>\n";   
  answer += "<h3>Z-Achse: " + String(accel.getAccelZ_mss()) + "</h3>\n";
  answer += "<h2>Gyroskop:</h2>\n";
  answer += "<h3>X-Achse: " + String(gyro.getGyroX_rads()) + "</h3>\n";
  answer += "<h3>Y-Achse: " + String(gyro.getGyroY_rads()) + "</h3>\n";   
  answer += "<h3>Z-Achse: " + String(gyro.getGyroZ_rads()) + "</h3>\n"; 
  answer += "<h3>Temperatur: " + String(accel.getTemperature_C()) + "&deg;C</h3>\n"; 
  answer += "<br/>";
  answer += "<h1>Aktuelle Uhrzeit:</h1>";
  answer += "<p><h2>" + String(hour()) + ":" + String(minute()) + ":" + String(second()) + "</h2></p>";
  answer += "<a href=\"/\" Set Time><button>Set Time</button></a>\n"; 
  answer += "<h1>Device ID:</h1>";
  answer += "<p><h2>" + String(deviceID) + "</h2></p>";
  answer += "</p></body>";

  server.send(200, "text/html", answer);
}


void resetlogdata(){

  // Reset data logging
  logpos = -1;
  autostart = false;

  // Create Answer
  String answer = "<html>\n";
  answer += "<head>\n";
  answer += "<title>Reset log data</title>\n";
  answer += "<meta http-equiv=\"refresh\" content=\"2; url=/ui\">\n";
  answer += "</head><body>\n";
  answer += "<h1>Reset log data</h1>\n";
  answer += "</body>"; 

  server.send(200, "text/html", answer);
}


void downloadlogdata(){
  uint16_t i;

  // Prevent execution when datalog is not complete
  if (logpos != DATALOG_SIZE)
    return;

  // Stop Sensor and Datalog Interrupt
   timer1_disable();
  
  // Creating filename
  String filename = "gSensor";
  char tmp[10];
  snprintf(tmp, 8, "%03d", deviceID);
  filename += tmp;
  filename += "_";
  snprintf(tmp, 4, "%04d", recordstart_year);
  filename += tmp;
  snprintf(tmp, 4, "%02d", recordstart_month);
  filename += tmp;
  snprintf(tmp, 4, "%02d", recordstart_day);
  filename += tmp;
  filename += "_"; 
  snprintf(tmp, 4, "%02d", recordstart_hour);
  filename += tmp;
  snprintf(tmp, 4, "%02d", recordstart_minute);
  filename += tmp;
  snprintf(tmp, 4, "%02d", recordstart_second);
  filename += tmp; 

  String webPageFirstLine = "Index;AccX;AccY;AccZ;GyroX;GyroY;GyroZ\n";
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=" + filename + ".txt");
  server.send(200, "text/plain", webPageFirstLine);
  
  // Pre Recorded data
  for (i = 0; i < PREDATALOG_SIZE; i++) {
    String csvline = String(i - PREDATALOG_SIZE);
    csvline += ";";
    csvline += String(accprelog[((i + prelogpos) % PREDATALOG_SIZE)].x/100.0);
    csvline += ";";
    csvline += String(accprelog[((i + prelogpos) % PREDATALOG_SIZE)].y/100.0);
    csvline += ";";
    csvline += String(accprelog[((i + prelogpos) % PREDATALOG_SIZE)].z/100.0);
    csvline += ";";
    csvline += String(gyroprelog[((i + prelogpos) % PREDATALOG_SIZE)].x/100.0);
    csvline += ";";
    csvline += String(gyroprelog[((i + prelogpos) % PREDATALOG_SIZE)].y/100.0);
    csvline += ";";
    csvline += String(gyroprelog[((i + prelogpos) % PREDATALOG_SIZE)].z/100.0);    
    csvline += "\n";
    server.sendContent(csvline);
  }

  // Recorded data
  for (i = 0; i < DATALOG_SIZE; i++) {
    
    String csvline = String(i);
    csvline += ";";
    csvline += String((acclog[i].x)/100.0);
    csvline += ";";
    csvline += String((acclog[i].y)/100.0);
    csvline += ";";
    csvline += String((acclog[i].z)/100.0);
    csvline += ";";
    csvline += String((gyrolog[i].x)/100.0);
    csvline += ";";
    csvline += String((gyrolog[i].y)/100.0);
    csvline += ";";
    csvline += String((gyrolog[i].z)/100.0);
    csvline += "\n";
    server.sendContent(csvline);
  }

  // End of Data
  server.sendContent("");


  // Re-Enable Timer1 interrupt
  timer1_isr_init();
  timer1_attachInterrupt(onTimer);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(50000); // Timer auf 10ms setzen
}


void autostarton(){

  // Reset data logging
  autostart = true;

  // Create Answer
  String answer = "<html>\n";
  answer += "<head>\n";
  answer += "<title>Autostart</title>\n";
  answer += "<meta http-equiv=\"refresh\" content=\"2; url=/ui\">\n";
  answer += "</head><body>\n";
  answer += "<h1>Autostart aktiv</h1>\n";
  answer += "</body>"; 

  server.send(200, "text/html", answer);
}

void autostartoff(){

  // Reset data logging
  autostart = false;

  // Create Answer
  String answer = "<html>\n";
  answer += "<head>\n";
  answer += "<title>Autostart</title>\n";
  answer += "<meta http-equiv=\"refresh\" content=\"2; url=/ui\">\n";
  answer += "</head><body>\n";
  answer += "<h1>Autostart deaktiviert</h1>\n";
  answer += "</body>"; 

  server.send(200, "text/html", answer);
}


void setup(void) {
  int status;

  // Onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH

  // Serial output
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // will pause Zero, Leonardo, etc until serial console opens
  }

  // Malloc memory for data logging
  accprelog = new logdata_t[PREDATALOG_SIZE];
  gyroprelog = new logdata_t[PREDATALOG_SIZE];

  acclog = new logdata_t[DATALOG_SIZE];
  gyrolog = new logdata_t[DATALOG_SIZE];

  if ( (accprelog == NULL) || (acclog == NULL)){
    Serial.println("Memoy alloc failed!");
    while(1){
      digitalWrite(LED_BUILTIN, LOW);
      delay(300);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(300);   
    }
  }

  // Device ID
  EEPROM.begin(1);
  deviceID = EEPROM.read(0);

  // Try to initialize!
  accel.setRange(Bmi088Accel::RANGE_24G);
  accel.setOdr(Bmi088Accel::ODR_1600HZ_BW_234HZ);
  status = accel.begin();

  if (status < 0) {
    Serial.println("Accel Initialization Error");
    Serial.println(status);
    while (1) { // Fast flash blue LED
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
    }
  }
  gyro.setOdr(Bmi088Gyro::ODR_2000HZ_BW_230HZ);
  gyro.setRange(Bmi088Gyro::RANGE_1000DPS);  
  status = gyro.begin();
  if (status < 0) {
    Serial.println("Gyro Initialization Error");
    Serial.println(status);
    while (1) { // Fast flash blue LED
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
    }
  }

  Serial.println("Starte Access Point...");
  WiFi.setOutputPower(10); // reduce transmit power to 10mW
  
  // Adapt WiFi-SSID to device ID
  String myssid = ssid;
  myssid += String((int)deviceID);
  WiFi.softAP(myssid.c_str(), password);

  // IP Adresse anzeigen (sollte 192.168.4.1 sein)
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Server einrichten und starten. Es wird hier nur die
  // Seite "/" (Hauptseite) angelegt. Die Funktion handleRoot
  // ist oben angelegt. So etwas nennt sich Callback Funktion.
  server.on("/", handleRoot);

  server.on("/reset", resetlogdata);
  server.on("/download", downloadlogdata);
  server.on("/autostarton", autostarton);
  server.on("/autostartoff", autostartoff);

  server.on("/ui", handleUI);
  server.on("/settime", handleSetTime);


  server.begin();
  Serial.println("HTTP server started");

  // Start mDNS responder
  if (MDNS.begin(hostname)) {
    Serial.print("mDNS responder started with hostname: ");
    Serial.println(hostname);
    MDNS.addService("http", "tcp", 80); // Add a service (optional)
  }

  Serial.println("");


  // Short flash the blue LED
  digitalWrite(LED_BUILTIN, LOW);
  delay(600);
  digitalWrite(LED_BUILTIN, HIGH);

  // Timer initialisieren
  timer1_isr_init();
  timer1_attachInterrupt(onTimer);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(50000); // Timer auf 10ms setzen
}


void loop() {

  MDNS.update();
  server.handleClient();  // Web server access

  // Show with blue onboard LED that logger is ready
  if (logpos < 0 && autostart)
    digitalWrite(LED_BUILTIN, LOW);
  else
    digitalWrite(LED_BUILTIN, HIGH);


  // Print out the values
  if (printmode){
    Serial.print(accel.getAccelX_mss());
    Serial.print("\t");
    Serial.print(accel.getAccelY_mss());
    Serial.print("\t");
    Serial.print(accel.getAccelZ_mss());
    Serial.print("\t");
    Serial.print(gyro.getGyroX_rads());
    Serial.print("\t");
    Serial.print(gyro.getGyroY_rads());
    Serial.print("\t");
    Serial.print(gyro.getGyroZ_rads());
    Serial.print("\t");
    Serial.print(accel.getTemperature_C());
    Serial.print("°C,"); 
    Serial.print("Logpos:");
    Serial.print(logpos);
    Serial.println("");
  }


  // Process serial commands
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    // Check if command is a number
    bool isNumber = true;
    for (int i = 0; i < command.length(); i++) {
      if (!isDigit(command.charAt(i))) {
        isNumber = false; // Zeichen ist keine Ziffer
        break;
      }
    }
    if (isNumber)
      Serial.print("Got number: ");
    else
      Serial.print("Got command: ");
    Serial.println(command);

    // Ganzzahl => Device ID 
    if (isNumber) {
      int num = command.toInt();
      if (num > 0 && num < 255){
      deviceID = num;
      Serial.print("Set device ID to: ");
      Serial.println(deviceID);
      EEPROM.write(0, deviceID);
      EEPROM.commit();
      }
    }

    // Printmode
    if (!isNumber && command.equalsIgnoreCase("print") ){
      printmode = !printmode;
    }

     // Info
    if (!isNumber && command.equalsIgnoreCase("info") ){
      Serial.print("Device ID: ");
      Serial.println(deviceID); 
      Serial.printf("Time: %02d:%02d:%02d am %02d.%02d.%d\n", 
                  hour(), minute(), second(), day(), month(), year());
      Serial.print("Free RAM: ");
      Serial.print( ESP.getMaxFreeBlockSize() );
      Serial.println(" bytes");

      Serial.print("accprelog: ");
      Serial.println((int)accprelog,HEX);
      Serial.print("gyroprelog: ");
      Serial.println((int)gyroprelog,HEX);
      Serial.print("acclog: ");
      Serial.println((int)acclog,HEX);
      Serial.print("gyrolog: ");
      Serial.println((int)gyrolog,HEX);

    }
  }
}