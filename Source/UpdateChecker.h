#pragma once
#include <juce_events/juce_events.h>
#include <functional>

// ============================================================================
//  Checks GitHub Releases for a newer version and (on Windows) downloads and
//  launches the installer. One instance per process (use SharedResourcePointer),
//  so several plugin instances in a DAW only check once.
// ============================================================================

struct ReleaseInfo
{
    juce::String version;        // e.g. "1.0.3" (without leading 'v')
    juce::String pageUrl;        // release page
    juce::String installerUrl;   // direct link to *Setup*.exe asset (may be empty)
    juce::String notes;          // release body (first lines)
};

namespace UpdateUtils
{
    // true if a > b, comparing dotted numeric versions ("v1.2.10" > "1.2.9")
    bool isNewer (const juce::String& a, const juce::String& b);
    // Parses the JSON of GET /repos/{owner}/{repo}/releases/latest
    bool parseLatestRelease (const juce::String& json, ReleaseInfo& out);
}

class UpdateChecker : private juce::Thread
{
public:
    enum class State { idle, checking, upToDate, updateAvailable, downloading, launched, failed, disabled };

    UpdateChecker();
    ~UpdateChecker() override;

    void checkNow();                       // safe to call repeatedly; runs in background
    void downloadAndInstall();             // Windows: download installer & run it; others: open release page

    State getState() const;
    juce::String getStatusText() const;
    ReleaseInfo getLatest() const;
    static juce::String currentVersion();
    static juce::String repository();      // "owner/repo" baked in at build time

    // Listeners are called on the message thread whenever the state changes.
    juce::ChangeBroadcaster broadcaster;

private:
    void run() override;
    void setState (State s, const juce::String& text);
    bool doCheck();
    bool doDownload();

    mutable juce::CriticalSection lock;
    State state = State::idle;
    juce::String statusText;
    ReleaseInfo latest;
    bool wantDownload = false;
    float progress = 0.0f;
};
