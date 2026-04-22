/**
 * @file admin_auth.cpp
 * @brief HTTP Basic Authentication implementation
 */

#include "admin_auth.h"
#include <Preferences.h>
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "Auth";
static constexpr const char* PW_KEY = "pw";

AdminAuthManager& AdminAuth = AdminAuthManager::getInstance();

AdminAuthManager& AdminAuthManager::getInstance() {
    static AdminAuthManager instance;
    return instance;
}

void AdminAuthManager::begin() {
    Preferences prefs;
    if (!prefs.begin(IWMP_AUTH_NVS_NAMESPACE, true /* read-only */)) {
        // Namespace may not exist yet on a fresh device — that's fine,
        // it means open mode.
        _password = "";
        LOG_I(TAG, "No auth namespace yet — open mode");
        return;
    }
    _password = prefs.getString(PW_KEY, "");
    prefs.end();

    if (_password.isEmpty()) {
        LOG_I(TAG, "No admin password set — open mode");
    } else {
        LOG_I(TAG, "Admin password loaded — auth required for writes");
    }
}

bool AdminAuthManager::require(AsyncWebServerRequest* request) {
    if (!request) return false;
    if (_password.isEmpty()) return true;  // open mode

    // AsyncWebServer's authenticate() expects (user, pass). We use an
    // empty username — browsers display the realm name and accept the
    // prompt with a blank user field.
    if (request->authenticate("", _password.c_str())) {
        return true;
    }

    request->requestAuthentication(IWMP_AUTH_REALM);
    return false;
}

bool AdminAuthManager::check(AsyncWebServerRequest* request) const {
    if (!request) return false;
    if (_password.isEmpty()) return true;
    return request->authenticate("", _password.c_str());
}

bool AdminAuthManager::verify(const String& password) const {
    if (_password.isEmpty()) return password.isEmpty();
    return _password == password;
}

bool AdminAuthManager::set(const String& new_password) {
    Preferences prefs;
    if (!prefs.begin(IWMP_AUTH_NVS_NAMESPACE, false /* read-write */)) {
        LOG_E(TAG, "Failed to open auth namespace for write");
        return false;
    }
    bool ok;
    if (new_password.isEmpty()) {
        ok = prefs.remove(PW_KEY);
    } else {
        ok = (prefs.putString(PW_KEY, new_password) > 0);
    }
    prefs.end();

    if (ok) {
        _password = new_password;
        LOG_I(TAG, "Admin password %s", new_password.isEmpty() ? "cleared (open mode)" : "updated");
    } else {
        LOG_E(TAG, "Failed to persist admin password");
    }
    return ok;
}

void AdminAuthManager::clear() {
    set("");
}

} // namespace iwmp
