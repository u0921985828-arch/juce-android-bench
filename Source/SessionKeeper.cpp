#include "SessionKeeper.h"
#include "ProjectStore.h"

SessionKeeper::SessionKeeper() : juce::Thread ("zati-session")
{
    startThread (juce::Thread::Priority::background);
}

SessionKeeper::~SessionKeeper()
{
    //  A write in flight is a WAV that would be left half-finished, so give it
    //  a moment to land before the thread is cut.
    stopThread (3000);
}

juce::File SessionKeeper::folder()
{
    //  Dot-prefixed: this is not the user's content, and it should not turn up
    //  in a file manager or the media scanner next to their projects.
    return ProjectStore::home().getChildFile (".sesion");
}

juce::File SessionKeeper::stateFile() { return folder().getChildFile ("state.xml"); }

juce::File SessionKeeper::padFile (int pad)
{
    return folder().getChildFile ("samples")
                   .getChildFile ("pad" + juce::String (pad + 1).paddedLeft ('0', 2) + ".wav");
}

void SessionKeeper::sync (const SampleBuffer::Ptr* live, int numPads)
{
    bool anything = false;
    {
        const juce::ScopedLock sl (lock);

        for (int i = 0; i < juce::jmin (numPads, kMaxPads); ++i)
        {
            const auto& now = live[i];
            if (now.get() == seen[(size_t) i].get())
                continue;

            seen[(size_t) i]   = now;
            queued[(size_t) i] = now;
            dirty[(size_t) i]  = true;
            anything = true;
        }
    }

    if (anything)
        notify();
}

void SessionKeeper::adopt (const SampleBuffer::Ptr* live, int numPads)
{
    const juce::ScopedLock sl (lock);

    for (int i = 0; i < juce::jmin (numPads, kMaxPads); ++i)
    {
        seen[(size_t) i]   = live[i];
        queued[(size_t) i] = nullptr;
        dirty[(size_t) i]  = false;
    }
}

void SessionKeeper::writeState (const juce::ValueTree& state, const juce::String& projectName)
{
    folder().createDirectory();

    //  The name of the open project rides along in the session's own copy of
    //  the tree, so coming back restores the header too - and a project.xml
    //  written from the same state never carries it.
    auto copy = state.createCopy();
    copy.setProperty ("sesion", true, nullptr);
    copy.setProperty ("proyecto", projectName, nullptr);

    stateFile().replaceWithText (copy.toXmlString());
}

bool SessionKeeper::isIdle() const
{
    const juce::ScopedLock sl (lock);

    if (writing)
        return false;

    for (int i = 0; i < kMaxPads; ++i)
        if (dirty[(size_t) i])
            return false;

    return true;
}

bool SessionKeeper::flush (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (! isIdle())
    {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;

        juce::Thread::sleep (20);
    }

    return true;
}

void SessionKeeper::clear()
{
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < kMaxPads; ++i)
        {
            seen[(size_t) i]   = nullptr;
            queued[(size_t) i] = nullptr;
            dirty[(size_t) i]  = false;
        }
    }

    //  Only an in-flight write is left now, and it would recreate the folder
    //  under us: wait for it, then take the whole thing away.
    flush (1000);
    folder().deleteRecursively();
}

void SessionKeeper::run()
{
    while (! threadShouldExit())
    {
        int pad = -1;
        SampleBuffer::Ptr sb;

        {
            const juce::ScopedLock sl (lock);

            for (int i = 0; i < kMaxPads; ++i)
            {
                if (! dirty[(size_t) i])
                    continue;

                pad = i;
                sb  = queued[(size_t) i];
                dirty[(size_t) i]  = false;
                queued[(size_t) i] = nullptr;
                writing = true;
                break;
            }
        }

        if (pad < 0)
        {
            wait (-1);          // nothing to do: sleep until sync() pokes us
            continue;
        }

        const auto dest = padFile (pad);

        if (sb != nullptr && sb->buffer.getNumSamples() > 0)
        {
            //  Write beside the real name and move it into place, so a process
            //  killed mid-write leaves the previous take intact rather than a
            //  truncated file that reads back as a click.
            const auto tmp = dest.getSiblingFile (dest.getFileName() + ".tmp");

            if (ProjectStore::writeSample (tmp, sb->buffer, sb->sourceSampleRate))
            {
                dest.deleteFile();
                tmp.moveFileTo (dest);
            }
            else
            {
                tmp.deleteFile();
            }
        }
        else
        {
            dest.deleteFile();      // the pad was emptied
        }

        sb = nullptr;               // release it here, off the message thread

        {
            const juce::ScopedLock sl (lock);
            writing = false;
        }
    }
}
