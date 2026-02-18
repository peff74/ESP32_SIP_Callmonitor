/*
 * ESP32 SIP CallMonitor with Ethernet LAN8720
 * 
 * Features:
 * - Ethernet LAN8720
 * - SIP registration
 * - Incoming call detection with buzzer signaling
 */

#include <ETH.h>
#include <WiFiUdp.h>
#include <MD5Builder.h>

// ========== ETHERNET CONFIGURATION ==========
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_POWER 16
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

// ========== SIP CONFIGURATION ==========
char SIP_SERVER[64] = "192.168.1.1";   // SIP server IP or hostname
char SIP_USER[64]   = "100";            // SIP extension
char SIP_PASS[64]   = "secret";         // SIP password
const int SIP_PORT        = 5060;
const int SIP_REG_TIMEOUT = 600;        // Registration expiry in seconds

// ========== LED ==========
const int LED_PIN = 2;

// ========== GLOBAL VARIABLES ==========
char sipOut[2048];
char sipIn[2048];
char tempBuf[256];

WiFiUDP udp;
String  currentIp;
uint32_t callid, tagid, branchid;
int      authCnt    = 0;
unsigned long lastRegTime = 0;
unsigned long regInterval = 0;
bool sipActive     = false;
bool sipRegistered = false;

bool ethConnected = false;

bool ringing = false;

// ========== HELPERS ==========
uint32_t randomId() {
  return ((rand() & 0x7fff) << 15) | (rand() & 0x7fff);
}

void md5Hash(char *out, const char *in) {
  MD5Builder md5;
  md5.begin();
  md5.add(in);
  md5.calculate();
  md5.getChars(out);
}

void addLine(char *buf, size_t size, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  size_t len = strlen(buf);
  vsnprintf(buf + len, size - len, fmt, args);
  va_end(args);
  len = strlen(buf);
  if (len < size - 2) {
    buf[len]     = '\r';
    buf[len + 1] = '\n';
    buf[len + 2] = 0;
  }
}

bool copyLine(char *buf, size_t size, const char *src, const char *search) {
  char *start = strstr((char *)src, search);
  if (!start) return false;
  char *end = strstr(start, "\r");
  if (!end) end = strstr(start, "\n");
  if (!end || end <= start) return false;
  char c = *end;
  *end = 0;
  addLine(buf, size, "%s", start);
  *end = c;
  return true;
}

bool parseParam(char *dst, int dstLen, const char *name, const char *line) {
  const char *r = strstr(line, name);
  if (!r) return false;
  r += strlen(name);
  const char *end = strchr(r, '\"');
  int len = end - r;
  if (len >= dstLen) return false;
  strncpy(dst, r, len);
  dst[len] = 0;
  return true;
}

// ========== LED ==========
void startLED() {
  if (ringing) return;
  ringing = true;
  digitalWrite(LED_PIN, HIGH);
  Serial.println(">>> LED ON (Incoming Call)");
}

void stopLED() {
  if (!ringing) return;
  ringing = false;
  digitalWrite(LED_PIN, LOW);
  Serial.println(">>> LED OFF");
}

// ========== SIP ==========
void sendUdp() {
  udp.beginPacket(SIP_SERVER, SIP_PORT);
  udp.write((uint8_t *)sipOut, strlen(sipOut));
  udp.endPacket();
  Serial.printf("\n>>> Sent %d bytes\n%s", strlen(sipOut), sipOut);
}

void sendResponse(const char *p, const char *status) {
  sipOut[0] = 0;
  addLine(sipOut, sizeof(sipOut), "SIP/2.0 %s", status);
  copyLine(sipOut, sizeof(sipOut), p, "Call-ID: ");
  copyLine(sipOut, sizeof(sipOut), p, "CSeq: ");
  copyLine(sipOut, sizeof(sipOut), p, "From: ");
  copyLine(sipOut, sizeof(sipOut), p, "Via: ");
  copyLine(sipOut, sizeof(sipOut), p, "To: ");
  addLine(sipOut, sizeof(sipOut), "Content-Length: 0");
  addLine(sipOut, sizeof(sipOut), "");
  sendUdp();
}

void sipRegister(const char *authData = NULL) {
  if (!sipActive) return;

  Serial.printf("Register %s auth\n", authData ? "with" : "without");

  if (authData && authCnt > 3) return;

  char realm[128], nonce[128], response[33];
  int cseq = authData ? 2 : 1;

  if (!authData) {
    authCnt      = 0;
    callid       = randomId();
    tagid        = randomId();
    branchid     = randomId();
    sipRegistered = false;
  } else {
    if (!parseParam(realm, 128, " realm=\"", authData) ||
        !parseParam(nonce, 128, " nonce=\"", authData)) return;

    char ha1[33], ha2[33];
    snprintf(tempBuf, 256, "%s:%s:%s", SIP_USER, realm, SIP_PASS);
    md5Hash(ha1, tempBuf);
    snprintf(tempBuf, 256, "REGISTER:sip:%s@%s", SIP_USER, SIP_SERVER);
    md5Hash(ha2, tempBuf);
    snprintf(tempBuf, 256, "%s:%s:%s", ha1, nonce, ha2);
    md5Hash(response, tempBuf);
    authCnt++;
  }

  sipOut[0] = 0;
  addLine(sipOut, sizeof(sipOut), "REGISTER sip:%s SIP/2.0", SIP_SERVER);
  addLine(sipOut, sizeof(sipOut), "Call-ID: %010u@%s", callid, currentIp.c_str());
  addLine(sipOut, sizeof(sipOut), "CSeq: %i REGISTER", cseq);
  addLine(sipOut, sizeof(sipOut), "Max-Forwards: 70");
  addLine(sipOut, sizeof(sipOut), "From: \"ESP32-SIP\" <sip:%s@%s>;tag=%010u", SIP_USER, SIP_SERVER, tagid);
  addLine(sipOut, sizeof(sipOut), "Via: SIP/2.0/UDP %s:%i;branch=%010u;rport=%i", currentIp.c_str(), SIP_PORT, branchid, SIP_PORT);
  addLine(sipOut, sizeof(sipOut), "To: <sip:%s@%s>", SIP_USER, SIP_SERVER);
  addLine(sipOut, sizeof(sipOut), "Contact: \"%s\" <sip:%s@%s:%i;transport=udp>", SIP_USER, SIP_USER, currentIp.c_str(), SIP_PORT);
  addLine(sipOut, sizeof(sipOut), "Expires: %d", SIP_REG_TIMEOUT);

  if (authData) {
    addLine(sipOut, sizeof(sipOut),
            "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"sip:%s@%s\", response=\"%s\"",
            SIP_USER, realm, nonce, SIP_USER, SIP_SERVER, response);
  }

  addLine(sipOut, sizeof(sipOut), "Content-Length: 0");
  addLine(sipOut, sizeof(sipOut), "");
  sendUdp();
}

void handleSipMessage(const char *msg) {
  if (!msg || !sipActive) return;

  if (strstr(msg, "SIP/2.0 401") == msg && strstr(msg, "REGISTER")) {
    Serial.println(">>> 401 Unauthorized – re-authenticating");
    sipRegister(msg);
  } else if (strstr(msg, "SIP/2.0 200") == msg && strstr(msg, "REGISTER")) {
    Serial.println(">>> Registered successfully");
    sipRegistered = true;
  } else if (strstr(msg, "SIP/2.0 403") == msg) {
    Serial.println(">>> 403 Forbidden");
  } else if (strstr(msg, "SIP/2.0 404") == msg) {
    Serial.println(">>> 404 Not Found");
  } else if (strstr(msg, "SIP/2.0 5") == msg) {
    Serial.println(">>> Server Error (5xx)");
  } else if (strstr(msg, "INVITE") == msg) {
    Serial.println(">>> INCOMING CALL");
    startLED();
    sendResponse(msg, "180 Ringing");
  } else if (strstr(msg, "BYE") == msg) {
    Serial.println(">>> BYE – call ended");
    stopLED();
    sendResponse(msg, "200 OK");
  } else if (strstr(msg, "CANCEL") == msg) {
    Serial.println(">>> CANCEL – call cancelled");
    stopLED();
    sendResponse(msg, "200 OK");
  } else if (strstr(msg, "INFO") == msg) {
    sendResponse(msg, "200 OK");
  }
}

void processSip() {
  if (!sipActive) return;
  int size = udp.parsePacket();
  if (size <= 0) return;
  size = udp.read(sipIn, sizeof(sipIn) - 1);
  if (size <= 0) return;
  sipIn[size] = 0;
  Serial.printf("\n<<< Received %d bytes from %s:%d\n%s",
                size, udp.remoteIP().toString().c_str(), udp.remotePort(), sipIn);
  handleSipMessage(sipIn);
}

void checkReregister() {
  if (!sipActive) return;
  if (millis() - lastRegTime < regInterval) return;
  Serial.println("Re-registering...");
  sipRegister();
  lastRegTime = millis();
}

void startSIP() {
  if (sipActive) return;
  Serial.println("\n=== Starting SIP ===");
  currentIp   = ETH.localIP().toString();
  regInterval = (SIP_REG_TIMEOUT / 2) * 1000UL;
  udp.begin(SIP_PORT);
  sipActive     = true;
  sipRegistered = false;
  sipRegister();
  lastRegTime = millis();
}

void stopSIP() {
  if (!sipActive) return;
  Serial.println("\n=== Stopping SIP ===");
  stopLED();
  udp.stop();
  sipActive     = false;
  sipRegistered = false;
}

// ========== ETHERNET EVENT HANDLER ==========
void ETHevent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("ESP32-SIP");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("ETH Got IP: %s\n", ETH.localIP().toString().c_str());
      ethConnected = true;
      delay(500);
      startSIP();
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Disconnected");
      ethConnected = false;
      stopSIP();
      break;
    default:
      break;
  }
}

// ========== MAIN ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 SIP CallMonitor ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Network.onEvent(ETHevent);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);

  Serial.println("Setup complete – waiting for ethernet and calls...");
}

void loop() {
  if (sipActive) {
    processSip();
    checkReregister();
  }
  delay(10);
}
