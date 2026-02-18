#pragma once

#include <string>

namespace thermostat {
namespace management_paths {

bool parse_cfg_set_topic(const std::string &base_topic, const std::string &topic,
                         std::string *key_out);
bool parse_cfg_state_topic(const std::string &base_topic, const std::string &topic,
                           std::string *key_out);
bool parse_prefixed_form_key(const std::string &name, const std::string &prefix,
                             std::string *key_out);

}  // namespace management_paths
}  // namespace thermostat
