#include "HttpClient.h"

#define LOGGING   // Enable debug data from serial port

// Define pins
int pin_led     = D1;
int pin_data_in = A1;
int pin_debug   = A2;

// Declare Global HTTP variables
HttpClient http;
http_request_t request;
http_response_t response;
http_header_t headers[] = {
    {   "Content-Type", "text/csv" },
    {   "Connection", "close" },
    {   NULL, NULL }    // Always terminate this with NULL
};

// Defines and variables for decoding OOK/Manchester data
#define OOK_PRE_LOW_MIN   4500  // us
#define OOK_PRE_LOW_MAX   5500  // us
#define OOK_PRE_HIGH_MIN  150   // us
#define OOK_PRE_HIGH_MAX  250   // us

enum rx_states{
  RX_STATE_IDLE,
  RX_STATE_CHECK_PRE,
  RX_STATE_RECEIVE
};
volatile enum rx_states state=RX_STATE_IDLE;

void setup() {
    // Register the Spark functions
    Spark.function("bbq", sendToBBQSite);

    // Set up HTTP variables
    request.hostname = "bobblake.me";
    request.path = "/bbq/php/datalog.php";
    request.port = 80;

    #ifdef LOGGING
      // Configure USB serial port
      Serial.begin(9600);
      // On Windows it is necessary to implement the following line:
      // Make sure your Serial Terminal app is closed before powering your Core
      // Now open your Serial Terminal, and hit any key to continue!
      while(!Serial.available()) SPARK_WLAN_Loop();
      Serial.println("Hello from Spark!");
    #endif

    // Configure pin I/O
    pinMode(pin_led, OUTPUT);
    pinMode(pin_data_in, INPUT);
    pinMode(pin_debug, OUTPUT);

    // Set up interrupt for incoming data
    attachInterrupt(pin_data_in,interrupt_ext,CHANGE);
}

void loop() {

}

// Use state machine here
// Idle, check preamble, receive data
void interrupt_ext() {
  static int counter=0;
  static unsigned long start_time;
  unsigned long now_time, pulse_width;

  digitalWrite(pin_debug,HIGH);
  now_time = micros(); // Save time immediately for best accuracy

  switch(state){

    case RX_STATE_IDLE:
      if(digitalRead(pin_data_in) == LOW){
        counter = 0;
        state = RX_STATE_CHECK_PRE;
      }
    break;

    case RX_STATE_CHECK_PRE:
      pulse_width = now_time - start_time;

      if(digitalRead(pin_data_in) == HIGH){ // Low-high transition
        if((pulse_width >= OOK_PRE_LOW_MIN) && (pulse_width <= OOK_PRE_LOW_MAX)){
          counter++;
          if(counter == 8){ // We have received the entire preamble
            state = RX_STATE_RECEIVE;
          }
        }
        else{ // Low pulse was too short
          state = RX_STATE_IDLE;
        }
      }
      else{ // High-low transition
        if((pulse_width >= OOK_PRE_HIGH_MIN) && (pulse_width <= OOK_PRE_HIGH_MAX)){
          // Do nothing
        }
        else{ // High pulse was too short
          state = RX_STATE_IDLE;
        }
      }
    break;

    case RX_STATE_RECEIVE:
      #ifdef LOGGING
        Serial.println("Data!");
      #endif
      state = RX_STATE_IDLE;
    break;
  }

  start_time = now_time;
  digitalWrite(pin_debug,LOW);
}

// Sends data to Bob's BBQ Site
// Command String Format: <Temp1 C>,<Temp2 C>
int sendToBBQSite(String command) {

    int comma;
    unsigned int len;

    comma = command.indexOf(',');
    if(comma < 1 || comma > 4){
        #ifdef LOGGING
          Serial.println("Comma error.");
        #endif
        return -1;    // Invalid value, return error
    }

    len = command.length();
    if(len < 3 || len > 9){
        #ifdef LOGGING
          Serial.println("Length error.");
        #endif
        return -1;     // Invalid value, return error
    }

    String content = String(command.substring(0,comma));   // No error checking is done here
    content.concat(String(","));
    content.concat(String(command.substring((comma+1),len)));
    content.concat(String(","));
    content.concat(String(Time.now()));

    request.body = content; // This doesn't seem like the most efficient way to do this

    #ifdef LOGGING
      Serial.println("Data received!");
    #endif

    http.put(request, response, headers);

    return 1;
}
