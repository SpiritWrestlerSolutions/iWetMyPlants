/**
 * @file admin_auth.h
 * @brief HTTP Basic Authentication for the admin web surface
 *
 * Single-password model — no usernames. The password is stored in
 * Preferences under namespace IWMP_AUTH_NVS_NAMESPACE / key "pw".
 * Storage is plaintext NVS (which lives in the device's encrypted flash
 * region when flash encryption is enabled, but is plaintext otherwise);
 * the threat model is LAN drive-by, not a physical-access attacker.
 *
 * Backward-compat: if no password has been set, isEnabled() returns
 * false and require() always returns true. Existing in-field devices
 * keep working as open until the user opts in via the Settings UI.
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace iwmp {

static constexpr const char* IWMP_AUTH_NVS_NAMESPACE = "iwmp_auth";
static constexpr const char* IWMP_AUTH_REALM         = "iWetMyPlants";

class AdminAuthManager {
public:
    static AdminAuthManager& getInstance();

    AdminAuthManager(const AdminAuthManager&) = delete;
    AdminAuthManager& operator=(const AdminAuthManager&) = delete;

    /// Load the saved password from NVS. Call once at boot.
    void begin();

    /// True if a non-empty admin password is currently set.
    bool isEnabled() const { return !_password.isEmpty(); }

    /**
     * Verify a request's Basic-Auth header. If no password is configured
     * (open mode) returns true. Otherwise returns true only when the
     * client presented matching credentials. On failure, sends 401 with
     * a WWW-Authenticate challenge so the browser will prompt.
     *
     * Idiomatic call site:
     *   server.on("/api/foo", HTTP_POST, [](AsyncWebServerRequest* req) {
     *       if (!AdminAuth.require(req)) return;
     *       // ... handler body
     *   });
     */
    bool require(AsyncWebServerRequest* request);

    /// Same as require() but exempt for read-only reuse (e.g., to test
    /// state from inside a JSON builder). Does NOT send 401.
    bool check(AsyncWebServerRequest* request) const;

    /// Direct verification without an HTTP context (e.g., for the
    /// "change password" endpoint comparing the supplied current pw).
    bool verify(const String& password) const;

    /// Persist a new password and switch to enabled mode. Empty string
    /// disables auth (callers should usually go through clear()).
    bool set(const String& new_password);

    /// Disable auth (clears the stored password).
    void clear();

private:
    AdminAuthManager() = default;
    ~AdminAuthManager() = default;

    String _password;  // empty = open mode
};

extern AdminAuthManager& AdminAuth;

} // namespace iwmp
