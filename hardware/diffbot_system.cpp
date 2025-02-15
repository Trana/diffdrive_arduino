// Copyright 2021 ros2_control Development Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "diffdrive_arduino/diffbot_system.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include "hardware_interface/lexical_casts.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace diffdrive_arduino
{
  hardware_interface::CallbackReturn DiffBotArduinoHardware::on_init(
      const hardware_interface::HardwareInfo &info)
  {
    if (
        hardware_interface::SystemInterface::on_init(info) !=
        hardware_interface::CallbackReturn::SUCCESS)
    {
      return hardware_interface::CallbackReturn::ERROR;
    }

    cfg_.left_wheel_name = info_.hardware_parameters["left_wheel_name"];
    cfg_.right_wheel_name = info_.hardware_parameters["right_wheel_name"];
    cfg_.loop_rate = std::stof(info_.hardware_parameters["loop_rate"]);
    cfg_.device = info_.hardware_parameters["device"];
    cfg_.baud_rate = std::stoi(info_.hardware_parameters["baud_rate"]);
    cfg_.timeout_ms = std::stoi(info_.hardware_parameters["timeout_ms"]);
    cfg_.left_wheel_enc_counts_per_rev = std::stoi(info_.hardware_parameters["left_wheel_enc_counts_per_rev"]);
    cfg_.right_wheel_enc_counts_per_rev = std::stoi(info_.hardware_parameters["right_wheel_enc_counts_per_rev"]);

    if (info_.hardware_parameters.count("pid_p") > 0)
    {
      cfg_.pid_p = std::stoi(info_.hardware_parameters["pid_p"]);
      cfg_.pid_d = std::stoi(info_.hardware_parameters["pid_d"]);
      cfg_.pid_i = std::stoi(info_.hardware_parameters["pid_i"]);
      cfg_.pid_o = std::stoi(info_.hardware_parameters["pid_o"]);
    }
    else
    {
      RCLCPP_INFO(rclcpp::get_logger("DiffDriveArduinoHardware"), "PID values not supplied, using defaults.");
    }

    wheel_l_.setup(cfg_.left_wheel_name, cfg_.left_wheel_enc_counts_per_rev);
    wheel_r_.setup(cfg_.right_wheel_name, cfg_.right_wheel_enc_counts_per_rev);

    for (const hardware_interface::ComponentInfo &joint : info_.joints)
    {
      // DiffBotSystem has exactly two states and one command interface on each joint
      if (joint.command_interfaces.size() != 1)
      {
        RCLCPP_FATAL(
            rclcpp::get_logger("DiffBotArduinoHardware"),
            "Joint '%s' has %zu command interfaces found. 1 expected.", joint.name.c_str(),
            joint.command_interfaces.size());
        return hardware_interface::CallbackReturn::ERROR;
      }

      if (joint.command_interfaces[0].name != hardware_interface::HW_IF_VELOCITY)
      {
        RCLCPP_FATAL(
            rclcpp::get_logger("DiffBotArduinoHardware"),
            "Joint '%s' have %s command interfaces found. '%s' expected.", joint.name.c_str(),
            joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_VELOCITY);
        return hardware_interface::CallbackReturn::ERROR;
      }

      if (joint.state_interfaces.size() != 2)
      {
        RCLCPP_FATAL(
            rclcpp::get_logger("DiffBotArduinoHardware"),
            "Joint '%s' has %zu state interface. 2 expected.", joint.name.c_str(),
            joint.state_interfaces.size());
        return hardware_interface::CallbackReturn::ERROR;
      }

      if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION)
      {
        RCLCPP_FATAL(
            rclcpp::get_logger("DiffBotArduinoHardware"),
            "Joint '%s' have '%s' as first state interface. '%s' expected.", joint.name.c_str(),
            joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION);
        return hardware_interface::CallbackReturn::ERROR;
      }

      if (joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY)
      {
        RCLCPP_FATAL(
            rclcpp::get_logger("DiffBotArduinoHardware"),
            "Joint '%s' have '%s' as second state interface. '%s' expected.", joint.name.c_str(),
            joint.state_interfaces[1].name.c_str(), hardware_interface::HW_IF_VELOCITY);
        return hardware_interface::CallbackReturn::ERROR;
      }
    }

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  std::vector<hardware_interface::StateInterface> DiffBotArduinoHardware::export_state_interfaces()
  {
    std::vector<hardware_interface::StateInterface> state_interfaces;

    state_interfaces.emplace_back(hardware_interface::StateInterface(
        wheel_l_.name, hardware_interface::HW_IF_POSITION, &wheel_l_.pos));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        wheel_l_.name, hardware_interface::HW_IF_VELOCITY, &wheel_l_.vel));

    state_interfaces.emplace_back(hardware_interface::StateInterface(
        wheel_r_.name, hardware_interface::HW_IF_POSITION, &wheel_r_.pos));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        wheel_r_.name, hardware_interface::HW_IF_VELOCITY, &wheel_r_.vel));

    return state_interfaces;
  }

  std::vector<hardware_interface::CommandInterface> DiffBotArduinoHardware::export_command_interfaces()
  {
    std::vector<hardware_interface::CommandInterface> command_interfaces;

    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        wheel_l_.name, hardware_interface::HW_IF_VELOCITY, &wheel_l_.cmd));
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        wheel_r_.name, hardware_interface::HW_IF_VELOCITY, &wheel_r_.cmd));

    return command_interfaces;
  }

  hardware_interface::CallbackReturn DiffBotArduinoHardware::on_configure(
      const rclcpp_lifecycle::State & /*previous_state*/)
  {
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Configuring ...please wait...");
    comms_.connect(cfg_.device, cfg_.baud_rate, cfg_.timeout_ms);
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Successfully configured!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn DiffBotArduinoHardware::on_cleanup(
      const rclcpp_lifecycle::State & /*previous_state*/)
  {
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Cleaning up ...please wait...");
    comms_.disconnect();
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Successfully cleaned up!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn DiffBotArduinoHardware::on_activate(
      const rclcpp_lifecycle::State & /*previous_state*/)
  {
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Activating ...please wait...");
    if (cfg_.pid_p > 0)
    {
      comms_.set_pid_values(cfg_.pid_p, cfg_.pid_d, cfg_.pid_i, cfg_.pid_o);
    }
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Successfully activated!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn DiffBotArduinoHardware::on_deactivate(
      const rclcpp_lifecycle::State & /*previous_state*/)
  {
    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Deactivating ...please wait...");

    RCLCPP_INFO(rclcpp::get_logger("DiffBotArduinoHardware"), "Successfully deactivated!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type DiffBotArduinoHardware::read(
      const rclcpp::Time & /*time*/, const rclcpp::Duration &period)
  {

    comms_.read_encoder_values(wheel_l_.enc, wheel_r_.enc);

    double delta_seconds = period.seconds();

    double pos_prev = wheel_l_.pos;
    wheel_l_.pos = wheel_l_.calc_enc_angle();
    wheel_l_.vel = (wheel_l_.pos - pos_prev) / delta_seconds;

    pos_prev = wheel_r_.pos;
    wheel_r_.pos = wheel_r_.calc_enc_angle();
    wheel_r_.vel = (wheel_r_.pos - pos_prev) / delta_seconds;

    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type diffdrive_arduino ::DiffBotArduinoHardware::write(
      const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
  {
    int motor_l_counts_per_loop = wheel_l_.cmd / wheel_l_.rads_per_count / cfg_.loop_rate;
    int motor_r_counts_per_loop = wheel_r_.cmd / wheel_r_.rads_per_count / cfg_.loop_rate;

    comms_.set_motor_values(motor_l_counts_per_loop, motor_r_counts_per_loop);
    return hardware_interface::return_type::OK;
  }

} // namespace diffdrive_arduino

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(
    diffdrive_arduino::DiffBotArduinoHardware, hardware_interface::SystemInterface)
