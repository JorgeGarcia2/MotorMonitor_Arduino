#include <avr/pgmspace.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>

#define SSpin 5      // Slave Select SD en pin 5
#define hxData 7      // Data HX711 en pin 7
#define hxClock 6     // Clock HX711 en pin 7 
#define eeAddress 0   // Direccion de memoria EEPROM que guarda el factor de escala del HX711

LiquidCrystal_I2C lcdDisplay(0x27, 20, 4); // Instancia de la clase LiquidCrystal_I2C con direccion 0x27 para un display de 16 caracteres y 4 lineas
HX711 loadCell; //Instancia de la clase HX711
RTC_DS3231 rtc;
DateTime now;

bool testState = false;
bool firstSample;
long ti;
long tf;
int timeDelta;
float fuelMassI; // Masa en Kg del combustible al inicio de la prueba
float fuelMassF; // Masa en Kg del combustible al final de la prueba

char response; // Respuesta del usuario 
String fileName; // Nombre del archivo de datos en la memoria SD
char outBuff[100];
char time[9];
char date[11];
bool sdReady = false; // Disponibilidad de gardar los datos en la memoria SD

uint16_t rpm; // Variable de las Revoluciones por minuto
float force, torque, power; // Variables de Fuerza, torque y potencia
float forceT, torqueT, powerT;
uint16_t idSample = 0; //Variable del numero de muestra


// Funcion de configuracion previa
void setup() {
  Serial.begin(9600); // Inicializa la comunicacion serie a 9600 baudios
  lcdDisplay.init(); // Inicializa el lcd
  lcdDisplay.backlight(); // Enciende la luz de fondo del display
  loadCell.begin(hxData, hxClock); // Inicializa el modulo HX711 con sus pines de datos y reloj

  // Comprobamos si tenemos el RTC conectado
  if (!rtc.begin()) {
    Serial.println(F("!! No hay módulo RTC"));
  }
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  sdReady = SD.begin(SSpin); // Inicializa la memoria SD y guarda el estado en sdReady
  if (sdReady) {
    getFileName();
  }
  else{
    fileName = "data.csv";
  }

  // Muestra el mensaje "Bienvenido" en el LCD
  lcdDisplay.setCursor(3, 1);
  lcdDisplay.print(F("Bienvenido"));
  delay(1000);

  setScale(); // Llama a la funcion setScale para que realize la configuracion de la celda de carga 

  // Se solicita las rpm por el monitor serie
  Serial.print(F("\n<< Ingresa las RPM: ")); 
  getRPM();
  Serial.print(F("<< Ingresa la masa del combustible: "));
  getfuel(&fuelMassI); 
  // Muestra en el monitor serie un mensaje para ingresar el numero de revolucioones por minuto
  Serial.print(F(">> Ingresa cualquier tecla para iniciar la prueba"));
}

// Funcion ciclica
void loop() {
  if(Serial.available() > 0){
    response = Serial.read(); // Guarda en response el primer caractar enviado por el usuario
    
    Serial.readString();    // Limpia el buffer
    testState =! testState;

    if (testState == true){
      setSD(); // Llama a la funcion setSD para que realice la configuracion de la memoria SD
      firstSample=true;
      idSample = 0;
      //Serial.println(ti);
      Serial.print(F("\n\n**** Prueba iniciada"));
      //sprintf(time, "%02d:%02d:%02d", now.hour(),now.minute(),now.second());
      //Serial.print(time);
      Serial.print(F("\n>> Ingresa cualquier tecla para detener la prueba\n"));
    }
    else {

      lcdDisplay.clear();
      lcdDisplay.setCursor(7, 1);
      lcdDisplay.print(F("Prueba"));
      lcdDisplay.setCursor(6, 2);
      lcdDisplay.print(F("Detenida"));
      
      timeDelta = now.unixtime()-ti;
      Serial.print(F("\n\n--- Prueba detenida"));
      // sprintf(time, "%02d:%02d:%02d", now.hour(),now.minute(),now.second());
      // Serial.print(time);
      sprintf(outBuff, "\n>> Tiempo cronometrado: %d segundos\n", timeDelta);
      Serial.print(outBuff);
      if(sdReady){
        Serial.print(F("\n<< Ingresa la masa final del combustible: "));
        getfuel(&fuelMassF); 
        saveLastLine();
      }

      Serial.print(F("\n\n>> Ingresa cualquier tecla para continuar la prueba"));
      while(!Serial.available()){ // Espera por una respuesta
        ;
      }
      Serial.readString(); // Limpia el buffer

      // Se solicita las rpm por el monitor serie
      lcdDisplay.clear();
      lcdDisplay.setCursor(6, 1);
      lcdDisplay.print(F("Iniciando"));
      lcdDisplay.setCursor(7, 2);
      lcdDisplay.print(F("Prueba"));
      

      Serial.print(F("\n<< Ingresa las RPM: ")); 
      getRPM();
      Serial.print(F("<< Ingresa la masa inicial del combustible (g): "));
      getfuel(&fuelMassI); 

      Serial.print(F(">> Ingresa cualquier tecla para iniciar la prueba"));
    
      delay(2000);
    }
  }

  if (testState) {
    // Llama a la funcion getData()
    getData(); 
    delay(200); // Espera 200 ms
  }
}

  

// Realiza la lectura y calculo de datos y envia a el LCD y al archivo de datos en caso de que este disponible.
void getData(void) {
  now = rtc.now();
  idSample++;
  sprintf(date, "%02d/%02d/%04d", now.day(),now.month(),now.year());
  sprintf(time, "%02d:%02d:%02d", now.hour(),now.minute(),now.second());
  force = loadCell.get_units(5); // Guarda la lectura obtenida por la celda de carga
  force = (force >= 0)? force : 0.0; // En caso de que la lectura de la celda de carga sea menor a 0, lo guardara como 0
  torque = force * 0.145;  // Guarda el calculo del torque, en esta caso 0.15 es la distancia en metros 
  power = (torque * ((rpm * 6.2831)/60))/745.7; // Guarda el calculo de la potencia 
  if(firstSample){
    ti = now.unixtime();
    firstSample = false;
    forceT=force;
    torqueT=torque;
    powerT=power;
  } 
  else{
    forceT+=force;
    torqueT+=torque;
    powerT+=power;
  }
  printValues(); // Llama a la funcion printValues
  if(sdReady){saveData();} // Si la memoria SD esta disponible, llama a la funcion saveData()
}

// Guarda los valores de id_sample, RPM, Fuera, Torque en N*m , Torque en lb*ft y Potencia en un archivo de datos en la memoria SD 
void saveData() {
  
  File myFile = SD.open(fileName, FILE_WRITE); // Crea un objeto tipo file que apunta a un archivo en la memoria SD
  // Si se crea exitosamente el objeto escribira los datos separandolos por comas
  if (myFile) { 
    sprintf(outBuff,"%s,%s,%d,%d,", date, time, idSample, rpm);
    myFile.print(outBuff);
    myFile.print(force);
    myFile.print(F(", "));
    myFile.print(torque);
    myFile.print(F(", "));
    myFile.print(torque * 0.73756);
    myFile.print(F(", "));
    myFile.print(power);
    sprintf(outBuff,",0,0,0,0,0,0,0,0\n");
    myFile.print(outBuff);
        
    myFile.close();                  
  } 
  // Si no pone el estado de sdReady en false y manda un mensaje de error al monitor serie 
  else {
    sdReady = false;
    Serial.print(F("\n!! Memoria extraida.\n\n"));
  }
}

// Imprime los valores de RPM, Fuera, Torque y Potencia en el LCD
void printValues() {
  lcdDisplay.clear();
  lcdDisplay.setCursor(0,0);
  lcdDisplay.print(F("RPM: "));
  lcdDisplay.print(rpm);
  lcdDisplay.setCursor(17,0);
  lcdDisplay.print(now.unixtime() - ti);
  lcdDisplay.setCursor(0,1);
  lcdDisplay.print(F("Fuerza (N): "));
  lcdDisplay.print(force);
  lcdDisplay.setCursor(0,2);
  lcdDisplay.print(F("Torque (Nm): "));
  lcdDisplay.print(torque);
  lcdDisplay.setCursor(0,3);
  lcdDisplay.print(F("Potencia (Hp): "));
  lcdDisplay.print(power);
}

// Calibra la celda de carga
void setScale(void){
  float knowWeight; // Peso de referencia en Kg
  float knowForce; // Fuerza de referencia en N
  float scaleFactor; // Factor de escala de la calibracion de la celda de carga
  
  // Se ajusta el offset [Sin peso en la celda]
  loadCell.tare();
  
  // Espera a que se conecte el puerto serie
  while(!Serial){
    ;
  }
  
  // Envia un mensaje a el LCD que indique que se esta calibrando la celda
  lcdDisplay.clear();
  lcdDisplay.setCursor(0, 0);
  lcdDisplay.print(F("Calibrando Celda"));

  // Manda mensaje al monitor serie preguntando si se desea realizar la calibracion.
  Serial.println(F("<< ¿Calibrar la celda de carga? (s/n)"));
  while(!Serial.available()){ // Espera por una respuesta
    ;
  }
  response = Serial.read(); //Guarda en response el primer caractar enviado por el usuario
  if(response != '\n') Serial.println(response);
  Serial.readString(); // Limpia el buffer

  // Si la respuesta es si ...
  if(response == 's') {       
    // Manda mensaje al monitor serie pidiendo que se Ingresa el peso de referencia
    Serial.print(F("<< Masa de referencia: "));
    
    while(!Serial.available()){ // Espera por una respuesta
      ;
    }
    
    knowWeight = Serial.parseFloat(SKIP_ALL, '\n'); // Guarda la respuesta en knowWeight
    Serial.print(knowWeight);
    Serial.println(F(" Kg\n"));

    // Manda mensaje al monitor serie pidiendo que se coloque sobre la celda la masa de referencia
    Serial.println(F(">> Colocar masa de referencia en los proximos 10 seg"));
    // Espera 10 segundos
    for(int i = 10; i > 0; i--) {
      delay(1000);
      Serial.println(i);  
    }

    // Calcula la fuera conocida multiplicando la masa por la fuerza de gravedad
    knowForce = knowWeight * 9.8; 
    // Se realiza la calibracion de la escala de la celda
    loadCell.calibrate_scale(knowForce);
    // Se guarda el valor de la escala en scaleFactor 
    scaleFactor = loadCell.get_scale(); 
    // Guarda en la direccion eeAddress de la EEPROM el valor del factor de escala
    EEPROM.put(eeAddress, scaleFactor);
  }
  // Si la respuesta es no, se utiliza el factor escala guardado en la EEPROM
  else {
    Serial.println(F(">> Se usara calibracion previa"));
    EEPROM.get(eeAddress, scaleFactor);
    
    loadCell.set_scale(scaleFactor);
  }

  //Serial.print(F("Factor de escala: "));
  //Serial.println(scaleFactor);
}

// Inicia la memoria SD y guarda la cabecera del archivo de datos
void setSD(void) {
  
  sdReady = SD.begin(SSpin); // Inicializa la memoria SD y guarda el estado en sdReady
  // Comprueba que se inicializo correctamente
  if (sdReady) {
    Serial.print(F("\n\n>> Memoria detectada"));
    
    //getFileName(); // llama a la funcion getFileName
    // Si no existe el archivo datalog.csv, se crea con su cabecera
    if(!SD.exists(fileName)){
      File newFile = SD.open(fileName, FILE_WRITE); // Crea un objeto tipo file que apunta a un archivo en la memoria SD
      // Si el objeto se creo correctamente, escribe en el archivo las cabeceras
      if (newFile) {
        newFile.print(F("Fecha, Hora, ID, RPM, Fuerza (N), Torque (Nm), Torque (lb ft), Potencia (Hp), "));
        newFile.print(F("Tiempo prueba (Seg), Masa combustible consumido (g), Consumo combustible(g/seg), BPO(HP), BPC(HP), BTC(Nm), SFCO(g/HP*h), SFCC(g/HP*h)\n"));
        newFile.close();
      }

      //Si no se crea correctamente, se le asigna a sdReady el estado de false
      else 
        sdReady = false;
    }
    Serial.print(F("\n>> Los datos se guardaran en "));
    Serial.print(fileName);
    Serial.print(F("\n"));
  }

  // Si sdReady tiene el estado de false, se mando un mensaje al monitor serie 
  if (!sdReady) {
    Serial.print(F("\n!! Archivo o memoria no disponible\n\n"));
  }
  delay(1000);
}


void getRPM(void) {
  while(!Serial.available()){ // Espera por una respuesta
    ;
  }
  rpm = Serial.parseInt(SKIP_ALL, '\n'); // Guarda la respuesta en rpm
  Serial.readString(); // Limpia el buffer
  Serial.println(rpm);
}

void getfuel(float *fuelMass) {
  while(!Serial.available()){ // Espera por una respuesta
    ;
  }
  *fuelMass = Serial.parseFloat(SKIP_ALL, '\n'); // Guarda la respuesta en g
  Serial.readString(); // Limpia el buffer
  Serial.print(*fuelMass);
  Serial.print(F(" gramos\n"));
}

void getFileName(void) {
  uint8_t nF = 0; // Numero del archivo 

  // Mientras el valor de filename ya exista, se incrementara el valor de nF
  while(true){
    nF++;
    fileName = "data" + String(nF) + ".csv";
    if(!SD.exists(fileName)){
      break;
    }
  }
}

void saveLastLine(){
  float fuelCons; //Consumo de Combustible
  float bpo;      //Potencia Cbservada
  double bpc;      //Potencia Corregida
  float btc;      // Par Torsional Corregido 
  float sfco;     // Consumo Específico de Combustible
  float sfcc;     // Consumo Específico de Combustible Corregido

  fuelCons = (fuelMassI - fuelMassF)/timeDelta;
  bpo = ((torqueT/idSample)*(rpm))/5252;
  bpc = bpo * pow(1, 0.4); //Revisar cual es el valor de factor atmosferico
  btc = (bpc*5252)/(rpm);
  sfco = fuelCons/bpo;
  sfcc = fuelCons/bpc;

  
  File myFile = SD.open(fileName, FILE_WRITE); // Crea un objeto tipo file que apunta a un archivo en la memoria SD
  // Si se crea exitosamente el objeto escribira los datos separandolos por comas
  if (myFile) { 
    sprintf(outBuff,"%s,%s,%s,%d,", date, time, "LO", rpm);
    myFile.print(outBuff);
    myFile.print(forceT/idSample);
    myFile.print(F(", "));
    myFile.print(torqueT/idSample);
    myFile.print(F(", "));
    myFile.print((torqueT * 0.73756)/idSample);
    myFile.print(F(", "));
    myFile.print(powerT/idSample);
    myFile.print(F(", "));
    myFile.print(timeDelta);
    myFile.print(F(", "));
    myFile.print(fuelMassI - fuelMassF);
    myFile.print(F(", "));
    myFile.print(fuelCons);
    myFile.print(F(", "));
    myFile.print(bpo);
    myFile.print(F(", "));
    myFile.print(bpc);
    myFile.print(F(", "));
    myFile.print(btc);
    myFile.print(F(", "));
    myFile.print(sfco);
    myFile.print(F(", "));
    myFile.println(sfcc);

    myFile.close();                  
  } 
}