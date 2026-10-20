#ifndef DEBUG_RUN_CONTROL_H
#define DEBUG_RUN_CONTROL_H

#include <stdint.h>
#include <Preferences.h>

enum class DebugCheckType
{
    TOF_SELF_TEST,
    EGG_BEFORE_LOAD,
    EGG_AFTER_LOAD,
    SHELL_RESIDUE,
    OIL_FEEDBACK,
    POT_PREFLIGHT
};

class DebugRunControl
{
public:
    static constexpr uint8_t ALL_BYPASS_MASK = (1U << 6U) - 1U;

    static DebugRunControl *getInstance();
    void init();

    bool isBypassEnabled() const;
    void setBypassEnabled(bool enabled);
    uint8_t getBypassMask() const;
    void setBypassMask(uint8_t mask);
    bool isCheckBypassed(DebugCheckType check) const;
    void setCheckBypassed(DebugCheckType check, bool enabled);
    bool isHeatingDisabled() const;
    void setHeatingDisabled(bool disabled);

    bool shouldAssumeSuccess(DebugCheckType check) const;
    bool canHeatPotA() const;
    bool canHeatPotB() const;

private:
    DebugRunControl();
    bool canHeatPot(bool potA) const;
    static uint8_t checkMask(DebugCheckType check);

    static DebugRunControl instance;
    Preferences *m_prefs;
    bool m_initialized;
    volatile uint8_t m_bypassMask;
    volatile bool m_heatingDisabled;
};

#endif
