/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "eagle_soc.h"
#include "mgos.h"
#include "mgos_adc.h"
#include "mgos_aws_shadow.h"
#include "mgos_mqtt.h"
#include "mgos_gpio.h"
#include "mgos_hal.h"
#include "mgos_gpio_hal.h"
#include "mgos_timers.h"
#include "frozen/frozen.h"

#define PIN_THERM_DRIVE 4 //D2
#define PIN_HEATER_RELAY 5 //D1
#define POLL_PERIOD 30000

int D_PINS[] = {PIN_THERM_DRIVE};
int INPUT_PIN = 14;
int PIN_COUNT = 1;
bool init = false;
int NUMSAMPLES = 5;
int isHeating = 0;

int heaterOnAbove = 2900;
int heaterOffBelow = 2800;

#define GPIO_PIN_ADDR(i)        (GPIO_PIN0_ADDRESS + i*4)


static int readVoltage(void) {
  int i;
  int samples[NUMSAMPLES];
  int average;
 
  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = mgos_adc_read(0);
   	mgos_msleep(100);
  }
 
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  return average;
}

static int adjustHeater(int voltage) {
  if (voltage < heaterOffBelow) {
    mgos_gpio_write(PIN_HEATER_RELAY, 0);
    isHeating = 0;
  } 
  if (voltage > heaterOnAbove) {
    mgos_gpio_write(PIN_HEATER_RELAY, 1);
    isHeating = 1;
  }
  return isHeating;
}

//t:2666}
static void measureTemp() {
  LOG(LL_INFO, ("Measuring temperature"));
	int voltage = 0;

	mgos_gpio_write(PIN_THERM_DRIVE, 0);
	mgos_msleep(1);
	voltage = readVoltage();
	mgos_gpio_write(PIN_THERM_DRIVE, 1);

  int heating = adjustHeater(voltage);

	char topic[34];
	sprintf(topic,"devices/%s/temperature", mgos_sys_config_get_device_id());
	// LOG(LL_INFO, ("Published '%s'[%d] to topic '%s'",buffer, size, topic));
	mgos_mqtt_pubf(topic, 0, false, "{ t: %d, h: %d }", voltage, heating);   /* Publish */
  // mgos_mqtt_pubf(topic, 0, false, "{ t: %d }", voltage)
}

void timer_cb(void *arg) {
	measureTemp();
	(void) arg;
}

static void aws_shadow_state_handler(void *arg, enum mgos_aws_shadow_event ev,
                                     uint64_t version,
                                     const struct mg_str reported,
                                     const struct mg_str desired,
                                     const struct mg_str reported_md,
                                     const struct mg_str desired_md) {
  LOG(LL_INFO, ("== Event: %d (%s), version: %llu", ev,
                mgos_aws_shadow_event_name(ev), version));
  
  // if (ev != MGOS_AWS_SHADOW_CONNECTED &&
    if (ev != MGOS_AWS_SHADOW_GET_ACCEPTED &&
      ev != MGOS_AWS_SHADOW_UPDATE_DELTA) {
    LOG(LL_INFO, ("Event type %d will be ignored", ev));
    return;
  }

  // LOG(LL_INFO, ("Reported state: %.*s", (int) reported.len, reported.p));
  // LOG(LL_INFO, ("Desired state : %.*s", (int) desired.len, desired.p));
  // LOG(LL_INFO,
  //     ("Reported metadata: %.*s", (int) reported_md.len, reported_md.p));
  // LOG(LL_INFO, ("Desired metadata : %.*s", (int) desired_md.len, desired_md.p));
  
  int newHeaterOffBelow, newHeaterOnAbove;
  int count = json_scanf(desired.p, desired.len, "{heater-config:{off-below:%d, on-above:%d}}", &newHeaterOffBelow, &newHeaterOnAbove);
  if (count == 2) {
    heaterOnAbove = newHeaterOnAbove;
    heaterOffBelow = newHeaterOffBelow;
    LOG(LL_INFO, ("Config settings: turn on above=%d, turn off below=%d", heaterOnAbove, heaterOffBelow));
  }
  (void) reported;
  (void) reported_md;
  (void) desired_md;
  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  LOG(LL_INFO, ("Starting Soil Temp Controller v0.2"));

  init = false;
  //mgos_gpio_init();
  for (int i=0; i<PIN_COUNT; i++) {    
  	LOG(LL_INFO,("Initializing GPIO[%d]",i));  
    int pin = D_PINS[i];
    mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_OUTPUT);
    GPIO_REG_WRITE(GPIO_PIN_ADDR(GPIO_ID_PIN(pin)), GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));  
    if (!mgos_gpio_set_pull(pin, MGOS_GPIO_PULL_NONE)) {
      LOG(LL_ERROR, ("Failed to set pull resistor"));
      return MGOS_INIT_APP_INIT_FAILED;
    }
    mgos_gpio_write(pin, 1); 
  }

  if (!mgos_gpio_set_pull(PIN_HEATER_RELAY, MGOS_GPIO_PULL_UP)) {
     LOG(LL_ERROR, ("Failed to pull"));
     return MGOS_INIT_APP_INIT_FAILED;
  }
  mgos_gpio_set_mode(PIN_HEATER_RELAY, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(PIN_HEATER_RELAY, 0); 

  if (!mgos_gpio_set_pull(INPUT_PIN, MGOS_GPIO_PULL_NONE)) {
     LOG(LL_ERROR, ("Failed to pull"));
     return MGOS_INIT_APP_INIT_FAILED;
  }
  mgos_gpio_set_mode(INPUT_PIN, MGOS_GPIO_MODE_INPUT);

  mgos_adc_enable(0);
  mgos_aws_shadow_set_state_handler(aws_shadow_state_handler, NULL);

  mgos_set_timer(POLL_PERIOD, 1, timer_cb, NULL);
  init = true;
  return MGOS_APP_INIT_SUCCESS;
}
