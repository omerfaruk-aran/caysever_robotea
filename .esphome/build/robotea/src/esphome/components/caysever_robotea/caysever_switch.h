#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome
{
    namespace caysever_robotea
    {

        class CayseverRoboteaSwitch : public switch_::Switch, public Component
        {
        protected:
            void write_state(bool state) override { this->publish_state(state); }
        };

    } // namespace caysever_robotea
} // namespace esphome