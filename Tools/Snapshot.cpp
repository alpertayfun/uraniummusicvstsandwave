// Renders the plugin editor to PNG files and tests the analysis: snapshot <out-dir>  |  snapshot --license-test <work-dir>
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <sys/stat.h>
#include <set>

extern "C"
{
   #include "../ThirdParty/monocypher/monocypher-ed25519.h"
}

// Local stand-in for the license server, signing with its own throwaway key, so that answers the
// real server won't give for our test key (lifetime, malformed...) can go through the whole check
struct MockLicenseServer : private juce::Thread
{
    MockLicenseServer() : juce::Thread ("mock license server")
    {
        uint8_t seed[32];
        arc4random_buf (seed, sizeof (seed));
        crypto_ed25519_key_pair (secretKey, publicKey, seed);
        listener.createListener (port, "127.0.0.1");
        startThread();
    }

    ~MockLicenseServer() override
    {
        signalThreadShouldExit();
        listener.close();
        stopThread (2000);
    }

    juce::String url() const { return "http://127.0.0.1:" + juce::String (port); }

    uint8_t publicKey[32], secretKey[64];
    std::function<void (juce::DynamicObject&)> shape;   // turns the signed answer into the case under test

private:
    void run() override
    {
        while (! threadShouldExit())
            if (std::unique_ptr<juce::StreamingSocket> client { listener.waitForNextConnection() })
                answer (*client);
    }

    void answer (juce::StreamingSocket& client)
    {
        juce::MemoryOutputStream request;
        char buffer[4096];
        for (;;)
        {
            const auto text = request.toString();
            const auto headerEnd = text.indexOf ("\r\n\r\n");
            const int length = text.fromFirstOccurrenceOf ("Content-Length:", false, true).getIntValue();
            if ((headerEnd >= 0 && text.length() >= headerEnd + 4 + length) || client.waitUntilReady (true, 2000) != 1)
                break;
            const int got = client.read (buffer, (int) sizeof (buffer), false);
            if (got <= 0)
                break;
            request.write (buffer, (size_t) got);
        }

        const auto text = request.toString();
        const auto target = text.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("valid", true);
        obj->setProperty ("status", "active");
        obj->setProperty ("machineId", LicenseManager::machineId());
        obj->setProperty ("serverTime", juce::Time::getCurrentTime().toISO8601 (true));
        if (text.startsWith ("POST"))
        {
            const auto body = juce::JSON::parse (text.fromFirstOccurrenceOf ("\r\n\r\n", false, false));
            obj->setProperty ("key", body["key"]);
            obj->setProperty ("nonce", body["nonce"]);
        }
        else
        {
            obj->setProperty ("hash", target.fromFirstOccurrenceOf ("/lisencecheck/", false, false).upToFirstOccurrenceOf ("?", false, false));
            obj->setProperty ("nonce", target.fromFirstOccurrenceOf ("nonce=", false, false).upToFirstOccurrenceOf ("&", false, false));
        }
        if (shape)
            shape (*obj);

        const auto payload = juce::JSON::toString (juce::var (obj), true);
        const auto utf8 = payload.toUTF8();
        uint8_t signature[64];
        crypto_ed25519_sign (signature, secretKey, reinterpret_cast<const uint8_t*> (utf8.getAddress()), utf8.sizeInBytes() - 1);

        auto* envelope = new juce::DynamicObject();
        envelope->setProperty ("alg", "Ed25519");
        envelope->setProperty ("payload", payload);
        envelope->setProperty ("signature", juce::Base64::toBase64 (signature, sizeof (signature)));
        const auto body = juce::JSON::toString (juce::var (envelope), true);
        const auto reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                           + juce::String ((int) body.getNumBytesAsUTF8()) + "\r\n\r\n" + body;
        client.write (reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
    }

    static constexpr int port = 48304;
    juce::StreamingSocket listener;
};

static int runLicenseTest (const juce::File& workDir)
{
    // the real key is only ever read from the environment, never stored in the source
    const auto realKey = juce::SystemStats::getEnvironmentVariable ("ACID_TEST_KEY", {});
    const auto realEmail = juce::SystemStats::getEnvironmentVariable ("ACID_TEST_EMAIL", {});
    workDir.deleteRecursively();
    workDir.createDirectory();
    LicenseManager::storageOverride = workDir;
    int failures = 0;

    auto expect = [&] (const char* name, bool ok, const juce::String& detail)
    {
        failures += ok ? 0 : 1;
        std::printf ("%-46s %s   %s\n", name, ok ? "ok  " : "FAIL", detail.toRawUTF8());
    };
    auto state = [] (LicenseManager& m) { return m.getStatus().state; };
    using S = LicenseManager::State;

    expect ("check hash matches reference md5",
            LicenseManager::checkHash ("URAN-TEST-AAAA-BBBB-CCCC", "00000000-1111-2222-3333-444444444444", "2026-10-30")
                == "4a2e69f945d4e6e33278338efe7264ba", {});
    const auto expectedUuid = juce::SystemStats::getEnvironmentVariable ("ACID_TEST_UUID", {});
    if (expectedUuid.isNotEmpty())
        expect ("machine id is the system hardware UUID", LicenseManager::machineId() == expectedUuid, {});

    {
        LicenseManager m;
        m.checkAsync(); m.waitUntilIdle();
        expect ("no stored key -> asks for key", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);

        m.activateAsync ("hello", "someone@example.com"); m.waitUntilIdle();
        expect ("malformed key rejected locally", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);

        m.activateAsync ("URAN-AAAA-BBBB-CCCC-DDDD", "not-an-email"); m.waitUntilIdle();
        expect ("bad e-mail rejected locally", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);

        m.activateAsync ("URAN-AAAA-BBBB-CCCC-DDDD", "someone@example.com"); m.waitUntilIdle();
        expect ("unknown key rejected by server", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
        expect ("rejected key not stored", ! workDir.getChildFile ("license.json").existsAsFile(), {});
    }

    {
        const juce::String testKey ("URAN-AAAA-BBBB-CCCC-DDDD"), testEmail ("someone@example.com");
        const auto cache = workDir.getChildFile ("license.json");
        const auto future = (juce::Time::getCurrentTime() + juce::RelativeTime::days (30)).toISO8601 (true);
        MockLicenseServer server;
        LicenseManager::serverOverride = server.url();
        LicenseManager::publicKeyOverride = server.publicKey;

        const auto lifetimeAnswer = [] (juce::DynamicObject& o) { o.setProperty ("expiresAt", juce::var()); o.setProperty ("lifetime", true); };
        auto activateWith = [&] (std::function<void (juce::DynamicObject&)> shape, LicenseManager& m)
        {
            cache.deleteFile();
            server.shape = std::move (shape);
            m.activateAsync (testKey, testEmail); m.waitUntilIdle();
        };

        {
            LicenseManager m;
            activateWith (lifetimeAnswer, m);
            const auto st = m.getStatus();
            expect ("lifetime: activates", state (m) == S::licensed && m.isLicensed(), st.message);
            expect ("lifetime: marked, no expiry date", st.lifetime && st.expires.toMilliseconds() == 0, {});
        }
        {
            // how the info panel words it
            SandwaveAudioProcessor proc;
            proc.getLicense().waitUntilIdle();
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            dynamic_cast<SandwaveAudioProcessorEditor*> (editor.get())->showInfo (true);
            juce::MemoryOutputStream png;
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), png);
            workDir.getSiblingFile ("lifetime-info.png").replaceWithData (png.getData(), png.getDataSize());
            expect ("lifetime: plugin opens licensed", proc.getLicense().isLicensed(), {});
        }
        {
            LicenseManager m;
            expect ("lifetime: reload, no dialog flash", m.isLicensed() && m.getStatus().lifetime, {});
            m.checkAsync(); m.waitUntilIdle();
            expect ("lifetime: online re-check", state (m) == S::licensed && ! m.getStatus().offline && m.getStatus().lifetime, m.getStatus().message);
        }
        {
            LicenseManager::serverOverride = "http://127.0.0.1:9";
            LicenseManager m;
            m.checkAsync(); m.waitUntilIdle();
            expect ("lifetime: offline grace", state (m) == S::licensed && m.getStatus().offline && m.getStatus().lifetime, m.getStatus().message);
            LicenseManager::serverOverride = server.url();
        }
        {
            server.shape = [&] (juce::DynamicObject& o) { lifetimeAnswer (o); o.setProperty ("valid", false); o.setProperty ("status", "revoked"); };
            LicenseManager m;
            m.checkAsync(); m.waitUntilIdle();
            expect ("lifetime + valid false -> dialog", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
            expect ("revoked lifetime: cache forgotten", ! cache.existsAsFile(), {});
        }
        {
            LicenseManager m;
            activateWith ([&] (juce::DynamicObject& o) { lifetimeAnswer (o); o.setProperty ("expiresAt", "2000-01-01T00:00:00Z"); }, m);
            expect ("lifetime true ignores any date", m.isLicensed() && m.getStatus().lifetime, {});
        }
        {
            LicenseManager m;
            activateWith ([] (juce::DynamicObject& o) { o.setProperty ("expiresAt", juce::var()); o.setProperty ("lifetime", "true"); }, m);
            expect ("lifetime must be a real boolean", ! m.isLicensed(), m.getStatus().message);
        }
        {
            LicenseManager m;
            activateWith ([] (juce::DynamicObject& o) { o.setProperty ("expiresAt", juce::var()); }, m);
            expect ("no date, not lifetime -> rejected", ! m.isLicensed(), m.getStatus().message);
        }
        {
            LicenseManager m;
            activateWith ([] (juce::DynamicObject& o) { o.setProperty ("expiresAt", "2000-01-01T00:00:00Z"); o.setProperty ("lifetime", false); }, m);
            expect ("dated: past expiry rejected", ! m.isLicensed(), m.getStatus().message);
        }
        {
            LicenseManager m;
            activateWith ([future] (juce::DynamicObject& o) { o.setProperty ("expiresAt", future); o.setProperty ("lifetime", false); }, m);
            const auto st = m.getStatus();
            expect ("dated: still works", m.isLicensed() && ! st.lifetime && st.expires > juce::Time::getCurrentTime(), {});
        }
        {
            // the signature covers the payload: a dated answer can't be edited into a lifetime one
            auto json = juce::JSON::parse (cache);
            const auto genuine = json["payload"].toString();
            const auto payload = genuine.replace ("\"lifetime\": false", "\"lifetime\": true");
            json.getDynamicObject()->setProperty ("payload", payload);
            cache.replaceWithText (juce::JSON::toString (json));
            LicenseManager m;
            expect ("forged lifetime flag not trusted", payload != genuine && ! m.isLicensed(), {});
        }

        cache.deleteFile();
        LicenseManager::serverOverride = {};
        LicenseManager::publicKeyOverride = nullptr;
    }

    if (realKey.isEmpty() || realEmail.isEmpty())
    {
        std::printf ("ACID_TEST_KEY / ACID_TEST_EMAIL not set: skipping the valid-key tests\n");
        return failures;
    }

    const auto file = workDir.getChildFile ("license.json");
    {
        LicenseManager m;
        m.activateAsync (realKey, "someone@example.com"); m.waitUntilIdle();
        expect ("real key + wrong e-mail rejected", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
        expect ("nothing stored after wrong e-mail", ! file.existsAsFile(), {});
    }
    {
        LicenseManager m;
        m.activateAsync (realKey.toLowerCase(), " " + realEmail.toUpperCase() + " ");   // sloppy typing on purpose
        m.waitUntilIdle();
        const auto st = m.getStatus();
        expect ("valid key activates", state (m) == S::licensed && m.isLicensed(),
                (st.lifetime ? juce::String ("lifetime") : "expires " + st.expires.toISO8601 (true)) + ", licensee present: " + (st.licensee.isNotEmpty() ? "yes" : "no")
                    + (st.message.isNotEmpty() ? ", " + st.message : juce::String()));
        if (! m.isLicensed())
        {
            std::printf ("the test key was not accepted: skipping the tests that need it\n%d failure(s)\n", failures);
            return failures;
        }
        struct stat info {};
        ::stat (file.getFullPathName().toRawUTF8(), &info);
        expect ("license file stored, owner-only (0600)", file.existsAsFile() && (info.st_mode & 0777) == 0600,
                juce::String::formatted ("%o", (unsigned) (info.st_mode & 0777)));
    }
    {
        LicenseManager m;
        const bool immediate = m.isLicensed() && state (m) == S::licensed;   // before any network answer
        expect ("reload: licensed at once, no dialog flash", immediate, {});
    }
    const auto activatedFile = file.loadFileAsString();

    LicenseManager::serverOverride = "https://127.0.0.1:9";   // nothing listens there: simulates being offline
    {
        LicenseManager m;
        m.checkAsync(); m.waitUntilIdle();
        expect ("offline -> grace from signed cache", state (m) == S::licensed && m.getStatus().offline, m.getStatus().message);
    }

    // tamper with the cached payload: push the expiry date out (a lifetime answer has none, so change the licensee)
    const auto original = file.loadFileAsString();
    {
        auto json = juce::JSON::parse (original);
        const auto genuine = json["payload"].toString();
        auto payload = genuine.replace ("\"expiresAt\":\"20", "\"expiresAt\":\"29");
        if (payload == genuine)
            payload = genuine.replace ("\"name\":\"", "\"name\":\"X");
        expect ("tamper test really changed the payload", payload != genuine, {});
        json.getDynamicObject()->setProperty ("payload", payload);
        file.replaceWithText (juce::JSON::toString (json));

        LicenseManager m;
        expect ("tampered cache not trusted at startup", ! m.isLicensed(), {});
        m.checkAsync(); m.waitUntilIdle();
        expect ("tampered cache rejected offline", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
    }
    {
        // a genuine signature for a different key must not unlock this one
        auto json = juce::JSON::parse (original);
        json.getDynamicObject()->setProperty ("key", "URAN-AAAA-BBBB-CCCC-DDDD");
        file.replaceWithText (juce::JSON::toString (json));

        LicenseManager m;
        m.checkAsync(); m.waitUntilIdle();
        expect ("cache for another key rejected", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
    }

    LicenseManager::serverOverride = "https://example.com";   // some other HTTPS server
    {
        LicenseManager m;
        m.activateAsync (realKey, realEmail); m.waitUntilIdle();
        expect ("foreign server cannot unlock", state (m) == S::needsKey && ! m.isLicensed(), m.getStatus().message);
    }

    // live /lisencecheck with the new hash: only passes once the server uses the same formula
    LicenseManager::serverOverride = {};
    file.replaceWithText (activatedFile);
    {
        LicenseManager m;
        m.checkAsync(); m.waitUntilIdle();
        const bool ok = state (m) == S::licensed && ! m.getStatus().offline;
        std::printf ("%-46s %s   %s\n", "INFO live re-check (needs server update)", ok ? "ok  " : "no  ",
                     ok ? "server accepts the new hash" : m.getStatus().message.toRawUTF8());
    }

    workDir.deleteRecursively();
    std::printf ("%d failure(s)\n", failures);
    return failures;
}

// Plays a tone (or noise when hz is 0) through processBlock for one 60 Hz frame
static void feedFrame (SandwaveAudioProcessor& proc, float hz, float amp)
{
    static double phase = 0.0;
    static juce::Random rng (7);
    const double sr = 48000.0;
    juce::AudioBuffer<float> buf (2, 800);
    juce::MidiBuffer midi;
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        float s;
        if (hz > 0.0f)
        {
            // a voice-like tone: fundamental plus weaker harmonics
            s = (float) (std::sin (phase) + 0.4 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase)) * amp / 1.6f;
            phase += juce::MathConstants<double>::twoPi * hz / sr;
        }
        else
        {
            s = (rng.nextFloat() * 2.0f - 1.0f) * amp;
        }
        buf.setSample (0, i, s);
        buf.setSample (1, i, s);
    }
    const auto before = buf;
    proc.processBlock (buf, midi);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            jassert (buf.getSample (c, i) == before.getSample (c, i));   // audio must pass through untouched
}

static void savePng (juce::Component& c, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds()), out);
    std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
}

static int runShots (const juce::File& outDir)
{
    outDir.createDirectory();
    int failures = 0;
    auto expect = [&] (const juce::String& name, bool ok, const juce::String& detail)
    {
        failures += ok ? 0 : 1;
        std::printf ("%-46s %s   %s\n", name.toRawUTF8(), ok ? "ok  " : "FAIL", detail.toRawUTF8());
    };

    SandwaveAudioProcessor proc;
    proc.getLicense().waitUntilIdle();
    proc.prepareToPlay (48000.0, 800);

    // locked: no analysis, the activation dialog covers the editor
    proc.getLicense().forceStateForTesting (false, "Enter your e-mail and license key to activate SANDWAVE.");
    feedFrame (proc, 440.0f, 0.3f);
    for (int i = 0; i < 8; ++i) feedFrame (proc, 440.0f, 0.3f);
    expect ("unlicensed: no analysis", proc.getDominantHz() == 0.0f, juce::String (proc.getDominantHz()));
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        savePng (*editor, outDir.getChildFile ("license.png"));
    }

    proc.getLicense().forceStateForTesting (true, {});
    for (float hz : { 82.0f, 220.0f, 440.0f, 1000.0f, 3100.0f })
    {
        for (int i = 0; i < 10; ++i) feedFrame (proc, hz, 0.3f);
        const float got = proc.getDominantHz();
        expect ("tone " + juce::String (hz, 0) + " Hz found", std::abs (got / hz - 1.0f) < 0.02f, juce::String (got, 1) + " Hz");
        expect ("tone " + juce::String (hz, 0) + " Hz level", std::abs (proc.getLevelDb() - (-16.8f)) < 1.0f, juce::String (proc.getLevelDb(), 1) + " dB");
    }
    for (int i = 0; i < 10; ++i) feedFrame (proc, 0.0f, 0.3f);
    expect ("white noise: no dominant frequency", proc.getDominantHz() == 0.0f, juce::String (proc.getDominantHz(), 1));
    for (int i = 0; i < 10; ++i) feedFrame (proc, 440.0f, 0.0f);
    expect ("silence: level floor", proc.getLevelDb() <= -99.0f, juce::String (proc.getLevelDb(), 1));

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    auto* ed = dynamic_cast<SandwaveAudioProcessorEditor*> (editor.get());

    auto run = [&] (float hz, int frames)
    {
        for (int i = 0; i < frames; ++i) { feedFrame (proc, hz, 0.1f); ed->tick(); }
    };

    run (180.0f, 300);
    savePng (*editor, outDir.getChildFile ("plate_low.png"));
    run (900.0f, 300);
    savePng (*editor, outDir.getChildFile ("plate_high.png"));

    proc.apvts.getParameterAsValue ("plate") = 1;
    run (400.0f, 300);
    savePng (*editor, outDir.getChildFile ("plate_circle.png"));

    // silence: the sand must stay where it settled
    run (400.0f, 0);
    for (int i = 0; i < 120; ++i) { feedFrame (proc, 0.0f, 0.0f); ed->tick(); }
    const auto still1 = editor->createComponentSnapshot (editor->getLocalBounds());
    for (int i = 0; i < 30; ++i) { feedFrame (proc, 0.0f, 0.0f); ed->tick(); }
    const auto still2 = editor->createComponentSnapshot (editor->getLocalBounds());
    int diff = 0;
    for (int y = 80; y < 520; y += 2)
        for (int x = 30; x < 460; x += 2)
            diff += still1.getPixelAt (x, y) != still2.getPixelAt (x, y) ? 1 : 0;
    expect ("silence: sand stays put", diff == 0, juce::String (diff) + " pixels changed");

    // manual mode works without any input
    proc.apvts.getParameterAsValue ("plate") = 0;
    proc.apvts.getParameterAsValue ("follow") = false;
    proc.apvts.getParameterAsValue ("freq") = 70.0f;
    for (int i = 0; i < 300; ++i) ed->tick();
    savePng (*editor, outDir.getChildFile ("plate_manual.png"));

    // cost of one frame (simulation + full repaint)
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 120; ++i) { ed->tick(); (void) editor->createComponentSnapshot (editor->getLocalBounds()); }
    std::printf ("frame cost: %.2f ms (tick + full editor render)\n", (juce::Time::getMillisecondCounterHiRes() - start) / 120.0);

    ed->showInfo (true);
    savePng (*editor, outDir.getChildFile ("info.png"));

    std::printf ("%d failure(s)\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--license-test")
        return runLicenseTest (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 2)
        return runShots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    std::printf ("usage: snapshot <out-dir> | snapshot --license-test <work-dir>\n");
    return 1;
}
