#include "reservation_manager.h"

#include "jiandan_data_manager.h"
#include "jiandan_maker.h"
#include "taiyangdan_data_manager.h"

ReservationManager ReservationManager::instance;

namespace
{
constexpr const char *NVS_NAMESPACE = "reservation";
constexpr const char *KEY_ACTIVE = "active";
constexpr const char *KEY_EPOCH = "epoch";
constexpr const char *KEY_QUANTITY = "qty";
constexpr const char *KEY_OIL = "oil";
constexpr const char *KEY_DONENESS = "doneness";
constexpr const char *KEY_STATUS = "status";
}

ReservationManager::ReservationManager()
    : initialized(false), clockValid(false), active(false), clockEpoch(0), clockMillis(0),
      epoch(0), quantity(1), oil(1), doneness(1), status(ReservationStatus::NONE)
{
}

ReservationManager *ReservationManager::getInstance() { return &instance; }

void ReservationManager::init()
{
    if (initialized) return;

    prefs.begin(NVS_NAMESPACE, false);
    active = prefs.getBool(KEY_ACTIVE, false);
    epoch = prefs.getUInt(KEY_EPOCH, 0);
    quantity = prefs.getUChar(KEY_QUANTITY, 1);
    oil = prefs.getUChar(KEY_OIL, 1);
    doneness = prefs.getUChar(KEY_DONENESS, 1);
    status = static_cast<ReservationStatus>(prefs.getUChar(KEY_STATUS, 0));
    initialized = true;

    if ((active && epoch < MIN_VALID_EPOCH) || quantity < 1 ||
        quantity > JiandanDataManager::maxMakeQuantityForDoneness(doneness) ||
        oil > 2 || doneness > 3 ||
        static_cast<uint8_t>(status) > static_cast<uint8_t>(ReservationStatus::FAILED_MACHINE_ERROR))
    {
        active = false;
        epoch = 0;
        quantity = 1;
        oil = 1;
        doneness = 1;
        status = ReservationStatus::NONE;
        persist();
    }
}

bool ReservationManager::syncClock(uint32_t syncedEpoch)
{
    if (syncedEpoch < MIN_VALID_EPOCH) return false;
    clockEpoch = syncedEpoch;
    clockMillis = millis();
    clockValid = true;
    return true;
}

bool ReservationManager::hasValidClock() const { return clockValid; }

uint32_t ReservationManager::nowEpoch() const
{
    if (!clockValid) return 0;
    return clockEpoch + static_cast<uint32_t>(millis() - clockMillis) / 1000UL;
}

bool ReservationManager::schedule(uint32_t requestedEpoch, uint8_t requestedQuantity,
                                  uint8_t requestedOil, uint8_t requestedDoneness,
                                  const char *&errorCode)
{
    JiandanDataManager *jiandan = JiandanDataManager::getInstance();
    const uint32_t now = nowEpoch();

    errorCode = "none";
    if (!clockValid) { errorCode = "clock_unsynced"; return false; }
    if (requestedOil > 2 || requestedDoneness > 3 || requestedQuantity < 1 ||
        requestedQuantity > JiandanDataManager::maxMakeQuantityForDoneness(requestedDoneness))
    {
        errorCode = "invalid_argument";
        return false;
    }
    if (requestedEpoch < now + MIN_LEAD_SECONDS) { errorCode = "time_too_soon"; return false; }
    if (requestedEpoch > now + MAX_LEAD_SECONDS) { errorCode = "time_too_far"; return false; }
    if (JiandanMaker::isEmergencyStopRequested()) { errorCode = "recovery_required"; return false; }
    if (jiandan->getErrorCode() != ALL_OK) { errorCode = "machine_error"; return false; }

    active = true;
    epoch = requestedEpoch;
    quantity = requestedQuantity;
    oil = requestedOil;
    doneness = requestedDoneness;
    status = ReservationStatus::SCHEDULED;
    persist();
    return true;
}

void ReservationManager::cancel()
{
    active = false;
    epoch = 0;
    status = ReservationStatus::NONE;
    persist();
}

ReservationEvent ReservationManager::update()
{
    if (!active || !clockValid) return ReservationEvent::NONE;

    const uint32_t now = nowEpoch();
    if (now < epoch) return ReservationEvent::NONE;
    if (now > epoch + LATE_GRACE_SECONDS)
    {
        clearActive(ReservationStatus::EXPIRED);
        return ReservationEvent::EXPIRED;
    }

    JiandanDataManager *jiandan = JiandanDataManager::getInstance();
    TaiyangdanDataManager *sun = TaiyangdanDataManager::getInstance();
    if (JiandanMaker::isEmergencyStopRequested())
    {
        clearActive(ReservationStatus::FAILED_RECOVERY_REQUIRED);
        return ReservationEvent::FAILED_RECOVERY_REQUIRED;
    }
    if (jiandan->getErrorCode() != ALL_OK)
    {
        clearActive(ReservationStatus::FAILED_MACHINE_ERROR);
        return ReservationEvent::FAILED_MACHINE_ERROR;
    }
    if (jiandan->getStart() || sun->getStart() || sun->hasNeed()) return ReservationEvent::NONE;

    jiandan->setDoneness(doneness);
    if (doneness == JiandanDataManager::SUNNY_DONENESS)
    {
        sun->setOilLevel(oil);
        sun->setQuantity(quantity);
        sun->setErrorCode(ALL_OK);
        sun->setStart(true);
    }
    else
    {
        jiandan->setOilLevel(oil);
        jiandan->setNeedMakeEggs(static_cast<float>(quantity));
        jiandan->setErrorCode(ALL_OK);
        jiandan->setStart(true);
    }
    clearActive(ReservationStatus::STARTED);
    return ReservationEvent::STARTED;
}

bool ReservationManager::isActive() const { return active; }
uint32_t ReservationManager::getEpoch() const { return active ? epoch : 0; }
uint8_t ReservationManager::getQuantity() const { return quantity; }
uint8_t ReservationManager::getOil() const { return oil; }
uint8_t ReservationManager::getDoneness() const { return doneness; }
ReservationStatus ReservationManager::getStatus() const { return status; }

void ReservationManager::persist()
{
    if (!initialized) return;
    prefs.putBool(KEY_ACTIVE, active);
    prefs.putUInt(KEY_EPOCH, epoch);
    prefs.putUChar(KEY_QUANTITY, quantity);
    prefs.putUChar(KEY_OIL, oil);
    prefs.putUChar(KEY_DONENESS, doneness);
    prefs.putUChar(KEY_STATUS, static_cast<uint8_t>(status));
}

void ReservationManager::clearActive(ReservationStatus newStatus)
{
    active = false;
    epoch = 0;
    status = newStatus;
    persist();
}
