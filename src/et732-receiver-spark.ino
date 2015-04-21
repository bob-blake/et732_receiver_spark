#include "HttpClient.h"

// name the pins
int led = D1;

#define LOGGING

unsigned int nextTime = 0;    // Next time to contact the server
HttpClient http;

// Set up HTTP headers
http_header_t headers[] = {
    {   "Content-Type", "text/csv" },
    {   "Connection", "close" },
    {   NULL, NULL }    // Always terminate this with NULL
};

http_request_t request;
http_response_t response;

void setup() {
    // Register the Spark functions
    Spark.function("led", ledControl);
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
    pinMode(led, OUTPUT);
}

void loop() {

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
