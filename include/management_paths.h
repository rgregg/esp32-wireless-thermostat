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
bool is_secret_cfg_key(const std::string &key);
// True for a secret key posted with an empty (or null) value. The config forms render
// secrets as blank password inputs ("Leave blank to keep") and the browser posts the
// blank, so the web /config handlers skip these instead of wiping the stored secret.
bool is_blank_secret_form_value(const std::string &key, const char *value);

}  // namespace management_paths
}  // namespace thermostat
