#include "UpdateChecker.h"

#ifndef SFTP_GITHUB_REPO
 #define SFTP_GITHUB_REPO ""
#endif
#ifndef SFTP_VERSION_STRING
 #ifdef JucePlugin_VersionString
  #define SFTP_VERSION_STRING JucePlugin_VersionString
 #else
  #define SFTP_VERSION_STRING "0.0.0"
 #endif
#endif

// ---------------------------------------------------------------------------
bool UpdateUtils::isNewer (const juce::String& a, const juce::String& b)
{
    auto parts = [] (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        v = v.upToFirstOccurrenceOf ("-", false, false);   // ignore "-beta" suffixes
        juce::StringArray s; s.addTokens (v, ".", "");
        std::vector<int> n;
        for (auto& p : s) n.push_back (p.getIntValue());
        while (n.size() < 3) n.push_back (0);
        return n;
    };
    const auto x = parts (a), y = parts (b);
    for (size_t i = 0; i < std::max (x.size(), y.size()); ++i)
    {
        const int xi = i < x.size() ? x[i] : 0, yi = i < y.size() ? y[i] : 0;
        if (xi != yi) return xi > yi;
    }
    return false;
}

bool UpdateUtils::parseLatestRelease (const juce::String& json, ReleaseInfo& out)
{
    const auto v = juce::JSON::parse (json);
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || ! obj->hasProperty ("tag_name")) return false;

    out.version = obj->getProperty ("tag_name").toString().trimCharactersAtStart ("vV");
    out.pageUrl = obj->getProperty ("html_url").toString();
    out.notes   = obj->getProperty ("body").toString().substring (0, 400);
    out.installerUrl = {};

    if (auto* assets = obj->getProperty ("assets").getArray())
        for (auto& a : *assets)
        {
            const auto name = a["name"].toString();
            if (name.endsWithIgnoreCase (".exe") && name.containsIgnoreCase ("setup"))
            {
                out.installerUrl = a["browser_download_url"].toString();
                break;
            }
        }
    return out.version.isNotEmpty();
}

// ---------------------------------------------------------------------------
UpdateChecker::UpdateChecker() : juce::Thread ("SFTunePlayer update check")
{
    if (repository().isEmpty()) setState (State::disabled, "v" + currentVersion());
}

UpdateChecker::~UpdateChecker() { stopThread (8000); }

juce::String UpdateChecker::currentVersion() { return SFTP_VERSION_STRING; }
juce::String UpdateChecker::repository()     { return SFTP_GITHUB_REPO; }

UpdateChecker::State UpdateChecker::getState() const { const juce::ScopedLock sl (lock); return state; }
juce::String UpdateChecker::getStatusText() const    { const juce::ScopedLock sl (lock); return statusText; }
ReleaseInfo UpdateChecker::getLatest() const         { const juce::ScopedLock sl (lock); return latest; }

void UpdateChecker::setState (State s, const juce::String& text)
{
    {
        const juce::ScopedLock sl (lock);
        state = s; statusText = text;
    }
    broadcaster.sendChangeMessage();   // asynchronous -> delivered on the message thread
}

void UpdateChecker::checkNow()
{
    const auto s = getState();
    if (s == State::disabled || s == State::checking || s == State::downloading || isThreadRunning()) return;
    wantDownload = false;
    startThread();
}

void UpdateChecker::downloadAndInstall()
{
    if (getState() != State::updateAvailable || isThreadRunning()) return;
   #if JUCE_WINDOWS
    if (getLatest().installerUrl.isNotEmpty())
    {
        wantDownload = true;
        startThread();
        return;
    }
   #endif
    juce::URL (getLatest().pageUrl).launchInDefaultBrowser();
}

void UpdateChecker::run()
{
    if (wantDownload) doDownload();
    else              doCheck();
}

bool UpdateChecker::doCheck()
{
    setState (State::checking, "Checking for updates...");

    const juce::URL url ("https://api.github.com/repos/" + repository() + "/releases/latest");
    int status = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (6000)
                                             .withStatusCode (&status)
                                             .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: SFTunePlayer"));
    if (stream == nullptr || status != 200)
    {
        setState (State::failed, "v" + currentVersion() + " (update check failed" + (status > 0 ? ": HTTP " + juce::String (status) : juce::String()) + ")");
        return false;
    }

    ReleaseInfo info;
    if (! UpdateUtils::parseLatestRelease (stream->readEntireStreamAsString(), info))
    {
        setState (State::failed, "v" + currentVersion() + " (could not read release info)");
        return false;
    }

    { const juce::ScopedLock sl (lock); latest = info; }

    if (UpdateUtils::isNewer (info.version, currentVersion()))
        setState (State::updateAvailable, "Update available: v" + info.version + " (you have v" + currentVersion() + ")");
    else
        setState (State::upToDate, "v" + currentVersion() + " - up to date");
    return true;
}

bool UpdateChecker::doDownload()
{
    const auto info = getLatest();
    setState (State::downloading, "Downloading v" + info.version + "...");

    auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("SFTunePlayer-Setup-" + info.version + ".exe");
    target.deleteFile();

    int status = 0;
    auto in = juce::URL (info.installerUrl).createInputStream (
                  juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                      .withConnectionTimeoutMs (15000).withStatusCode (&status).withNumRedirectsToFollow (8)
                      .withExtraHeaders ("User-Agent: SFTunePlayer"));
    if (in == nullptr || status != 200)
    {
        setState (State::updateAvailable, "Download failed - opening the release page instead");
        juce::MessageManager::callAsync ([u = info.pageUrl] { juce::URL (u).launchInDefaultBrowser(); });
        return false;
    }

    {
        juce::FileOutputStream out (target);
        if (! out.openedOk()) { setState (State::failed, "Cannot write " + target.getFullPathName()); return false; }
        juce::HeapBlock<char> buf (65536);
        for (;;)
        {
            if (threadShouldExit()) { out.flush(); target.deleteFile(); return false; }
            const auto n = in->read (buf.get(), 65536);
            if (n <= 0) break;
            out.write (buf.get(), (size_t) n);
        }
        out.flush();
    }

    if (target.getSize() < 100 * 1024)   // an installer is never this small: probably an error page
    {
        target.deleteFile();
        setState (State::failed, "Downloaded file looks invalid");
        return false;
    }

    // The installer closes / waits for the DAW itself (it uses the Windows Restart Manager).
    juce::MessageManager::callAsync ([target] { target.startAsProcess(); });
    setState (State::launched, "Installer started - follow its steps, then restart your DAW");
    return true;
}
