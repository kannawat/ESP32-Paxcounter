/* This routine is called by interrupt in regular intervals */
/* Interval can be set in paxcounter.conf (HOMECYCLE)       */

// Basic config
#include "cyclic.h"

// Local logging tag
static const char TAG[] = "main";

uint32_t userUTCTime; // Seconds since the UTC epoch
unsigned long nextTimeSync = millis();

// do all housekeeping
void doHousekeeping() {

  // update uptime counter
  uptime();

  // check if update mode trigger switch was set
  if (cfg.runmode == 1)
    do_reset();

  spi_housekeeping();
  lora_housekeeping();

// time sync once per TIME_SYNC_INTERVAL
#ifdef TIME_SYNC_INTERVAL
  if (millis() >= nextTimeSync) {
    nextTimeSync = millis() + TIME_SYNC_INTERVAL *
                                  60000; // set up next time sync period
    do_timesync();
  }
#endif

#ifdef HAS_BME
  // read BME280 sensor if present
  bme_read();
#endif

  // task storage debugging //
  ESP_LOGD(TAG, "Wifiloop %d bytes left",
           uxTaskGetStackHighWaterMark(wifiSwitchTask));
  ESP_LOGD(TAG, "IRQhandler %d bytes left",
           uxTaskGetStackHighWaterMark(irqHandlerTask));
#ifdef HAS_GPS
  ESP_LOGD(TAG, "Gpsloop %d bytes left", uxTaskGetStackHighWaterMark(GpsTask));
#endif

#if (HAS_LED != NOT_A_PIN) || defined(HAS_RGB_LED)
  ESP_LOGD(TAG, "LEDloop %d bytes left",
           uxTaskGetStackHighWaterMark(ledLoopTask));
#endif

// read battery voltage into global variable
#ifdef HAS_BATTERY_PROBE
  batt_voltage = read_voltage();
  ESP_LOGI(TAG, "Measured Voltage: %dmV", batt_voltage);
#endif

  // check free memory
  if (esp_get_minimum_free_heap_size() <= MEM_LOW) {
    ESP_LOGI(TAG,
             "Memory full, counter cleared (heap low water mark = %d Bytes / "
             "free heap = %d bytes)",
             esp_get_minimum_free_heap_size(), ESP.getFreeHeap());
    SendPayload(COUNTERPORT); // send data before clearing counters
    reset_counters();      // clear macs container and reset all counters
    get_salt();            // get new salt for salting hashes

    if (esp_get_minimum_free_heap_size() <= MEM_LOW) // check again
      do_reset(); // memory leak, reset device
  }
} // doHousekeeping()

// uptime counter 64bit to prevent millis() rollover after 49 days
uint64_t uptime() {
  static uint32_t low32, high32;
  uint32_t new_low32 = millis();
  if (new_low32 < low32)
    high32++;
  low32 = new_low32;
  return (uint64_t)high32 << 32 | low32;
}

void reset_counters() {
  macs.clear();   // clear all macs container
  macs_total = 0; // reset all counters
  macs_wifi = 0;
  macs_ble = 0;
}

void do_timesync() {
#ifdef TIME_SYNC_INTERVAL
// sync time & date if we have valid gps time
#ifdef HAS_GPS
  if (gps.time.isValid()) {
    setTime(gps.time.hour(), gps.time.minute(), gps.time.second(),
            gps.date.day(), gps.date.month(), gps.date.year());
    ESP_LOGI(TAG, "Time synced by GPS to %02d:%02d:%02d", hour(), minute(),
             second());
    return;
  } else {
    ESP_LOGI(TAG, "No valid GPS time");
  }
#endif // HAS_GPS
  // Schedule a network time request at the next possible time
  LMIC_requestNetworkTime(user_request_network_time_callback, &userUTCTime);
  ESP_LOGI(TAG, "Network time request scheduled");
#endif // TIME_SYNC_INTERVAL
} // do_timesync()

#ifndef VERBOSE
int redirect_log(const char *fmt, va_list args) {
  // do nothing
  return 0;
}
#endif
