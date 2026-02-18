#include "management_paths.h"

namespace thermostat {
namespace management_paths {
namespace {

bool starts_with(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parse_cfg_topic(const std::string &base_topic, const std::string &topic,
                     const std::string &tail, std::string *key_out) {
  if (key_out == nullptr || base_topic.empty() || topic.empty()) {
    return false;
  }
  const std::string prefix = base_topic + "/cfg/";
  if (!starts_with(topic, prefix) || !ends_with(topic, tail)) {
    return false;
  }
  if (topic.size() <= prefix.size() + tail.size()) {
    return false;
  }
  *key_out = topic.substr(prefix.size(), topic.size() - prefix.size() - tail.size());
  return !key_out->empty();
}

}  // namespace

bool parse_cfg_set_topic(const std::string &base_topic, const std::string &topic,
                         std::string *key_out) {
  return parse_cfg_topic(base_topic, topic, "/set", key_out);
}

bool parse_cfg_state_topic(const std::string &base_topic, const std::string &topic,
                           std::string *key_out) {
  return parse_cfg_topic(base_topic, topic, "/state", key_out);
}

bool parse_prefixed_form_key(const std::string &name, const std::string &prefix,
                             std::string *key_out) {
  if (key_out == nullptr || prefix.empty() || name.size() <= prefix.size() ||
      !starts_with(name, prefix)) {
    return false;
  }
  *key_out = name.substr(prefix.size());
  return !key_out->empty();
}

}  // namespace management_paths
}  // namespace thermostat
