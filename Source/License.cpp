#include "License.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cstdlib>
#include <ctime>
#include <random>

#if JUCE_MAC
 #include <IOKit/IOKitLib.h>
 #include <CoreFoundation/CoreFoundation.h>
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment (lib, "bcrypt.lib")
#else
 #include <sys/stat.h>
#endif

extern "C"
{
   #include "../ThirdParty/monocypher/monocypher-ed25519.h"
}

namespace
{
    const char* serverBase = "https://uraniummusiclicense.vercel.app";

    // Ed25519 public key of the license server (from /api/public-key), pinned here on purpose:
    // fetching it at runtime would let a fake server supply its own key
    const uint8_t serverPublicKey[32] = {
        0xfc, 0x12, 0x59, 0x99, 0xf7, 0x95, 0x6f, 0xda, 0x2a, 0x41, 0xc0, 0xfe, 0x1a, 0x2c, 0x09, 0x68,
        0x08, 0x09, 0xea, 0xd9, 0x5e, 0x44, 0xc7, 0xce, 0x78, 0x84, 0x9a, 0x21, 0xfd, 0x7b, 0x09, 0x1a
    };

    constexpr int requestTimeoutMs = 10000;
    constexpr int maxResponseBytes = 16 * 1024;
    const juce::RelativeTime offlineGrace = juce::RelativeTime::hours (72);
    const juce::RelativeTime clockSkew = juce::RelativeTime::minutes (10);
    const juce::RelativeTime recheckInterval = juce::RelativeTime::seconds (10);   // instances loaded together share one check

    juce::String randomHex (int numBytes)
    {
        juce::HeapBlock<uint8_t> bytes ((size_t) numBytes);
        // always the operating system's CSPRNG
       #if JUCE_MAC || JUCE_IOS
        arc4random_buf (bytes.get(), (size_t) numBytes);
       #elif JUCE_WINDOWS
        if (BCryptGenRandom (nullptr, bytes.get(), (ULONG) numBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            jassertfalse;
       #else
        std::random_device rd;   // /dev/urandom
        for (int i = 0; i < numBytes; ++i) bytes[i] = (uint8_t) (rd() & 0xff);
       #endif
        return juce::String::toHexString (bytes.get(), numBytes, 0);
    }

    bool decodeBase64 (const juce::String& text, juce::MemoryBlock& out)
    {
        juce::MemoryOutputStream mo;
        if (! juce::Base64::convertFromBase64 (mo, text))
            return false;
        out = mo.getMemoryBlock();
        return true;
    }

    juce::String formatDate (juce::Time t) { return t.formatted ("%d %b %Y"); }

    // YYYY-MM-DD in UTC
    juce::String utcDate (juce::Time t)
    {
        const std::time_t secs = (std::time_t) (t.toMilliseconds() / 1000);
        std::tm tm {};
       #if JUCE_WINDOWS
        gmtime_s (&tm, &secs);
       #else
        gmtime_r (&secs, &tm);
       #endif
        char buf[16] {};
        std::strftime (buf, sizeof (buf), "%Y-%m-%d", &tm);
        return buf;
    }

   #if JUCE_MAC
    // the Mac's hardware UUID (IOPlatformUUID), the same value "System Information" shows
    juce::String platformUUID()
    {
        juce::String result;
        const io_service_t service = IOServiceGetMatchingService (MACH_PORT_NULL, IOServiceMatching ("IOPlatformExpertDevice"));
        if (service == 0)
            return result;
        if (auto ref = IORegistryEntryCreateCFProperty (service, CFSTR (kIOPlatformUUIDKey), kCFAllocatorDefault, 0))
        {
            char buf[128] {};
            if (CFGetTypeID (ref) == CFStringGetTypeID()
                && CFStringGetCString ((CFStringRef) ref, buf, sizeof (buf), kCFStringEncodingUTF8))
                result = buf;
            CFRelease (ref);
        }
        IOObjectRelease (service);
        return result;
    }
   #endif
}

#if SANDWAVE_LICENSE_TESTING
juce::String LicenseManager::serverOverride;
juce::File LicenseManager::storageOverride;
const uint8_t* LicenseManager::publicKeyOverride = nullptr;
#endif

//==============================================================================
LicenseManager::LicenseManager() : juce::Thread ("Sandwave license")
{
   #if SANDWAVE_DEV_UNLOCKED
    // local test builds only (cmake -DSANDWAVE_DEV_UNLOCKED=ON), never shipped by the installers
    licensed = true;
    setStatus (State::licensed, "Development build");
    return;
   #endif

    // Restore the last verified state straight away so an activated plugin opens without the
    // activation dialog flashing up. This only trusts a correctly signed response for the stored
    // key that is inside the grace period; the online check that follows can still revoke it.
    const auto storedKey = normaliseKey (juce::JSON::parse (getStorageFile())["key"].toString());
    if (storedKey.isEmpty())
        setStatus (State::needsKey, "Enter your e-mail and license key to activate SANDWAVE.");
    else
        tryOfflineGrace (storedKey, false);

    startThread();
}

LicenseManager::~LicenseManager()
{
    signalThreadShouldExit();
    notify();
    stopThread (requestTimeoutMs + 2000);
}

void LicenseManager::checkAsync()
{
    {
        const juce::ScopedLock sl (lock);
        if (pending.load() || (juce::Time::getCurrentTime() - lastCheck) < recheckInterval)
            return;
        requestedKey.clear();
        requestFromUser = false;
    }
    pending = true;
    notify();
}

void LicenseManager::activateAsync (const juce::String& key, const juce::String& email)
{
    {
        const juce::ScopedLock sl (lock);
        requestedKey = key;
        requestedEmail = email;
        requestFromUser = true;
        status.state = State::checking;
        status.message = "Checking your key...";
    }
    pending = true;
    notify();
}

LicenseManager::Status LicenseManager::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void LicenseManager::run()
{
    while (! threadShouldExit())
    {
        wait (-1);

        while (pending.load() && ! threadShouldExit())
        {
            busy = true;   // set before clearing pending so waiters never see both false mid-request
            pending = false;
            juce::String key, email;
            bool fromUser;
            {
                const juce::ScopedLock sl (lock);
                key = requestedKey;
                email = requestedEmail;
                fromUser = requestFromUser;
                requestedKey.clear();
                requestedEmail.clear();
                requestFromUser = false;
            }

            if (! fromUser)
            {
                const auto stored = juce::JSON::parse (getStorageFile());
                key = stored["key"].toString();
            }

            validate (key, email, fromUser);

            {
                const juce::ScopedLock sl (lock);
                lastCheck = juce::Time::getCurrentTime();
            }
            busy = false;
        }
    }
}

//==============================================================================
void LicenseManager::validate (const juce::String& rawKey, const juce::String& rawEmail, bool fromUser)
{
    const auto key = normaliseKey (rawKey);
    const auto email = rawEmail.trim().toLowerCase();

    if (! isWellFormedKey (key))
    {
        licensed = false;
        setStatus (State::needsKey, fromUser || rawKey.isNotEmpty()
                                        ? "That doesn't look like a license key (URAN-XXXX-XXXX-XXXX-XXXX)."
                                        : "Enter your e-mail and license key to activate SANDWAVE.");
        return;
    }

    if (fromUser && ! isPlausibleEmail (email))
    {
        licensed = false;
        setStatus (State::needsKey, "Please enter the e-mail address the license was issued to.");
        return;
    }

    Verified v;
    juce::String payload, signature, error, checkDate;
    switch (queryServer (key, fromUser ? email : juce::String(), {}, v, payload, signature, error, checkDate))
    {
        case Outcome::ok:
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("key", key);
            if (checkDate.isNotEmpty())
                obj->setProperty ("date", checkDate);   // needed to re-derive the check hash offline
            obj->setProperty ("payload", payload);
            obj->setProperty ("signature", signature);
            auto file = getStorageFile();
            file.getParentDirectory().createDirectory();
            const bool written = file.replaceWithText (juce::JSON::toString (juce::var (obj)));
           #if ! JUCE_WINDOWS
            if (written)
                ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);   // readable by this user only
           #else
            juce::ignoreUnused (written);   // %APPDATA% is already private to the user
           #endif

            licensed = true;
            setStatus (State::licensed, {}, &v);
            return;
        }

        case Outcome::rejected:
        {
            // the server said no in a correctly signed answer: forget any cached approval
            if (! fromUser)
                getStorageFile().deleteFile();

            licensed = false;
            juce::String msg;
            const bool hasDate = v.expires.toMilliseconds() > 0;
            if (v.status == "expired" || (hasDate && v.expires <= juce::Time::getCurrentTime()))
                msg = hasDate ? "This license expired on " + formatDate (v.expires) + ". Please enter a new key."
                              : juce::String ("This license has expired. Please enter a new key.");
            else if (fromUser && (v.status == "invalid" || v.status.isEmpty()))
                msg = "This key and e-mail address don't match a valid license.";
            else if (v.status == "invalid" || v.status.isEmpty())
                msg = "This license key is not valid.";
            else
                msg = "This license is " + v.status + ". Please contact Uranium Music.";
            setStatus (State::needsKey, msg);
            return;
        }

        case Outcome::unreachable:
        default:
            if (! fromUser && tryOfflineGrace (key, true))
                return;

            if (! fromUser || ! licensed.load())
                licensed = false;
            setStatus (licensed.load() ? State::licensed : State::needsKey,
                       "Could not verify the license (" + error + "). Check your internet connection and try again.");
            return;
    }
}

LicenseManager::Outcome LicenseManager::queryServer (const juce::String& key, const juce::String& email, const juce::String& forcedDate,
                                                     Verified& result, juce::String& payload, juce::String& signature,
                                                     juce::String& error, juce::String& usedDate)
{
    const auto nonce = randomHex (16);

    juce::String base = serverBase;
   #if SANDWAVE_LICENSE_TESTING
    if (serverOverride.isNotEmpty()) base = serverOverride;
   #endif

    // first activation: POST /api/verify with key + e-mail + machine id;
    // later checks: GET /lisencecheck/<md5(key + machineId + UTC date + "1.618")>
    const bool activation = email.isNotEmpty();
    juce::String expectedHash;
    juce::URL url;
    if (activation)
    {
        auto* body = new juce::DynamicObject();
        body->setProperty ("key", key);
        body->setProperty ("email", email);
        body->setProperty ("nonce", nonce);
        body->setProperty ("machineId", machineId());
        body->setProperty ("vstName", "SANDWAVE");
        url = juce::URL (base + "/api/verify").withPOSTData (juce::JSON::toString (juce::var (body), true));
    }
    else
    {
        usedDate = forcedDate.isNotEmpty() ? forcedDate : utcDate (juce::Time::getCurrentTime());
        expectedHash = checkHash (key, machineId(), usedDate);
        url = juce::URL (base + "/lisencecheck/" + expectedHash)
                  .withParameter ("nonce", nonce)
                  .withParameter ("machineId", machineId())
                  .withParameter ("vstName", "SANDWAVE");
    }

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions (activation ? juce::URL::ParameterHandling::inPostData
                                                                   : juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (activation ? "Content-Type: application/json" : "")
                             .withConnectionTimeoutMs (requestTimeoutMs)
                             .withNumRedirectsToFollow (0)   // never follow a redirect away from the HTTPS endpoint
                             .withStatusCode (&statusCode)
                             .withProgressCallback ([this] (int, int) { return ! threadShouldExit(); });

    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        error = "no connection";
        return Outcome::unreachable;
    }
    if (statusCode != 200)
    {
        error = "server returned " + juce::String (statusCode);
        return Outcome::unreachable;
    }

    juce::MemoryBlock body;
    stream->readIntoMemoryBlock (body, maxResponseBytes);
    const auto response = juce::JSON::parse (body.toString());

    if (response["alg"].toString() != "Ed25519" || ! response["payload"].isString() || ! response["signature"].isString())
    {
        error = "unexpected response";
        return Outcome::unreachable;
    }

    payload = response["payload"].toString();
    signature = response["signature"].toString();

    if (! verifySigned (payload, signature, key, expectedHash, &nonce, result))
    {
        error = "response failed the security check";
        return Outcome::unreachable;
    }

    // our clock may be on a different UTC day than the server's (wrong clock, or a check right at midnight):
    // ask once more using the day from the server's signed answer
    if (! activation && forcedDate.isEmpty() && ! result.valid && result.serverTime.toMilliseconds() > 0)
    {
        const auto serverDate = utcDate (result.serverTime);
        if (serverDate != usedDate)
            return queryServer (key, email, serverDate, result, payload, signature, error, usedDate);
    }

    return isAccepted (result, juce::Time::getCurrentTime()) ? Outcome::ok : Outcome::rejected;
}

// `valid` decides; a lifetime license has no date to check, any other must be inside its dates
bool LicenseManager::isAccepted (const Verified& v, juce::Time now)
{
    return v.valid && (v.lifetime || (v.expires > v.serverTime && v.expires > now));
}

bool LicenseManager::verifySigned (const juce::String& payload, const juce::String& signatureB64, const juce::String& expectedKey,
                                   const juce::String& expectedHash, const juce::String* expectedNonce, Verified& out) const
{
    juce::MemoryBlock sig;
    if (! decodeBase64 (signatureB64, sig) || sig.getSize() != 64)
        return false;

    const uint8_t* publicKey = serverPublicKey;
   #if SANDWAVE_LICENSE_TESTING
    if (publicKeyOverride != nullptr) publicKey = publicKeyOverride;
   #endif

    const auto utf8 = payload.toUTF8();
    if (crypto_ed25519_check (static_cast<const uint8_t*> (sig.getData()), publicKey,
                              reinterpret_cast<const uint8_t*> (utf8.getAddress()), utf8.sizeInBytes() - 1) != 0)
        return false;

    // the signature is genuine; now make sure it is about this key, this machine and (online) this request
    // (/lisencecheck answers name the key by the check hash we sent, /api/verify by the key itself)
    const auto p = juce::JSON::parse (payload);
    const bool sameKey = (expectedHash.isNotEmpty() && p["hash"].toString() == expectedHash)
                         || (p["key"].isString() && normaliseKey (p["key"].toString()) == expectedKey);
    if (! sameKey)
        return false;
    if (expectedNonce != nullptr && p["nonce"].toString() != *expectedNonce)
        return false;

    out.valid = p["valid"].isBool() && (bool) p["valid"];
    if (out.valid && p["machineId"].toString() != machineId())   // an approval must be for this machine
        return false;
    out.status = p["status"].toString();
    out.licensee = p["name"].toString();
    out.lifetime = p["lifetime"].isBool() && (bool) p["lifetime"];
    out.expires = ! out.lifetime && p["expiresAt"].isString() ? juce::Time::fromISO8601 (p["expiresAt"].toString()) : juce::Time();
    out.serverTime = juce::Time::fromISO8601 (p["serverTime"].toString());
    return true;
}

bool LicenseManager::tryOfflineGrace (const juce::String& key, bool reportOffline)
{
    const auto stored = juce::JSON::parse (getStorageFile());
    if (normaliseKey (stored["key"].toString()) != key)
        return false;

    Verified v;
    const auto date = stored["date"].toString();
    const auto expectedHash = date.isNotEmpty() ? checkHash (key, machineId(), date) : juce::String();
    if (! verifySigned (stored["payload"].toString(), stored["signature"].toString(), key, expectedHash, nullptr, v))
        return false;

    const auto now = juce::Time::getCurrentTime();
    const bool ok = isAccepted (v, now)
                    && now >= v.serverTime - clockSkew             // clock set back before the last check
                    && now - v.serverTime <= offlineGrace;          // last online check is recent enough
    if (! ok)
        return false;

    licensed = true;
    if (! reportOffline)
    {
        setStatus (State::licensed, {}, &v);
        return true;
    }
    setStatus (State::licensed, "Offline: verified " + formatDate (v.serverTime) + ", connect within "
                                    + juce::String (juce::roundToInt (offlineGrace.inHours())) + " hours of that.", &v, true);
    return true;
}

//==============================================================================
void LicenseManager::setStatus (State state, const juce::String& message, const Verified* v, bool offline)
{
    const juce::ScopedLock sl (lock);
    status.state = state;
    status.message = message;
    status.offline = offline;
    if (v != nullptr)
    {
        status.licensee = v->licensee;
        status.expires = v->expires;
        status.lifetime = v->lifetime;
    }
    else if (state != State::licensed)
    {
        status.licensee.clear();
        status.expires = {};
        status.lifetime = false;
    }
}

juce::File LicenseManager::getStorageFile()
{
   #if SANDWAVE_LICENSE_TESTING
    if (storageOverride != juce::File()) return storageOverride.getChildFile ("license.json");
   #endif
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/Uranium Music/Sandwave/license.json");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)   // %APPDATA% on Windows
               .getChildFile ("Uranium Music/Sandwave/license.json");
   #endif
}

juce::String LicenseManager::md5Hex (const juce::String& key)
{
    return juce::MD5 (key.toUTF8()).toHexString();
}

juce::String LicenseManager::checkHash (const juce::String& key, const juce::String& machine, const juce::String& date)
{
    return md5Hex (key + machine + date + "1.618");
}

juce::String LicenseManager::machineId()
{
    static const juce::String id = []
    {
        juce::String uuid;
       #if JUCE_MAC
        uuid = platformUUID();
       #elif JUCE_WINDOWS
        // Windows' own machine id, set when the OS is installed
        uuid = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid",
                                                {}, juce::WindowsRegistry::WoW64_64bit);
       #endif
        if (uuid.isEmpty())
            uuid = juce::SystemStats::getUniqueDeviceID();
        return uuid;
    }();
    return id;
}

juce::String LicenseManager::normaliseKey (const juce::String& key)
{
    return key.trim().toUpperCase().removeCharacters (" \t\r\n");
}

bool LicenseManager::isPlausibleEmail (const juce::String& email)
{
    const auto at = email.indexOfChar ('@');
    return email.length() <= 254 && at > 0 && email.lastIndexOfChar ('@') == at
           && email.substring (at + 1).containsChar ('.') && ! email.endsWithChar ('.')
           && ! email.containsAnyOf (" \t\r\n<>\"");
}

bool LicenseManager::isWellFormedKey (const juce::String& key)
{
    // URAN-XXXX-XXXX-XXXX-XXXX
    const auto parts = juce::StringArray::fromTokens (key, "-", "");
    if (parts.size() != 5 || parts[0] != "URAN")
        return false;
    for (int i = 1; i < 5; ++i)
        if (parts[i].length() != 4 || ! parts[i].containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"))
            return false;
    return true;
}
