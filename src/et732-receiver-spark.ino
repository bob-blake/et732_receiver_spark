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
#define RX_PRE_TIMEOUT  40000   // us

#define NUM_RX_BYTES    13
#define NUM_RX_NIBBLES  NUM_RX_BYTES * 2
#define NUM_RX_BITS     NUM_RX_BYTES * 8

#define TWO_T_US    500  //us
#define ONE_T_US    250  //us

#define ONE_T_MIN     125
#define ONE_T_MAX     375
#define TWO_T_MIN     375 //2T - 0.5T
#define TWO_T_MAX     625 //etc
#define TWENTY_T_MIN  4500  // us
#define TWENTY_T_MAX  5500  // us

enum rx_states{
  RX_STATE_IDLE,
  RX_STATE_PREAMBLE,
  RX_STATE_RECEIVE
};

enum pulse_width{
  SHORT,
  ONE_T,
  TWO_T,
  TWENTY_T,
  LONG
};

struct rx_pulse {
  pulse_width width;
  bool        edge;
};

volatile int  rx_err=0, rx_done=0;
char message[NUM_RX_BITS];


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
  char msg_parsed[NUM_RX_NIBBLES];
  signed int celsius;
  signed int probe1, probe2;

  #ifdef LOGGING
    if(rx_done){
      Serial.print("Raw Data: ");
      for(i=0;i<sizeof(message);i++){
        Serial.print(message[i],DEC);
      }
      Serial.print("\r\n");

      parse_binary_data(message,msg_parsed);
      probe1 = calc_probe_temp(1,msg_parsed);
      probe2 = calc_probe_temp(2,msg_parsed);

      Serial.print("Parsed Data: ");
      for(i=0;i<sizeof(msg_parsed);i++){
        Serial.print(msg_parsed[i],HEX);
      }
      Serial.print("\r\n");

      Serial.print("Temp 1: ");
      Serial.print(probe1,DEC);
      Serial.print("    Temp 2: ");
      Serial.print(probe2,DEC);
      Serial.print("\r\n\r\n");

      rx_done = 0;
    }

    if(rx_err > 2){
      Serial.print("Err: ");
      Serial.println(rx_err);
      rx_err = 0;
    }
  #endif
}

//Calculate probe temperature in celsius
signed int calc_probe_temp(char which_probe, char *rx_parsed)
{
    int i, offset, probe_temp;
    unsigned char   rx_quart[5];

    if(which_probe == 1)
        offset = 8;
    else
        offset = 13;

    //Parse data to quaternary
    for(i=0;i<5;i++){
        switch(rx_parsed[i+offset]){
            case 0x05:
                rx_quart[i] = 0;
            break;
            case 0x06:
                rx_quart[i] = 1;
            break;
            case 0x09:
                rx_quart[i] = 2;
            break;
            case 0x0A:
                rx_quart[i] = 3;
            break;
        }
    }
    probe_temp = 0;
    probe_temp += rx_quart[0] * 256;
    probe_temp += rx_quart[1] * 64;
    probe_temp += rx_quart[2] * 16;
    probe_temp += rx_quart[3] * 4;
    probe_temp += rx_quart[4] * 1;
    probe_temp -= 532;
    return probe_temp;
}

void parse_binary_data(char *binary_in, char *hex_out)
{
    int i,j;
    unsigned char temp;
    // Parse binary data into hex nibbles (stored inefficiently in bytes)
    for(i=0;i<NUM_RX_NIBBLES;i++){
        hex_out[i]=0; //initialize to 0
        for(j=0;j<4;j++){
            hex_out[i] <<= 1;
            temp = binary_in[(i*4)+j];
            hex_out[i] = hex_out[i] | temp;
        }
    }
}

void interrupt_ext() {
  static enum rx_states state=RX_STATE_IDLE;
  static int bit_cntr=0, waiting=0;
  static unsigned long start_time=0;
  unsigned long now_time, pulse_time;
  struct rx_pulse pulse;
  static char rx_data[NUM_RX_BITS];

  // Mark time and read edge immediately for best accuracy
  now_time = micros();
  pulse.edge = digitalRead(pin_data_in);

  // Now, figure out what kind of pulse it was
  pulse_time = now_time - start_time;

  if(pulse_time < ONE_T_MIN)
    pulse.width = SHORT;
  else if(pulse_time <= ONE_T_MAX)
    pulse.width = ONE_T;
  else if(pulse_time <= TWO_T_MAX)
    pulse.width = TWO_T;
  else if((pulse_time > TWENTY_T_MIN) && (pulse_time <= TWENTY_T_MAX))
    pulse.width = TWENTY_T;
  else
    pulse.width = LONG;


  switch(state){

    case RX_STATE_IDLE:
      if((pulse.width == TWENTY_T) && (pulse.edge == HIGH)){ // Received a long low pulse
        bit_cntr = 0;
        waiting = 0;
        rx_err = 0;
        state = RX_STATE_PREAMBLE;
        digitalWrite(pin_led,LOW);
      }
    break;

    case RX_STATE_PREAMBLE:
      if(pulse.width == TWO_T){
        rx_data[bit_cntr++] = !pulse.edge; // There must have been a previous edge
        rx_data[bit_cntr++] = pulse.edge;  // That's the edge we just captured
        state = RX_STATE_RECEIVE;
        digitalWrite(pin_led,HIGH);
      }
      else if((pulse.width == ONE_T) && (pulse.edge == LOW)){
        // Just a normal high pulse in the preamble
      }
      else if((pulse.width == TWENTY_T) && (pulse.edge = HIGH)){
        // Just a normal low pulse in the preamble
      }
      else{
        rx_err = 1;
        state = RX_STATE_IDLE;
      }
    break;

    case RX_STATE_RECEIVE:
      if(pulse.width == ONE_T){
        if(waiting == 0){
          waiting = 1;  // Wait to capture next edge, make sure this is also T
        }
        else{
          rx_data[bit_cntr] = rx_data[bit_cntr - 1];  // Repeat last bit
          bit_cntr++;
          waiting = 0;
        }
      }
      else if(pulse.width == TWO_T){
        if(waiting == 1){  // Only one consecutive T shouldn't happen, reset
          rx_err = 2;
          state = RX_STATE_IDLE;
        }
        rx_data[bit_cntr] = rx_data[bit_cntr - 1] ^ 0b00000001; // Toggle last bit
        bit_cntr++;
      }
      else{
        rx_err = 3;
        state = RX_STATE_IDLE;
      }

      if(bit_cntr >= NUM_RX_BITS){ // Full message received
        rx_done = 1;
        memcpy(&message,&rx_data,sizeof(rx_data));
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
