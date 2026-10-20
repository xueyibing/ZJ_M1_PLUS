#pragma once

namespace ntc_ad_temp_map
{
// 3K下拉，ad与温度对应，偏移宏: NTC_3K_DOWN_TEMP_OFFSET_C
int adToTemp3KDown(int adcValue);

// 5.1K下拉，ad与温度对应，偏移宏: NTC_5K1_DOWN_TEMP_OFFSET_C
int adToTemp5K1Down(int adcValue);

// 20K上拉，ad与温度对应，偏移宏: NTC_20K_UP_TEMP_OFFSET_C
int adToTemp20KUp(int adcValue);
} // namespace ntc_ad_temp_map
