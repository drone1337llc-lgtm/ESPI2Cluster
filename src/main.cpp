// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#include <Arduino.h>

// This file is intentionally minimal
// The actual device code is in master_main.cpp or worker_main.cpp
// Device type is selected via platformio.ini build flags:
// -D MASTER_DEVICE or -D WORKER_DEVICE

void setup()
{
  // Setup is defined in master_main.cpp or worker_main.cpp
  // This empty setup prevents compilation errors
}

void loop()
{
  // Loop is defined in master_main.cpp or worker_main.cpp
  // This empty loop prevents compilation errors
  delay(100);
}
