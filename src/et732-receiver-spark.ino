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

#define NUM_PRE_PULSES  8
#define NUM_RX_BYTES    13
#define NUM_RX_NIBBLES  NUM_RX_BYTES * 2
#define NUM_RX_BITS     NUM_RX_BYTES * 8

#define TMR_TICK    1.62 //us
#define TWO_T_US    500  //us
#define T_US        250  //us

#define TWO_T_MIN   375 //2T - 0.5T
#define TWO_T_MAX   625 //etc
#define T_MIN       125
#define T_MAX       375

enum rx_states{
  RX_STATE_IDLE,
  RX_STATE_CHECK_PRE,
  RX_STATE_SYNC,
  RX_STATE_RECEIVE
};
volatile enum rx_states state=RX_STATE_IDLE;
volatile char rx_data[NUM_RX_BITS];
volatile int  rx_err=0, rx_done=0;

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
  if(rx_done){
    Serial.println("Data!");
    rx_done = 0;
  }
  /*
  if(state > RX_STATE_IDLE){
    Serial.print("State: ");
    Serial.println(state);
  }
  if(rx_err > 1){
    Serial.print("Err: ");
    Serial.println(rx_err);
  }*/
}

// Use state machine here
// Idle, check preamble, receive data
void interrupt_ext() {
  static int pre_counter=0, bit_counter=0;
  static unsigned long start_time;
  unsigned long now_time, pulse_width;
  bool  edge, waiting;

  digitalWrite(pin_debug,HIGH);
  now_time = micros(); // Save time  and edge immediately for best accuracy
  edge = digitalRead(pin_data_in);
  pulse_width = now_time - start_time;

  switch(state){

    case RX_STATE_IDLE:
      if(edge == LOW){
        pre_counter = 0;
        bit_counter = 0;
        waiting = 0;
        rx_err = 0;
        //rx_done = 0;
        state = RX_STATE_CHECK_PRE;
      }
    break;

    case RX_STATE_CHECK_PRE:
      if(edge == HIGH){ // Low-high transition
        if((pulse_width >= OOK_PRE_LOW_MIN) && (pulse_width <= OOK_PRE_LOW_MAX)){
          pre_counter++;
          if(pre_counter == NUM_PRE_PULSES){ // We have received the entire preamble
            state = RX_STATE_SYNC;
          }
        }
        else{ // Low pulse was too short
          rx_err = 1;
          state = RX_STATE_IDLE;
        }
      }
      else{ // High-low transition
        if((pulse_width >= OOK_PRE_HIGH_MIN) && (pulse_width <= OOK_PRE_HIGH_MAX)){
          // Do nothing
        }
        else{ // High pulse was too short
          rx_err = 2;
          state = RX_STATE_IDLE;
        }
      }
    break;

    case RX_STATE_SYNC:
      if((pulse_width >= TWO_T_MIN) && (pulse_width <= TWO_T_MAX)){  // Wait for 2T to start counting
        rx_data[bit_counter] = edge;
        bit_counter++;
        state = RX_STATE_RECEIVE;
      }
    break;

    case RX_STATE_RECEIVE:
      if((pulse_width >= T_MIN) && (pulse_width <= T_MAX)){ // If T
        if(!waiting){
          waiting = 1;  // Wait to capture next edge, make sure this is also T
        }
        else{
          rx_data[bit_counter] = rx_data[bit_counter - 1];  // Bit stays the same
          waiting = 0;
          bit_counter++;
        }
      }
      else if((pulse_width >= TWO_T_MIN) && (pulse_width <= TWO_T_MAX)){
        if(waiting){  // Only one consecutive T means an error, reset
          rx_err = 3;
          state = RX_STATE_IDLE;
        }
        // 2T means the bit changes
        if(rx_data[bit_counter - 1] == 0)
          rx_data[bit_counter] = 1;
        else
          rx_data[bit_counter] = 0;

        bit_counter++;
      }
      if(bit_counter >= NUM_RX_BITS){ // Full message received
        rx_done = 1;            // Maybe add something to protect data array against new data coming in quickly
        state = RX_STATE_IDLE;
      }
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
