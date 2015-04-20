// name the pins
int led1 = D0;
int led2 = D1;

#define LOGGING

// Server variables
//TCPClient client;
//byte server[] = { 74, 125, 224, 72 };
//char server[] = "bobblake.me";
//char server[] = "192.168.1.112";
//char remote_url[] = "/bbq/php/datalog.php";

// Key-value pair for each http header
// Create an array of these
// Content-length, http version are sent automatically, don't need to add them explicitly
typedef struct
{
    const char* header;
    const char* value;
} http_header_t;

// This holds the http request variables
typedef struct
{
    String      hostname;
    String      path;
    int         port;
    String      body;
} http_request_t;

// This holds the http response
typedef struct
{
    int status;
    String body;
} http_response_t;

// Set up HTTP headers
http_header_t headers[] = {
    {   "Content-Type", "text/csv" },
    {   "Connection", "close" },
    {   NULL, NULL }    // Always terminate this with NULL
};
http_request_t request;
http_response_t response;
void http_put(http_request_t &aRequest, http_response_t &aResponse, http_header_t headers[]);

void setup() {
    // Register the Spark functions
    Spark.function("led", ledControl);
    Spark.function("bbq", sendToBBQSite);

    // Set up HTTP variables
    request.hostname = "bobblake.me";
    request.path = "/bbq/php/datalog.php";
    request.port = 80;

    // Configure USB serial port
    Serial.begin(9600);
    // On Windows it will be necessary to implement the following line:
    // Make sure your Serial Terminal app is closed before powering your Core
    // Now open your Serial Terminal, and hit any key to continue!
    while(!Serial.available()) SPARK_WLAN_Loop();
    Serial.println("Hello from Spark!");

    // Configure pins as outputs
    pinMode(led1, OUTPUT);
    pinMode(led2, OUTPUT);

    // Initialize both the LEDs to be OFF
    digitalWrite(led1, LOW);
    digitalWrite(led2, LOW);
}

void loop() {
    /*if(client.available()){
        char c = client.read();
        Serial.print(c);
    }*/
}

void http_put(http_request_t &aRequest, http_response_t &aResponse, http_header_t headers[]) {
    TCPClient client;
    aResponse.status = -1;  // If a proper response code isn't received, set to -1

    //TODO: Make sure all necessary request fields are populated

    bool connected = false;
    if(aRequest.hostname!=NULL) {
        connected = client.connect(aRequest.hostname.c_str(), (aRequest.port) ? aRequest.port : 80 );
    }

    #ifdef LOGGING
    if (connected) {
        if(aRequest.hostname!=NULL) {
            Serial.print("HttpClient>\tConnecting to: ");
            Serial.print(aRequest.hostname);
        }
        Serial.print(":");
        Serial.println(aRequest.port);
    } else {
        Serial.println("HttpClient>\tConnection failed.");
    }
    #endif

    if (!connected){
        client.stop();  // If there's a problem connecting to host, exit here
        return;
    }

    #ifdef LOGGING
        // Send HTTP start-line
        Serial.print("PUT ");
    	Serial.print(request.path);
    	Serial.print(" HTTP/1.1\r\n");

    	// Then send (maybe) user-agent, (definitely) host and length header
    	Serial.print("User-Agent: Spark-Core/1.0\r\n");

    	Serial.print("Host: ");
    	Serial.print(request.hostname);
    	Serial.print("\r\n");

    	Serial.print("Content-length: ");
    	Serial.print(request.body.length());
    	Serial.print("\r\n");

    	// Then send the rest of the headesr
    	if (headers != NULL){
    	    int i = 0;
    	    while (headers[i].header != NULL){
    	        Serial.print(headers[i].header);
    	        Serial.print(": ");
    	        if (headers[i].value != NULL)
    	            Serial.print(headers[i].value);
    	        Serial.print("\r\n");
    	        i++;
    	    }
    	}
        Serial.println();   //Empty line to finish headers

        // Send body
        if (aRequest.body != NULL){
            Serial.println(aRequest.body);
        }
    #endif

    // Send HTTP start-line
    client.print("PUT ");
	client.print(request.path);
	client.print(" HTTP/1.1\r\n");

	// Then send (maybe) user-agent, (definitely) host and length header
	client.print("User-Agent: Spark-Core/1.0\r\n");

	client.print("Host: ");
	client.print(request.hostname);
	client.print("\r\n");

	client.print("Content-length: ");
	client.print(request.body.length());
	client.print("\r\n");

	// Then send the rest of the headesr
	if (headers != NULL){
	    int i = 0;
	    while (headers[i].header != NULL){
	        client.print(headers[i].header);
	        client.print(": ");
	        if (headers[i].value != NULL)
	            client.print(headers[i].value);
	        client.print("\r\n");
	        i++;
	    }
	}
    client.println();   //Empty line to finish headers
    client.flush();     //Flush input buffer to prepare for response...?

    // Send body
    if (aRequest.body != NULL){
        client.println(aRequest.body);
    }



}

// This will get called whenever there is a matching API request
// Command string format: l<led number>,<state>
// Examples: l1,HIGH or l2,LOW
int ledControl(String command) {
    int state = 0;
    // find out the pin number and conver the ascii to integer
    int pinNumber = (command.charAt(1) - '0') - 1;
    // sanity check to see if the pin numbers are within limits
    if (pinNumber < 0 || pinNumber > 1) return -1;

    // find out the state of the led
    if(command.substring(3,7) == "HIGH") state = 1;
    else if(command.substring(3,6) == "LOW") state = 0;
    else return -1;

    // write to the appropriate pin
    digitalWrite(pinNumber, state);
    return 1;
}

// Sends data to Bob's BBQ Site
// Command String Format: <Temp1 C>,<Temp2 C>
int sendToBBQSite(String command) {

    int comma;
    unsigned int len;

    comma = command.indexOf(',');
    if(comma < 1 || comma > 4){
        Serial.println("Comma error.");
        return -1;    // Invalid value, return error
    }

    len = command.length();
    if(len < 3 || len > 9){
        Serial.println("Length error.");
        return -1;     // Invalid value, return error
    }

    String content = String(command.substring(0,comma));   // No error checking is done here
    content.concat(String(","));
    content.concat(String(command.substring((comma+1),len)));
    content.concat(String(","));
    content.concat(String(Time.now()));

    request.body = content; // This doesn't seem like the most efficient way to do this


    Serial.println("Data received!");

    /*
    Serial.print("Connecting to server...");
    if(client.connect(server, 80)){
        Serial.println("connected!");
    }
    else{
        Serial.println("connection failed!");
        return -1;
    }*/

    http_put(request, response, headers);
    Serial.print("Application>\tResponse status: ");
    Serial.println(response.status);

    Serial.print("Application>\tHTTP Response Body: ");
    Serial.println(response.body);

    //String request = String("PUT " + String(remote_url) + " HTTP/1.1" + "\r\n");
    //request.concat(String("Host: " + String(server) + "\r\n"));
    //request.concat(String("Content-Type: text/csv" + "\r\n"));
    //request.concat(String("Content-Length: " + content.length() + "\r\n"));
    //request.concat(String("Connection: close" + "\r\n"));
    //request.concat(String("\r\n"));
    //request.concat(String(content + "\r\n"));

    /*
    Serial.print("Client request:\r\n\r\n");
    Serial.print("PUT ");
	Serial.print(remote_url);
	Serial.print(" HTTP/1.1\r\nHost: ");
	Serial.print(server);
	Serial.print("\r\n");
	Serial.print("Content-Type: text/csv\nContent-Length: ");
	Serial.print(output.length());
	Serial.print("\r\n");
	Serial.print("Connection: close\r\n\r\n");
	Serial.print(content);
	Serial.print("\r\n");

    client.print("PUT ");
	client.print(remote_url);
	client.print(" HTTP/1.1\r\nHost: ");
	client.print(server);
	client.print("\r\n");
	client.print("Content-Type: text/csv\nContent-Length: ");
	client.print(output.length());
	client.print("\r\n");
	client.print("Connection: close\r\n\r\n");
	client.print(content);
	client.print("\r\n");
    */
    /*
    unsigned long t1 = millis();
    while(!client.available() && (millis() - t1) < 10000 ); // Timeout after 10 seconds
    if(client.available())
        Serial.print("\r\n\r\nServer response:\r\n\r\n");
    while(client.available()){
        char c = client.read();
        Serial.print(c);
    }

    client.stop();*/

    return 1;
}
