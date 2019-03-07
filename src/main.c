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

// #include <stdio.h>
// #include "common/platform.h" 
// #include "common/mbuf.h"
#include "eagle_soc.h"
#include "mgos.h"
#include "mgos_adc.h"
#include "mgos_mqtt.h"
#include "mgos_gpio.h"
#include "mgos_hal.h"
#include "mgos_gpio_hal.h"
#include "mgos_timers.h"
#include "frozen/frozen.h"
// #include "mgos_sys_config.h"
// #include "mgos_mongoose.h"
// #include "mgos_hal.h"
// #include "mgos_app.h"
// #include "mgos_wifi.h"
// #include "mgos_aws_shadow.h"
// #include "mgos_http_server.h"

#define PIN_THERM_DRIVE 4 //D1
#define POLL_PERIOD 5 * 60000

int D_PINS[] = {PIN_THERM_DRIVE};
int INPUT_PIN = 14;
int PIN_COUNT = 1;
bool init = false;
int NUMSAMPLES = 5;

#define GPIO_PIN_ADDR(i)        (GPIO_PIN0_ADDRESS + i*4)


static int readVoltage(void) {
  int i;
  int samples[NUMSAMPLES];
  int average;
 
  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = mgos_adc_read(0);
   	mgos_msleep(5);
  }
 
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  // LOG(LL_INFO, ("Total ADC value: %d", average));
  return average;
}
//t:2666}
static void measureTemp() {
	int voltage = 0;

	mgos_gpio_write(PIN_THERM_DRIVE, 0);
	mgos_msleep(1);
	voltage = readVoltage();
	mgos_gpio_write(PIN_THERM_DRIVE, 1);

	char topic[34];
	sprintf(topic,"devices/%s/temperature", mgos_sys_config_get_device_id());
	// LOG(LL_INFO, ("Published '%s'[%d] to topic '%s'",buffer, size, topic));
	mgos_mqtt_pubf(topic, 0, false, "{ t: %d }", voltage);   /* Publish */
}

void timer_cb(void *arg) {
	measureTemp();
	(void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  LOG(LL_INFO, ("Hi there"));

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
  if (!mgos_gpio_set_pull(INPUT_PIN, MGOS_GPIO_PULL_NONE)) {
     LOG(LL_ERROR, ("Failed to pull"));
     return MGOS_INIT_APP_INIT_FAILED;
  }
  mgos_gpio_set_mode(INPUT_PIN, MGOS_GPIO_MODE_INPUT);

  mgos_adc_enable(0);
  // mgos_aws_shadow_set_state_handler(aws_shadow_state_handler, NULL);

  mgos_set_timer(POLL_PERIOD, 1, timer_cb, NULL);
  //Set a short timer so the temperature is uploaded right away (no repeat)
  mgos_set_timer(1000, 0, timer_cb, NULL);
  init = true;
  return MGOS_APP_INIT_SUCCESS;
}
