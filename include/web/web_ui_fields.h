#pragma once
#if defined(ARDUINO)
#include <Arduino.h>
#include "web/web_ui_escape.h"

namespace web_ui {

// Begin a card with optional heading
inline void card_begin(String &html, const char *heading = nullptr) {
  html += F("<div class=\"card\">");
  if (heading && heading[0]) {
    html += F("<h3>");
    html += heading;
    html += F("</h3>");
  }
}

inline void card_end(String &html) {
  html += F("</div>");
}

// Begin a form (AJAX submission)
inline void form_begin(String &html) {
  html += F("<form onsubmit=\"return submitForm(this)\">");
}

// End a form with a save button
inline void form_end(String &html, const char *button_label = "Save") {
  html += F("<div class=\"mt\"><button type=\"submit\" class=\"btn btn-p\">");
  html += button_label;
  html += F("</button></div></form>");
}

// Text input field
inline void text_field(String &html, const char *label, const char *name,
                        const String &value, const char *hint = nullptr,
                        const char *pattern = nullptr, const char *title = nullptr,
                        int maxlength = 0) {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><input name=\"");
  html += name;
  html += F("\" value=\"");
  html += html_escape(value);
  html += '"';
  if (pattern) {
    html += F(" pattern=\"");
    html += pattern;
    html += '"';
  }
  if (title) {
    html += F(" title=\"");
    html += title;
    html += '"';
  }
  if (maxlength > 0) {
    html += F(" maxlength=\"");
    html += String(maxlength);
    html += '"';
  }
  html += F(">");
  if (hint) {
    html += F("<div class=\"ht\">");
    html += hint;
    html += F("</div>");
  }
  html += F("</div>");
}

// Password field (never shows stored value)
inline void password_field(String &html, const char *label, const char *name,
                            bool is_set = false, const char *pattern = nullptr,
                            const char *title = nullptr) {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><input type=\"password\" name=\"");
  html += name;
  html += F("\" value=\"\"");
  if (pattern) {
    html += F(" pattern=\"");
    html += pattern;
    html += '"';
  }
  if (title) {
    html += F(" title=\"");
    html += title;
    html += '"';
  }
  html += F("><div class=\"ht\">");
  html += is_set ? "Currently set. Leave blank to keep." : "Not set.";
  html += F("</div></div>");
}

// Number input field
inline void number_field(String &html, const char *label, const char *name,
                          const String &value, const char *min_val = nullptr,
                          const char *max_val = nullptr, const char *step = nullptr,
                          const char *hint = nullptr) {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><input type=\"number\" name=\"");
  html += name;
  html += F("\" value=\"");
  html += html_escape(value);
  html += '"';
  if (min_val) { html += F(" min=\""); html += min_val; html += '"'; }
  if (max_val) { html += F(" max=\""); html += max_val; html += '"'; }
  if (step) { html += F(" step=\""); html += step; html += '"'; }
  html += F(">");
  if (hint) {
    html += F("<div class=\"ht\">");
    html += hint;
    html += F("</div>");
  }
  html += F("</div>");
}

// MAC address field with pattern validation and device picker
inline void mac_field(String &html, const char *label, const char *name,
                       const String &value, const char *hint = nullptr) {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><div style=\"display:flex;gap:0.4rem\">"
            "<input name=\"");
  html += name;
  html += F("\" value=\"");
  html += html_escape(value);
  html += F("\" list=\"dl-");
  html += name;
  html += F("\" pattern=\"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$\""
            " title=\"Format: AA:BB:CC:DD:EE:FF\""
            " style=\"flex:1\">"
            "<button type=\"button\" class=\"btn btn-p\" style=\"padding:0.45rem 0.6rem\""
            " onclick=\"pickMac('");
  html += name;
  html += F("')\">&#9660;</button></div>"
            "<datalist id=\"dl-");
  html += name;
  html += F("\"></datalist>"
            "<div class=\"ht\">");
  html += hint ? hint : "Format: AA:BB:CC:DD:EE:FF";
  html += F("</div></div>");
}

// Select dropdown
struct SelectOption {
  const char *value;
  const char *label;
};

inline void select_field(String &html, const char *label, const char *name,
                          const SelectOption *options, size_t count,
                          const String &selected, const char *hint = nullptr) {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><select name=\"");
  html += name;
  html += F("\">");
  for (size_t i = 0; i < count; ++i) {
    html += F("<option value=\"");
    html += options[i].value;
    html += '"';
    if (selected == options[i].value) {
      html += F(" selected");
    }
    html += '>';
    html += options[i].label;
    html += F("</option>");
  }
  html += F("</select>");
  if (hint) {
    html += F("<div class=\"ht\">");
    html += hint;
    html += F("</div>");
  }
  html += F("</div>");
}

// Status item for the status grid
inline void status_item(String &html, const char *label, const char *id,
                         const char *initial_value = "-") {
  html += F("<div class=\"si\"><div class=\"sl\">");
  html += label;
  html += F("</div><div class=\"sv\" id=\"st-");
  html += id;
  html += F("\">");
  html += initial_value;
  html += F("</div></div>");
}

// Section heading inside a status grid (spans full width)
inline void status_section(String &html, const char *heading) {
  html += F("<div style=\"grid-column:1/-1;margin-top:0.4rem\">"
            "<div class=\"sl\" style=\"border-bottom:1px solid var(--bd);padding-bottom:0.2rem\">");
  html += heading;
  html += F("</div></div>");
}

// Status grid container
inline void status_grid_begin(String &html) {
  html += F("<div class=\"sg\">");
}

inline void status_grid_end(String &html) {
  html += F("</div>");
}

// Reboot button (as a standalone POST form)
inline void reboot_button(String &html, const char *label = "Reboot Device",
                           const char *action = "/reboot") {
  html += F("<form method=\"post\" action=\"");
  html += action;
  html += F("\"><button type=\"submit\" class=\"btn btn-d\" "
            "onclick=\"return confirm('Reboot now?')\">");
  html += label;
  html += F("</button></form>");
}

// Hidden field
inline void hidden_field(String &html, const char *name, const char *value) {
  html += F("<input type=\"hidden\" name=\"");
  html += name;
  html += F("\" value=\"");
  html += value;
  html += F("\">");
}

// File upload for OTA
inline void file_upload(String &html, const char *label = "Firmware File",
                         const char *name = "firmware") {
  html += F("<div class=\"fg\"><label>");
  html += label;
  html += F("</label><input type=\"file\" name=\"");
  html += name;
  html += F("\" accept=\".bin\" style=\"padding:0.3rem\">");
  html += F("</div>");
}

}  // namespace web_ui
#endif  // ARDUINO
