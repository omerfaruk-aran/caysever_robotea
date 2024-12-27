#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

namespace esphome
{
    namespace caysever_robotea
    {

        class CayseverRoboteaSelect : public select::Select, public Component
        {
        protected:
            void control(const std::string &value) override { this->publish_state(value); }
        };

    } // namespace caysever_robotea
} // namespace esphome