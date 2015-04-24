#include "HttpClient.h"

#define LOGGING   // Enable debug data from serial port
#define LOGGING_FINE

// Define pins
int pin_led     = D1;
int pin_data_in = A1;
int pin_debug   = D6;

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

#define RX_PRE_TIMEOUT  40000   // us

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
  RX_STATE_PREAMBLE,
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
      Serial.begin(115200);
      // On Windows it is necessary to implement the following line:
      // Make sure your Serial Terminal app is closed before powering your Core
      // Now open your Serial Terminal, and hit any key to continue!
      while(!Serial.available()) SPARK_WLAN_Loop();
      Serial.println("Hello from Spark!");
    #endif

    // Configure pin I/O
    pinMode(pin_led, OUTPUT);
    pinMode(pin_data_in, INPUT_PULLUP);
    pinMode(pin_debug, OUTPUT);
    digitalWrite(pin_debug,LOW);
    // Set up interrupt for incoming data
    attachInterrupt(pin_data_in,interrupt_ext,CHANGE);
}

void loop() {
  unsigned int i;
  #ifdef LOGGING
    if(rx_done){
      Serial.print("Data: ");
      for(i=0;i<sizeof(rx_data);i++){
        Serial.print(rx_data[i],DEC);
      }
      Serial.print("\r\n");
      rx_done = 0;
    }

    //if(rx_err > 1){
//      Serial.print("Err: ");
      //Serial.println(rx_err);
    //}
  #endif
}

// Use state machine here
// Idle, check preamble, receive data
void interrupt_ext() {
  volatile static int pre_counter=0, bit_counter=0, waiting=0;
  volatile static unsigned long start_time=0;
  volatile unsigned long now_time, pulse_width, timeout_counter, timeout;
  volatile bool  edge;


  now_time = micros(); // Save time  and edge immediately for best accuracy
  edge = digitalRead(pin_data_in);
  pulse_width = now_time - start_time;

  switch(state){

    case RX_STATE_IDLE:
      if(edge == HIGH){ // Received a low pulse
        timeout_counter = 0;
        bit_counter = 0;
        waiting = 0;
        rx_err = 0;
        if((pulse_width >= OOK_PRE_LOW_MIN) && (pulse_width <= OOK_PRE_LOW_MAX)){
            timeout_counter = now_time; // Mark time from the end of first low pulse
            state = RX_STATE_PREAMBLE;
        }
        else{ // Low pulse was too short
          rx_err = 1;
        }
      }
    break;

    case RX_STATE_PREAMBLE:
      if(edge == LOW){ // High-low transition
        if((pulse_width >= TWO_T_MIN) && (pulse_width <= TWO_T_MAX)){
          rx_data[bit_counter] = 1; // There must have been a rising edge first
          bit_counter++;
          rx_data[bit_counter] = 0;  // That's the falling edge we just got
          digitalWrite(pin_debug,HIGH);
          bit_counter++;
          state = RX_STATE_RECEIVE;
        }
        else if((pulse_width < T_MIN) || (pulse_width > TWO_T_MAX)){
          rx_err = 2;   // High pulse was too short or too long
          state = RX_STATE_IDLE;
        }
      }
      else if(edge == HIGH){ // Low-high transition
        if((pulse_width < OOK_PRE_LOW_MIN) || (pulse_width > OOK_PRE_LOW_MAX)){
          rx_err = 3;   // Low pulse was too long or short
          state = RX_STATE_IDLE;
        }
      }
    break;

    case RX_STATE_RECEIVE:
      if(edge == HIGH){
        if(pulse_width >= OOK_PRE_LOW_MIN){
          rx_err = 3;
          rx_done = 1;            // If we receive a long low pulse, call it done
          Serial.print("Err: ");
          Serial.println(rx_err);
          state = RX_STATE_IDLE;
        }
      }

      if((pulse_width >= T_MIN) && (pulse_width <= T_MAX)){ // If T

        if(waiting == 0){
          waiting = 1;  // Wait to capture next edge, make sure this is also T
        }
        else{
          rx_data[bit_counter] = rx_data[bit_counter - 1];  // Bit stays the same
          waiting = 0;
          bit_counter++;
        }
      }
      else if((pulse_width >= TWO_T_MIN) && (pulse_width <= TWO_T_MAX)){
        if(waiting == 1){  // Only one consecutive T means an error, reset
          rx_err = 4;
          Serial.print("Err: ");
          Serial.println(rx_err);
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
        digitalWrite(pin_debug,LOW);
        state = RX_STATE_IDLE;
      }
    break;
  }

  start_time = now_time;

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
