#if defined(ARDUINO)
#include "web/web_auth.h"

// Definitions of the web_auth state variables declared in web_auth.h.
// Having a single definition here prevents ODR violations that would occur
// if the state were declared `static` in the header and multiple translation
// units included it.
namespace web_auth {
  char s_pwd_hash[65] = {};
  char s_session_token[65] = {};
  uint32_t s_session_expiry_ms = 0;
}

#endif  // ARDUINO
