#include <Homie.h>

const int PIN_PUMP = D1;
const int PIN_LIGHT = D2;

HomieNode lightNode("light", "switch");
HomieNode pumpNode("pump", "switch");

bool pumpOnHandler(const HomieRange& range, const String& value)
{
    if (value != "true" && value != "false") return false;

    bool on = (value == "true");
    digitalWrite(PIN_PUMP, on ? LOW : HIGH);
    pumpNode.setProperty("on").send(value);
    Homie.getLogger() << "Pump is " << (on ? "on" : "off") << endl;

    return true;
}

bool lightOnHandler(const HomieRange& range, const String& value)
{
    if (value != "true" && value != "false") return false;

    bool on = (value == "true");
    digitalWrite(PIN_LIGHT, on ? LOW : HIGH);
    lightNode.setProperty("on").send(value);
    Homie.getLogger() << "Light is " << (on ? "on" : "off") << endl;

    return true;
}

void setup()
{
    Serial.begin(115200);
    Serial << endl << endl;
    pinMode(PIN_PUMP, OUTPUT);
    pinMode(PIN_LIGHT, OUTPUT);
    digitalWrite(PIN_PUMP, HIGH);
    digitalWrite(PIN_LIGHT, HIGH);

    Homie_setFirmware("relay-controller", "1.0.0");

    lightNode.advertise("on").settable(lightOnHandler);
    pumpNode.advertise("on").settable(pumpOnHandler);

    Homie.setup();
}

void loop()
{
    Homie.loop();
}





