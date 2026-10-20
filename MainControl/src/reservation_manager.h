#ifndef RESERVATION_MANAGER_H
#define RESERVATION_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>

enum class ReservationStatus : uint8_t
{
    NONE = 0,
    SCHEDULED = 1,
    STARTED = 2,
    EXPIRED = 3,
    FAILED_RECOVERY_REQUIRED = 4,
    FAILED_MACHINE_ERROR = 5
};

enum class ReservationEvent : uint8_t
{
    NONE = 0,
    STARTED,
    EXPIRED,
    FAILED_RECOVERY_REQUIRED,
    FAILED_MACHINE_ERROR
};

class ReservationManager
{
public:
    static constexpr uint32_t MIN_VALID_EPOCH = 1704067200UL;
    static constexpr uint32_t MIN_LEAD_SECONDS = 30UL;
    static constexpr uint32_t MAX_LEAD_SECONDS = 7UL * 24UL * 60UL * 60UL;
    static constexpr uint32_t LATE_GRACE_SECONDS = 5UL * 60UL;

    static ReservationManager *getInstance();
    void init();
    bool syncClock(uint32_t epoch);
    bool hasValidClock() const;
    uint32_t nowEpoch() const;
    bool schedule(uint32_t epoch, uint8_t quantity, uint8_t oil, uint8_t doneness,
                  const char *&errorCode);
    void cancel();
    ReservationEvent update();
    bool isActive() const;
    uint32_t getEpoch() const;
    uint8_t getQuantity() const;
    uint8_t getOil() const;
    uint8_t getDoneness() const;
    ReservationStatus getStatus() const;

private:
    ReservationManager();
    void persist();
    void clearActive(ReservationStatus status);

    static ReservationManager instance;
    Preferences prefs;
    bool initialized;
    bool clockValid;
    bool active;
    uint32_t clockEpoch;
    uint32_t clockMillis;
    uint32_t epoch;
    uint8_t quantity;
    uint8_t oil;
    uint8_t doneness;
    ReservationStatus status;
};

#endif
