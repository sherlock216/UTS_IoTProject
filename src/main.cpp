#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>


#define pin_buz 25
#define pin_red_led 22
#define pin_green_led 23
#define pin_button 4
#define pin_trigger 13
#define pin_echo 14


char *ssid = "MyiPhone";
char *pwd = "pwd";

#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "Ex_token"
#define INFLUXDB_ORG "Ex_org"
#define INFLUXDB_BUCKET "Ex_bucketname"

// Time zone info
#define TZ_INFO "AEST-10AEDT,M10.1.0,M4.1.0/3"

// Declare InfluxDB client_mqtt instance with preconfigured InfluxCloud certificate
InfluxDBClient client_influxdb(INFLUXDB_URL, INFLUXDB_ORG,
            INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
// Declare Data point
Point sensor("hyojunsesp32");

volatile int buz_freq = 2e3;
char res_buz = 12;
volatile int dc_buz = 50;

bool button_pressed = false;
bool red_active = true;

unsigned long sonar_time = 0;

volatile int16_t thr = 10;
volatile int8_t red_people = 0;
volatile int8_t green_people = 0;



// MQTT Broker
const char *mqtt_broker = "broker.emqx.io";
const char *mqtt_topic = "hyojunsesp32/msgIn/sonar_threshold";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;


unsigned long tA_clock_calibration = 0;
String clientID;

WiFiClient espClient;
PubSubClient client_mqtt(espClient);

void callback_mqtt(char *topic, byte *payload, unsigned int length);
void reconnect();
void fun_subscribe(void);
void fun_connect2wifi();
float fun_sonar(void);
void check_crossing(float range);

void setup()
{
  Serial.begin(115200);

  pinMode(pin_button, INPUT);
  ledcSetup(0, buz_freq, res_buz);
  ledcAttachPin(pin_buz, 0);
  ledcWrite(0, 0);

  ledcSetup(1, 1000, res_buz);
  ledcAttachPin(pin_red_led, 1);
  
  ledcSetup(2, 1000, res_buz);
  ledcAttachPin(pin_green_led, 2);

  pinMode(pin_echo, INPUT);
  pinMode(pin_trigger, OUTPUT);
  fun_connect2wifi();


  clientID = "hyojunsesp32-";
  clientID += WiFi.macAddress();

  client_mqtt.setServer(mqtt_broker, mqtt_port);
  client_mqtt.setCallback(callback_mqtt);
  while (!client_mqtt.connected())
  {
    Serial.printf("The client_mqtt %s connects to the public MQTT broker\n", clientID.c_str());
    if (client_mqtt.connect(clientID.c_str(), mqtt_username, mqtt_password))
    {
      Serial.println("Public EMQX MQTT broker connected");
    }
    else
    {
      Serial.print("failed with state ");
      Serial.print(client_mqtt.state());
      delay(2000);
    }
  }
  fun_subscribe();

  // Accurate time is necessary for certificate validation and writing in batches
  // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  if (client_influxdb.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client_influxdb.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client_influxdb.getLastErrorMessage());
  }


  
  // Add tags to the data point
  sensor.addTag("device", "ESP32");
  sensor.addTag("location", "homeoffice");
  sensor.addTag("esp32_id", String(WiFi.BSSIDstr().c_str()));
  tA_clock_calibration = millis(); 

  Serial.println("Setup complete. Waiting for button press to start.");
}

void loop()
{

  // If no Wifi signal, try to reconnect it
  if (!WiFi.isConnected())
  {
    WiFi.reconnect(); 
    Serial.println("Wifi connection lost");
  }

  if (millis() - sonar_time >= 100) {
    float range = fun_sonar();
    sonar_time = millis();

    check_crossing(range);

  }

  if (digitalRead(pin_button) == LOW) {
    delay(50);
    if (digitalRead(pin_button) == LOW) {
      button_pressed = true;
      red_active = true;
      Serial.println("Button pressed. Starting sequence.");
    }
  }

  if (button_pressed) {
    if (red_active) {
      Serial.println("Red LED and Buzzer sequence started.");
      for (int i = 0; i < 4095; i += 409) { // Gradually decrease brightness over 15 seconds
        ledcWrite(1, 4095 - i); // Decrease red LED brightness
        int freq = map(i, 0, 4095, 1000, 3000); // Increase buzzer frequency
        ledcChangeFrequency(0, freq, res_buz); // Update frequency
        Serial.print("Red LED Brightness: ");
        Serial.print(4095 - i);
        Serial.print(" | Buzzer Frequency: ");
        Serial.println(freq);

        // Blinking effect (0.5 seconds ON, 0.5 seconds OFF)
        ledcWrite(0, dc_buz);  // Turn buzzer ON
        delay(500);            // ON for 0.5 seconds
        ledcWrite(0, 0);       // Turn buzzer OFF
        delay(500);            // OFF for 0.5 seconds

        if (millis() - sonar_time >= 100) {
          float range = fun_sonar();
          sonar_time = millis();

          check_crossing(range);
        }

      }
      red_active = false;
      Serial.println("Red sequence complete. Switching to Green.");
    } else {
      ledcWrite(1, 0);
      Serial.println("Green LED and Buzzer sequence started.");
      for (int i = 0; i < 4095; i += 409) { // Gradually decrease brightness over 15 seconds
        ledcWrite(2, 4095 - i); // Decrease green LED brightness
        int freq = map(i, 0, 4095, 1000, 3000); // Increase buzzer frequency
        ledcChangeFrequency(0, freq, res_buz); // Update frequency
        Serial.print("Green LED Brightness: ");
        Serial.print(4095 - i);
        Serial.print(" | Buzzer Frequency: ");
        Serial.println(freq);

        // Blinking effect (0.5 seconds ON, 0.5 seconds OFF)
        ledcWrite(0, dc_buz);  // Turn buzzer ON
        delay(500);            // ON for 0.5 seconds
        ledcWrite(0, 0);       // Turn buzzer OFF
        delay(500);            // OFF for 0.5 seconds

        if (millis() - sonar_time >= 100) {
          float range = fun_sonar();
          sonar_time = millis();

          check_crossing(range);
        }
      }
      button_pressed = false; // Need to press button again to start red sequence
      red_active = true;
      Serial.println("Green sequence complete. Waiting for button press.");
    }
  } else {
    // Do nothing and keep red state
    ledcWrite(1, 4095); // Keep red LED at maximum brightness
    ledcWrite(2, 0);    // Keep green LED off
    ledcWrite(0, 0);    // Turn buzzer off
  }

  
  if (!client_mqtt.connected())
  {
    reconnect();
  }
  client_mqtt.loop();

}

void callback_mqtt(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("-----------------------");

  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  } else {

    if (String(topic) == mqtt_topic) {
      const char *thr_local = doc["thr"];

      Serial.println(thr);

      thr = String(thr_local).toInt();
    }
  }

  Serial.print("Updated thr value: ");
  Serial.println(thr);
}

void reconnect()
{
  while (!client_mqtt.connected())
  {
    Serial.print("Attempting MQTT connection...");

    if (client_mqtt.connect(clientID.c_str()))
    {
      Serial.println("connected");
      fun_subscribe();
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client_mqtt.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void fun_subscribe(void)
{
  client_mqtt.subscribe(mqtt_topic);
  Serial.print("Subscribed to: ");
  Serial.println(mqtt_topic);
}

void fun_connect2wifi()
{
  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\n");
  Serial.print("Connected to the Wi-Fi network with local IP: ");
  Serial.println(WiFi.localIP());
}

float fun_sonar()
{

  digitalWrite(pin_trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(pin_trigger, LOW);

  unsigned long tmp = pulseIn(pin_echo, HIGH);
  float range = (float)tmp / 1.0e6 * 340. / 2. * 100.;

  Serial.printf(">sonar(mm):%f\n", range);
  delay(50);
  return range;
}


void check_crossing(float range) {

  String topic_base = String("hyojunsesp32/sonarDistance/");
  String distance_str = String(range);
  const char *payload = distance_str.c_str();
  client_mqtt.publish(topic_base.c_str(), payload); 
  Serial.print("Published distance to MQTT on topic: ");
  Serial.println(topic_base);
  Serial.print("Payload: ");
  Serial.println(payload);

  if (range <= thr) {
    if (red_active) {
      sensor.clearFields();
      red_people += 1;
      sensor.addField("red_crossing", red_people);
      sensor.addField("green_crossing", green_people);
      // Print what are we exactly writing
      Serial.print("Writing: ");
      Serial.println(client_influxdb.pointToLineProtocol(sensor));
      client_influxdb.writePoint(sensor);
      Serial.println("Red crossing detected and sent to InfluxDB.");
    } else {
      sensor.clearFields();
      green_people += 1;
      sensor.addField("red_crossing", red_people);
      sensor.addField("green_crossing", green_people);
      // Print what are we exactly writing
      Serial.print("Writing: ");
      Serial.println(client_influxdb.pointToLineProtocol(sensor));
      client_influxdb.writePoint(sensor);
      Serial.println("Green crossing detected and sent to InfluxDB.");
    }
  }
}