#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

// License checking against the Uranium Music license server.
//
// Every response is verified before it is trusted:
//  - Ed25519 signature checked against the public key compiled into the plugin
//  - the signed payload must echo our key hash, a fresh random nonce (no replays) and this machine's id
//  - the license must be valid and, unless it is a lifetime license, not past its expiry date
// A copy of the last good signed response allows a short offline grace period.
//
// One instance is shared by every plugin instance in the host (juce::SharedResourcePointer).
class LicenseManager : private juce::Thread
{
public:
    enum class State { checking, licensed, needsKey };

    struct Status
    {
        State state = State::checking;
        juce::String message;    // shown in the activation dialog
        juce::String licensee;   // name from the signed payload
        juce::Time expires;      // unset for a lifetime license
        bool lifetime = false;
        bool offline = false;    // licensed from the cached response
    };

    LicenseManager();
    ~LicenseManager() override;

    void checkAsync();                              // re-validate the stored key (called on every plugin load)
    // first activation: key + the e-mail the license was issued to, checked with POST /api/verify
    void activateAsync (const juce::String& key, const juce::String& email);

    bool isLicensed() const noexcept { return licensed.load(); }   // safe to call from the audio thread
    Status getStatus() const;

    static juce::String normaliseKey (const juce::String& key);
    static bool isPlausibleEmail (const juce::String& email);

    // system machine id (the Mac's hardware UUID) and the /lisencecheck hash: md5 (key + machineId + UTC date + "1.618")
    static juce::String machineId();
    static juce::String checkHash (const juce::String& key, const juce::String& machine, const juce::String& date);
    static bool isWellFormedKey (const juce::String& key);

   #if SANDWAVE_LICENSE_TESTING
    // test hooks: point the manager at another server / storage folder / signing key
    static juce::String serverOverride;
    static juce::File storageOverride;
    static const uint8_t* publicKeyOverride;
    void waitUntilIdle() { juce::Thread::sleep (50); while (pending.load() || busy.load()) juce::Thread::sleep (20); }
    void forceStateForTesting (bool isLicensed, const juce::String& message)
    {
        licensed = isLicensed;
        setStatus (isLicensed ? State::licensed : State::needsKey, message);
    }
   #endif

private:
    struct Verified
    {
        bool valid = false, lifetime = false;
        juce::String status, licensee;
        juce::Time expires, serverTime;
    };

    static bool isAccepted (const Verified&, juce::Time now);

    enum class Outcome { ok, rejected, unreachable };

    void run() override;
    void validate (const juce::String& key, const juce::String& email, bool fromUser);
    Outcome queryServer (const juce::String& key, const juce::String& email, const juce::String& forcedDate, Verified& result,
                         juce::String& payload, juce::String& signature, juce::String& error, juce::String& usedDate);
    bool verifySigned (const juce::String& payload, const juce::String& signatureB64, const juce::String& expectedKey,
                       const juce::String& expectedHash,
                       const juce::String* expectedNonce, Verified& out) const;
    bool tryOfflineGrace (const juce::String& key, bool reportOffline);

    void setStatus (State, const juce::String& message, const Verified* v = nullptr, bool offline = false);

    static juce::File getStorageFile();
    static juce::String md5Hex (const juce::String& key);


    std::atomic<bool> licensed { false };
    std::atomic<bool> pending { false };
    std::atomic<bool> busy { false };

    juce::CriticalSection lock;
    Status status;
    juce::String requestedKey;     // key to check on the worker thread (empty = stored key)
    juce::String requestedEmail;
    bool requestFromUser = false;
    juce::Time lastCheck;
};
