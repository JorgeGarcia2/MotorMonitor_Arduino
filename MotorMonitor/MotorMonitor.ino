#include <avr/pgmspace.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

#define SSpin 10      // Slave Select SD en pin 10
#define hxData 7      // Data HX711 en pin 7
#define hxClock 6     // Clock HX711 en pin 7 
#define eeAddress 0   // Direccion de memoria EEPROM que guarda el factor de escala del HX711


LiquidCrystal_I2C lcdDisplay(0x27, 20, 4); // Instancia de la clase LiquidCrystal_I2C con direccion 0x27 para un display de 16 caracteres y 4 lineas
HX711 loadCell; //Instancia de la clase HX711


String fileName; // Nombre del archivo de datos en la memoria SD
bool sdReady = false; // Disponibilidad de gardar los datos en la memoria SD

uint16_t rpm; // Variable de las Revoluciones por minuto
float force, torque, power; // Variables de Fuerza, torque y potencia
uint16_t id_sample = 0; //Variable del numero de muestra

// Funcion de configuracion previa
void setup() {
  Serial.begin(9600); // Inicializa la comunicacion serie a 9600 baudios
  lcdDisplay.init(); // Inicializa el lcd
  lcdDisplay.backlight(); // Enciende la luz de fondo del display
  loadCell.begin(hxData, hxClock); // Inicializa el modulo HX711 con sus pines de datos y reloj

  // Muestra el mensaje "Bienvenido" en el LCD
  lcdDisplay.setCursor(3, 1);
  lcdDisplay.print(F("Bienvenido"));
  delay(1000);

  setScale(); // Llama a la funcion setScale para que realize la configuracion de la celda de carga 
  setSD(); // Llama a la funcion setSD para que realice la configuracion de la memoria SD
  
  // Muestra en el monitor serie un mensaje para ingresar el numero de revolucioones por minuto
  Serial.println(F("\nIngrese las RPM: ")); 
}

// Funcion ciclica
void loop() {
  // Llama a la funcion getData()
  getData(); 
  delay(500); // Espera 500 ms
}

// Realiza la lectura y calculo de datos y envia a el LCD y al archivo de datos en caso de que este disponible.
void getData(void) {
  force = loadCell.get_units(5); // Guarda la lectura obtenida por la celda de carga
  force = (force >= 0)? force : 0.0; // En caso de que la lectura de la celda de carga sea menor a 0, lo guardara como 0
  // En caso de que se envie un nuevo valor de las rpm por el monitor serie, se sustituira el valor, en caso contrario se mantendra
  rpm = (Serial.available() > 0)? Serial.parseInt(SKIP_ALL, '\n') : rpm; 
  torque = force * 0.15;  // Guarda el calculo del torque, en esta caso 0.15 es la distancia en metros 
  power = (torque * ((rpm * 6.2831)/60))/745.7; // Guarda el calculo de la potencia 
  printValues(); // Llama a la funcion printValues
  if(sdReady){saveData();} // Si la memoria SD esta disponible, llama a la funcion saveData()
}

// Guarda los valores de id_sample, RPM, Fuera, Torque en N*m , Torque en lb*ft y Potencia en un archivo de datos en la memoria SD 
void saveData()
{
  
  File myFile = SD.open(fileName, FILE_WRITE); // Crea un objeto tipo file que apunta a un archivo en la memoria SD
  // Si se crea exitosamente el objeto escribira los datos separandolos por comas
  if (myFile) { 
    myFile.print(++id_sample);
    myFile.print(F(", "));
    myFile.print(rpm);
    myFile.print(F(", "));
    myFile.print(force);
    myFile.print(F(", "));
    myFile.print(torque);
    myFile.print(F(", "));
    myFile.print(torque * 0.73756);
    myFile.print(F(", "));
    myFile.println(power);
        
    myFile.close();                  
  } 
  // Si no pone el estado de sdReady en false y manda un mensaje de error al monitor serie 
  else {
    sdReady = false;
    Serial.print(F("\nError SD. Memoria extraida.\n\n"));
  }
}

// Imprime los valores de RPM, Fuera, Torque y Potencia en el LCD
void printValues(){
  lcdDisplay.clear();
  lcdDisplay.setCursor(0,0);
  lcdDisplay.print(F("RPM: "));
  lcdDisplay.print(rpm);
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
  
  char response; // Respuesta del usuario 
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
  lcdDisplay.print(F("Calibrando Celda..."));

  // Manda mensaje al monitor serie preguntando si se desea realizar la calibracion.
  Serial.println(F("¿Calibrar la celda de carga? (s/n)"));
  while(!Serial.available()){ // Espera por una respuesta
    ;
  }
  response = Serial.read(); //Guarda en response el primer caractar enviado por el usuario
  Serial.println(response);
  Serial.print("\n");

  // Si la respuesta es si ...
  if(response == 's') {       
    // Manda mensaje al monitor serie pidiendo que se ingrese el peso de referencia
    Serial.println(F("Ingresar masa de referencia (Kg): "));
    Serial.readString(); // Limpia el buffer
    while(!Serial.available()){ // Espera por una respuesta
      ;
    }
    
    knowWeight = Serial.parseFloat(SKIP_ALL, '\n'); // Guarda la respuesta en knowWeight
    Serial.print(F("Masa conocida = "));
    Serial.println(knowWeight);

    // Manda mensaje al monitor serie pidiendo que se coloque sobre la celda la masa de referencia
    Serial.println(F("Colocar masa de referencia"));
    // Espera 5 segundos
    for(int i = 5; i > 0; i--) {
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
    Serial.println(F("Se usara calibracion previa"));
    EEPROM.get(eeAddress, scaleFactor);
    
    loadCell.set_scale(scaleFactor);
  }

  Serial.print(F("Factor de escala: "));
  Serial.println(scaleFactor);
}

// Inicia la memoria SD y guarda la cabecera del archivo de datos
void setSD(void) {
  
  sdReady = SD.begin(SSpin); // Inicializa la memoria SD y guarda el estado en sdReady
  // Comprueba que se inicializo correctamente
  if (sdReady) {
    Serial.print(F("\nMemoria detectada"));
    
    getFileName(); // llama a la funcion getFileName
    File newFile = SD.open(fileName, FILE_WRITE); // Crea un objeto tipo file que apunta a un archivo en la memoria SD
    // Si el objeto se creo correctamente, escribe en el archivo las cabeceras
    if (newFile) {
      newFile.print(F("ID, "));
      newFile.print(F("RPM, "));
      newFile.print(F("Fuerza (N), "));
      newFile.print(F("Torque (Nm), "));
      newFile.print(F("Torque (lb ft), "));
      newFile.println(F("Potencia (Hp)"));
      newFile.close();
      
      Serial.print(F("\nLos datos se guardaran en "));
      Serial.print(fileName);
      Serial.print(F("\n"));
    }
    //Si no se crea correctamente, se le asigna a sdReady el estado de false
    else 
      sdReady = false;

  }

  // Si sdReady tiene el estado de false, se mando un mensaje al monitor serie 
  if (!sdReady)
  {
    Serial.print(F("\nError SD. Archivo o memoria no disponible\n\n"));
  }
  delay(1000);
}

// Genera el nombre del archivo de datos a partir de concatenar "data" + nF + ".csv"
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