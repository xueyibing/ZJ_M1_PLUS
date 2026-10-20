# AI Coding Guidelines for ZJ_M1 Egg Cooker Project

## Project Overview
This is an ESP32-S3 based automatic egg cooker system with dual cooking pots (A/B), oil dispensing, heating control, motor-driven pot flipping and egg transport, weight sensing, and UART-based UI. The system uses FreeRTOS for concurrent task management.

## Architecture Patterns
- **Singleton Pattern**: All major components (DataManager, OilControl, PotAMotor, etc.) use singleton pattern. Access via `ClassName::getInstance()`.
- **FreeRTOS Tasks**: Use `xTaskCreatePinnedToCore()` for concurrent operations. Tasks self-delete with `vTaskDelete(NULL)` when complete.
- **Mutex Synchronization**: Use `xSemaphoreCreateMutex()` and `xSemaphoreTake()`/`xSemaphoreGive()` for shared resource access (e.g., PotAMotor in maker.cpp).
- **Component Initialization**: All singletons initialized in `main.cpp::init()`, updates in `main.cpp::loop()`.

## Key Components
- **Maker**: Orchestrates cooking process, creates heating and pot operation tasks
- **DataManager**: Persistent state storage using Preferences library
- **PotIIC**: Heating power control via IIC communication
- **Motor Classes**: Stepper motor controls using FastAccelStepper (PotAMotor, PotBMotor, TransMotor, PushMotor)
- **OilControl**: Oil dispensing logic
- **UI_UART & TM1640**: User interface and display

## Development Workflow
- **Build**: `platformio run --environment esp32-s3`
- **Upload**: `platformio run --target upload --environment esp32-s3 --upload-port COM20`
- **Debug**: Use `Serial.printf()` for logging, monitor via serial at 115200 baud

## Coding Conventions
- **Logging**: Use `Serial.printf()` for debug output, include descriptive messages
- **Task Parameters**: Pass structs by pointer to tasks, access via `(TaskParams*)params`
- **Timing**: Use `millis()` for relative timing, `vTaskDelay()` for task sleeping
- **Error Handling**: Set global flags like `ERR_OIL_EMPTY` to signal errors across tasks
- **Heating Levels**: Induction cookers are controlled by DW levels. `DataManager` stores the user dynamic DW range 1~6; `PotIIC` sends hardware DW 0~8 and maps active DW to the cooker `cur` byte.

## Example Patterns
- **Task Creation**: `xTaskCreatePinnedToCore(Maker::heatATask, "HotATask", 1024*4, params, 2, NULL, 1);`
- **Mutex Usage**: See PotBMaker in maker.cpp for blocking mutex acquisition
- **Singleton Access**: `PotAMotor::getInstance()->turn();`

## Dependencies
- HX711 for weight sensing
- Ticker for timing
- FastAccelStepper for motor control
- Preferences for persistent storage</content>
<parameter name="filePath">d:\gitee\zj_m1\.github\copilot-instructions.md
