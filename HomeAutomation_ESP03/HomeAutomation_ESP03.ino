#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266TelegramBOT.h> //written by Giacarlo Bacchio 

/* Programmering:
    1. Håll knappen inne när du trycker in kontakten i väggen (håll inne till uppladdningen börjat)
    2. Tryck på ladda upp kod...
*/

// Initialize Wifi connection to the router
char ssid[] = "Smaggan";              // your network SSID (name)
char password[] = "hibiskusblomma";             // your network key

//Debug?
#define SerialDebug true

// Initialize Telegram BOT (telephone app bot)
#define BOTtoken "272266714:AAHxKu-xVr1ukNWgyzuUZuDBwZ-E2_AqXuM"  //token of Smaggan 
#define BOTname "Smaggan"
#define BOTusername "Smaggan_bot"

/*
#define BOTtoken "307344844:AAEty3cQPhptxaVvjJHklRx9f_7-ICIwT54"  //token of Smaggan2
#define BOTname "Smaggan2"
#define BOTusername "Smaggan_bot2"
*/
#define ChatID "-198620254" //Se filen Chat ID för hur man får tag på det

TelegramBOT bot(BOTtoken, BOTname, BOTusername); //define an instance of TelegramBOT called "bot"

int TimeBetweenScans = 5000; // time between scan Telegram-messages
long TimeLastScanned = 0;   //last time messages' scan has been done

int FireAlarm_Pin = 13; //GPIO13
int PIR_pin = 14;       //GPIO14

bool FireAlarm = false;
bool ActivatePIR = false;

//Time keepers in interrupt to keep track of how long there is sound/silense (volatile = importante!)
volatile unsigned long ONstart=0;
volatile unsigned long OFFstart=0;
volatile unsigned long FireAlarmONtime;
volatile unsigned long FireAlarmOFFtime;
volatile unsigned long FireAlarmFreqON;
volatile unsigned long FireAlarmFreqOFF;

//Variables to keep track of fire alarm on/off and if we are recording
unsigned long StartFireLogg=0;
bool FireAlarmIsRecording=false;
int FireAlarmCounter;
int FireFalseAlarmCounter;

volatile bool LastPirState;

void FireAlarm_ISR(); //initialize function

//Setp function is run only once when unit boots up!
void setup() {
  if(SerialDebug){
    Serial.begin(115200);
    delay(10);
    
    // attempt to connect to Wifi network:
    Serial.print("Connecting Wifi: ");
    Serial.println(ssid);
  }
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if(SerialDebug) Serial.print(".");
  }
  if(SerialDebug){
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
  }
  IPAddress ip = WiFi.localIP();
  if(SerialDebug) Serial.println(ip);


  bot.begin();      // launch Bot functionalities
  bot.sendMessage(ChatID, "Ansluten! (inaktiv)", ""); //send text to app 
  
  pinMode(PIR_pin, INPUT); //define pin as input
  pinMode(FireAlarm_Pin, INPUT_PULLUP);
  
}

//Custom function that loops through all new messaes from the app and returns true if the text is found
bool FindMessage(String text){
  //Serial.println(bot.message[0][0].toInt()-1);
  //bot.message[0][0] contains nr of received messages
  for(int i=bot.message[0][0].toInt()-1;i>0;i--){ //skip message[0] which is just status...
    
    if(sizeof(bot.message[i][5])>0){
      //Serial.print(i);
      //Serial.print(": ");
      //Serial.println(bot.message[i][5]);
      String CurrentMessage = bot.message[i][5];
      CurrentMessage.toUpperCase(); //Convert to upper case
      text.toUpperCase(); //Convert to upper case
      if(CurrentMessage.indexOf(text)>-1){ 
        return true; //do not look further up (might have sent first ON then OFF)
      }
    }
  }
  return false;
}

//Main loop
void loop() {
  //check if it is time to connect to Telegram chat to see if there is any new messages waiting to be read
  if (millis() > TimeLastScanned + TimeBetweenScans)  {
    bot.getAllUpdates_Kalle(true);   // launches modified version of GetUpdates and markes them as read 
    //Serial.println("Connecting to Telegram");
    //Serial.print(".");
    if(bot.message[0][0].toInt()>0){
      if(SerialDebug) Serial.println();
      if(FindMessage("LarmON")==true){
        if(SerialDebug) Serial.print("Rorelsedetektor aktiverad!");
        ActivatePIR=true;
        LastPirState = !digitalRead(PIR_pin);
        bot.sendMessage(ChatID, "Rörelsedetektor aktiverad!", "");
      }
      else if(FindMessage("LarmOFF")==true){
        if(SerialDebug) Serial.println("Rorelsedetektor Inaktiverad!");
        ActivatePIR=false;
        bot.sendMessage(ChatID, "Rörelsedetektor Inaktiverad!", "");
      }
      else if(FindMessage("BrandON")==true){
        if(SerialDebug) Serial.println("Brandlarm Aktiverat!");
        attachInterrupt(FireAlarm_Pin, FireAlarm_ISR, CHANGE);//
        FireAlarm=true;

        bot.sendMessage(ChatID, "Brandlarm Aktiverat!", "");
      }
      else if(FindMessage("BrandOFF")==true){
        if(SerialDebug) Serial.println("Brandlarm Inaktiverad!");
        detachInterrupt(FireAlarm_Pin);
        FireAlarm=false;
        bot.sendMessage(ChatID, "Brandlarm Inaktiverad!", "");
      }
      else if(FindMessage("Status")==true){
        if(SerialDebug) Serial.println("Status");
        
        String SendText = "Status:%0A"; //%0A is new line in html-string!
        
        if(FireAlarm){
          SendText+="Brandlarm: Aktiverat%0A";
        }else{
          SendText+="Brandlarm: Inaktiv%0A";
        }
        if(ActivatePIR){
          SendText+="Rörelsevakt: Aktiverad";
        }else{
          SendText+="Rörelsevakt: Inaktiv";
        }
        bot.sendMessage(ChatID, SendText, "");
      }
      
      if(SerialDebug) Serial.println();
    }
    TimeLastScanned = millis();
    
    StartFireLogg=millis(); //Reset fire alarm counter
    FireAlarmONtime=0; //Restart loggers
    FireAlarmOFFtime=0;
  }

  //Check if PIR-sensor is detecting movemet if activated
  if(ActivatePIR==true){
    if(LastPirState!=digitalRead(PIR_pin)){ //If changed since last loop cycle
      if(SerialDebug) Serial.println(digitalRead(PIR_pin));
      if(digitalRead(PIR_pin)) delay(1); //If High, delay 1 ms and see if it's still high (not only missreading)
      if(digitalRead(PIR_pin)){
        if(SerialDebug) Serial.println();
        if(SerialDebug) Serial.println("Tjuvar!");
        bot.sendMessage(ChatID, "Rörelse detekterad!", "");
        //delay(500);
        StartFireLogg=millis(); //Reset fire alarm counter
        FireAlarmONtime=0; //Restart logger
        FireAlarmOFFtime=0;
      }
      LastPirState = digitalRead(PIR_pin);
    }
    
    
  }
  
  //FireAlarm
  if(FireAlarm==true){
    
    if (millis()>StartFireLogg+100) { //Every 100ms
      //See interrupt function "FireAlarm_ISR" that loggs the on/off-times of the microphone)
      float a = FireAlarmONtime/1000; //a is number of ms that has been on during the last 100 ms => % on
      float b = FireAlarmOFFtime/1000;
      int ProcON = int(100*a/(a+b)); //convert the float to int to skip decimals 
      if(SerialDebug){ //b=0 är felvärde
        Serial.println();
        Serial.print(int(a));
        Serial.print(" : ");
        Serial.print(int(b));
        Serial.print(" = ");
        Serial.print(ProcON);
        Serial.print("%");
      }

      if(ProcON>5){ //Threshold (if no sound is detected => skip freq analysation)
        float freq = 1.0/((FireAlarmFreqON+FireAlarmFreqOFF)*0.000001); //Calculate frequancy of the current us-readings on and off time
        if(SerialDebug) {
          Serial.println();
          Serial.print("Freq:");
          Serial.print(freq);
        }
        if(freq > 3000){ //Only logg sound above 3kHz my alarme is around 3.1kHz
          FireAlarmIsRecording=true; //Start recording sounds
          FireAlarmCounter++;
        }
      }else if(FireAlarmIsRecording){ //If under threshold while recording (the alarm is pulsing...)
        FireFalseAlarmCounter++; //No sound this time
      }
      
      if(FireAlarmIsRecording){ //When recording: 
        if(FireAlarmCounter>10) //Do we have a registrated sound more than 10 times out of 20 (false alarm >10 bellow)
        {
          if(SerialDebug) Serial.println();
          if(SerialDebug) Serial.println("FIRE!");
          bot.sendMessage(ChatID, "Brandlarm tjuter!", "");
          
          FireAlarmIsRecording=false;
          FireAlarmCounter=0;
          FireFalseAlarmCounter=0;
        }
        else if(FireFalseAlarmCounter>10) //If we have a false alarm more than 10 times after recording start: false alarm!
        {
          FireAlarmIsRecording=false;
          FireAlarmCounter=0;
          FireFalseAlarmCounter=0;
        }
      }
      
      //Serial.println(" OFF");
      StartFireLogg=millis();
      FireAlarmONtime=0; //Restart logger
      FireAlarmOFFtime=0;
    }

    //delay(500);
  }
  
  yield();
}

//Interupt routine:
void FireAlarm_ISR(){
  if(digitalRead(FireAlarm_Pin)) //Just turned high => alarmpin has been low
  {
    FireAlarmOFFtime+=micros()-OFFstart; //Add all the Off-times we have during 100ms (100ms is time between readings in main loop)
    FireAlarmFreqOFF=micros()-OFFstart; //This one only keeps track of the current frequenzy (not logging)
    ONstart=micros();
  }
  else{ 
    FireAlarmONtime+=micros()-ONstart;
    FireAlarmFreqON=micros()-ONstart;
    OFFstart=micros();
    
  }
  
}

