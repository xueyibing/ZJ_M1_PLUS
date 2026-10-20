#ifndef UART_CONNECTION_H
#define UART_CONNECTION_H

#include <HardwareSerial.h>
#include <Ticker.h>

#include "protocol_frame.h"

class UartConnection
{
public:
    static UartConnection *getInstance();

    void init();
    void update();

private:
    UartConnection();

    static void taskToExecute();
    static void processLine(const String &line);
    static void dispatchRequest(const ProtocolRequest &request);
    static void sendResponse(const ProtocolRequest &request,
                             const char *result,
                             const char *code,
                             const String &fields = "");
    static void sendEvent(const char *event, const String &fields = "");

    // Hardware actions used by the structured debug.run protocol command.
    static void home();
    static void setPotDw(const String &potName, int dw, int seconds);
    static void moveMotor(const String &motorName, int angle);
    static bool giveOil(int milliseconds);
    static void backOil();
    static void beat();
    static void giveEgg();
    static void giveEggOpen();
    static void giveEggClose();

    static UartConnection instance;
    static HardwareSerial *m_serial;
    Ticker *timer = nullptr;
};

#endif
