#include "uart_connection.h"

#include <Arduino.h>

#include "egg_control.h"
#include "gpio.h"
#include "oil_control.h"
#include "pot_a_motor.h"
#include "pot_b_motor.h"
#include "pot_i2c.h"
#include "push_motor.h"
#include "reservation_manager.h"
#include "trans_motor.h"

UartConnection UartConnection::instance;
HardwareSerial *UartConnection::m_serial;

namespace
{
char uartRxLine[PROTOCOL_MAX_LINE_LENGTH + 1];
size_t uartRxLength = 0;
bool uartRxDropping = false;
bool uartRxSawCarriageReturn = false;
}

UartConnection::UartConnection()
{
}

UartConnection *UartConnection::getInstance()
{
    return &instance;
}

void UartConnection::init()
{
    if (m_serial == nullptr)
    {
        m_serial = new HardwareSerial(2);
    }
    m_serial->begin(115200, SERIAL_8N1, UI_RX_PIN, UI_TX_PIN);
    ReservationManager::getInstance()->init();
    m_serial->setTimeout(50);

    timer = new Ticker(taskToExecute, 20, 0, MILLIS);
    timer->start();
}

void UartConnection::update()
{
    const ReservationEvent event = ReservationManager::getInstance()->update();
    if (event == ReservationEvent::STARTED)
    {
        sendEvent("reservation.started");
    }
    else if (event == ReservationEvent::EXPIRED)
    {
        sendEvent("reservation.expired");
    }
    else if (event == ReservationEvent::FAILED_RECOVERY_REQUIRED)
    {
        sendEvent("reservation.failed", "code=recovery_required");
    }
    else if (event == ReservationEvent::FAILED_MACHINE_ERROR)
    {
        sendEvent("reservation.failed", "code=machine_error");
    }

    timer->update();
}

void UartConnection::taskToExecute()
{
    if (m_serial == nullptr)
    {
        return;
    }

    while (m_serial->available() > 0)
    {
        const char received = static_cast<char>(m_serial->read());
        if (received == '\n')
        {
            if (!uartRxDropping && uartRxLength > 0)
            {
                uartRxLine[uartRxLength] = '\0';
                processLine(String(uartRxLine));
            }
            uartRxLength = 0;
            uartRxDropping = false;
            uartRxSawCarriageReturn = false;
            continue;
        }
        if (uartRxDropping)
        {
            continue;
        }
        if (received == '\r')
        {
            uartRxSawCarriageReturn = true;
            continue;
        }
        if (uartRxSawCarriageReturn)
        {
            uartRxLength = 0;
            uartRxDropping = true;
            continue;
        }
        if (uartRxLength >= PROTOCOL_MAX_LINE_LENGTH)
        {
            uartRxLength = 0;
            uartRxDropping = true;
            continue;
        }
        uartRxLine[uartRxLength++] = received;
    }
}

void UartConnection::home()
{
    TransMotor::getInstance()->home();
    PushMotor::getInstance()->home();
}

void UartConnection::setPotDw(const String &potName, int dw, int seconds)
{
    Serial.printf("UartConnection: heat command pot=%s dw=%d duration_s=%d\n",
                  potName.c_str(), dw, seconds);
    if (potName == "DWA")
    {
        PotI2c::getInstance()->setPotADW(dw, seconds * 1000);
    }
    else if (potName == "DWB")
    {
        PotI2c::getInstance()->setPotBDW(dw, seconds * 1000);
    }
}

void UartConnection::moveMotor(const String &motorName, int angle)
{
    Serial.printf("UartConnection: move motor=%s angle=%d\n", motorName.c_str(), angle);
    if (motorName == "TRANS")
    {
        TransMotor::getInstance()->enable();
        TransMotor::getInstance()->move(angle);
        TransMotor::getInstance()->relax();
    }
    else if (motorName == "PUSH")
    {
        PushMotor::getInstance()->enable();
        PushMotor::getInstance()->move(angle, true, 5000, 10000);
        PushMotor::getInstance()->relax();
    }
    else if (motorName == "POTA")
    {
        PotAMotor::getInstance()->enable();
        PotAMotor::getInstance()->move(angle);
        PotAMotor::getInstance()->relax();
    }
    else if (motorName == "POTB")
    {
        PotBMotor::getInstance()->enable();
        PotBMotor::getInstance()->move(angle);
        PotBMotor::getInstance()->relax();
    }
}

bool UartConnection::giveOil(int milliseconds)
{
    Serial.printf("Give oil command received for %d milliseconds.\n", milliseconds);
    return OilControl::getInstance()->giveOil(milliseconds);
}

void UartConnection::backOil()
{
    Serial.println("Back oil command received.");
    OilControl::getInstance()->backOil();
}

void UartConnection::beat()
{
    Serial.println("Beat command received.");
    PushMotor::getInstance()->beatEgg();
}

void UartConnection::giveEgg()
{
    Serial.println("Give egg command received.");
    EggControl::getInstance()->give();
}

void UartConnection::giveEggOpen()
{
    Serial.println("Give egg open command received.");
    EggControl::getInstance()->open();
}

void UartConnection::giveEggClose()
{
    Serial.println("Give egg close command received.");
    EggControl::getInstance()->close();
}
