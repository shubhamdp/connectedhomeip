/*
 *    Copyright (c) 2024 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "LEDWidget.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

static LEDWidget::LEDWidgetStateUpdateHandler sStateUpdateCallback;

void LEDWidget::SetStateUpdateCallback(LEDWidgetStateUpdateHandler stateUpdateCb)
{
    if (stateUpdateCb)
        sStateUpdateCallback = stateUpdateCb;
}

int LEDWidget::Init(const struct gpio_dt_spec * gpioSpec)
{
    mBlinkOnTimeMS  = 0;
    mBlinkOffTimeMS = 0;
    mGpioSpec       = gpioSpec;
    mState          = false;
    mInitialized    = false;

    if (!gpio_is_ready_dt(mGpioSpec))
    {
        LOG_ERR("LED GPIO device not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(mGpioSpec, GPIO_OUTPUT_INACTIVE);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LED GPIO: %d", ret);
        return ret;
    }

    k_timer_init(&mLedTimer, &LEDWidget::LedStateTimerHandler, nullptr);
    k_timer_user_data_set(&mLedTimer, this);

    mInitialized = true;
    Set(false);
    return 0;
}

void LEDWidget::Invert(void)
{
    Set(!mState);
}

void LEDWidget::Set(bool state)
{
    k_timer_stop(&mLedTimer);
    mBlinkOnTimeMS = mBlinkOffTimeMS = 0;
    DoSet(state);
}

void LEDWidget::Blink(uint32_t changeRateMS)
{
    Blink(changeRateMS, changeRateMS);
}

void LEDWidget::Blink(uint32_t onTimeMS, uint32_t offTimeMS)
{
    k_timer_stop(&mLedTimer);

    mBlinkOnTimeMS  = onTimeMS;
    mBlinkOffTimeMS = offTimeMS;

    if (mBlinkOnTimeMS != 0 && mBlinkOffTimeMS != 0)
    {
        DoSet(!mState);
        ScheduleStateChange();
    }
}

void LEDWidget::ScheduleStateChange()
{
    k_timer_start(&mLedTimer, K_MSEC(mState ? mBlinkOnTimeMS : mBlinkOffTimeMS), K_NO_WAIT);
}

void LEDWidget::DoSet(bool state)
{
    mState = state;
    if (mInitialized)
    {
        gpio_pin_set_dt(mGpioSpec, state ? 1 : 0);
    }
}

void LEDWidget::UpdateState()
{
    if (mBlinkOnTimeMS != 0 && mBlinkOffTimeMS != 0)
    {
        DoSet(!mState);
        ScheduleStateChange();
    }
}

void LEDWidget::LedStateTimerHandler(k_timer * timer)
{
    if (sStateUpdateCallback)
        sStateUpdateCallback(*reinterpret_cast<LEDWidget *>(timer->user_data));
}
