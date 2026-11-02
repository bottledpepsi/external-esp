#include "Hud.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace hud
{
    const char* Name(Mode m) { return m == Mode::Off ? "Off" : m == Mode::Detailed ? "Detailed" : "Compact"; }
    Mode Next(Mode m) { return m == Mode::Off ? Mode::Compact : m == Mode::Compact ? Mode::Detailed : Mode::Off; }
    bool Parse(const std::string& t, Mode& out)
    {
        if (t == "Off") out = Mode::Off;
        else if (t == "Compact") out = Mode::Compact;
        else if (t == "Detailed") out = Mode::Detailed;
        else return false;
        return true;
    }

    Text::Text()
    {
        for (std::string& s : line_) s.reserve(384); // longest possible line, so Build() never has to grow a string
    }

    void Text::Set(int i, const char* s, size_t n, bool alert)
    {
        line_[i].assign(s, n);
        alert_[i] = alert;
    }

    namespace
    {
        // Appends printf output to a fixed buffer; never overruns.
        struct Buf
        {
            char b[384];
            size_t n = 0;
            void F(const char* fmt, ...) __attribute__((format(printf, 2, 3)))
            {
                if (n >= sizeof b - 1) return;
                va_list ap;
                va_start(ap, fmt);
                const int w = std::vsnprintf(b + n, sizeof b - n, fmt, ap);
                va_end(ap);
                if (w > 0) n = std::min(sizeof b - 1, n + static_cast<size_t>(w));
            }
            void S(std::string_view s)
            {
                const size_t take = std::min(s.size(), sizeof b - 1 - n);
                std::memcpy(b + n, s.data(), take);
                n += take;
            }
        };

        const char* CamText(Cam c)
        {
            switch (c)
            {
            case Cam::Exact: return "cam: exact";
            case Cam::ExactThirdPerson: return "cam: exact, third person";
            case Cam::Reconstructed: return "cam: reconstructed";
            default: return "";
            }
        }
    } // namespace

    void Text::Build(Mode mode, const Facts& f)
    {
        lines_ = 0;
        Buf b;
        if (f.exiting)
        {
            b.F("external-esp  |  exiting in %.1fs (release to cancel)", f.exitLeftS > 0 ? f.exitLeftS : 0.f);
            Set(lines_++, b.b, b.n, true);
            return;
        }
        if (mode == Mode::Off) return;

        const char* state = !f.visible ? "hidden" : f.demo ? "demo" : f.haveCam ? "active" : "waiting";
        if (mode == Mode::Compact)
        {
            b.F("external-esp  |  %s  |  %.0f fps", state, f.fps);
            if (!f.demo && f.agentLive)
            {
                b.F("  |  %.0f Hz", f.sampleHz);
                if (f.ageMs >= 0) b.F("  |  age %.0f ms", f.ageMs);
                b.F("  |  loss %.1f%%", f.lossPct);
            }
            Set(lines_++, b.b, b.n, false);
            // a second line only when something needs attention
            const bool attention = !f.demo && (!f.agentLive || !f.link.empty() || f.replay);
            if (attention)
            {
                Buf c;
                c.S(f.status);
                if (!f.link.empty()) { if (c.n) c.S("  |  "); c.S(f.link); }
                if (f.recording) c.F("  |  REC %llu", static_cast<unsigned long long>(f.recorded));
                Set(lines_++, c.b, c.n, !f.agentLive || !f.link.empty());
            }
            else if (f.recording)
            {
                Buf c;
                c.F("REC %llu%s", static_cast<unsigned long long>(f.recorded), f.dropped ? " (some dropped)" : "");
                Set(lines_++, c.b, c.n, f.dropped != 0);
            }
            return;
        }

        // ---- Detailed ----
        b.F("external-esp  |  %s  |  %.0f fps  |  %.2f ms cpu  |  %s", state, f.fps, f.cpuMs, f.gpu ? "gpu" : "gdi");
        Set(lines_++, b.b, b.n, false);

        Buf c; // what the supervisor / protocol / link say, then camera and the agent's own line, then the recorder
        c.S(f.status);
        if (!f.link.empty()) { if (c.n) c.S("  |  "); c.S(f.link); }
        if (f.haveCam && !f.demo) { c.S("  |  "); c.S(CamText(f.cam)); }
        if (!f.demo && !f.snapStatus.empty() && f.agentLive) { c.S("  |  "); c.S(f.snapStatus); }
        if (f.recording)
        {
            c.F("  |  REC %llu", static_cast<unsigned long long>(f.recorded));
            if (f.dropped) c.F(" (%llu dropped)", static_cast<unsigned long long>(f.dropped));
        }
        Set(lines_++, c.b, c.n, !f.agentLive && !f.demo);

        if (!f.demo && f.agentLive)
        {
            Buf d;
            d.F("agent  %.0f Hz", f.sampleHz);
            if (f.ageMs >= 0) d.F("  |  data age %.1f ms", f.ageMs);
            if (f.jitterMs >= 0) d.F("  |  jitter %.1f ms", f.jitterMs);
            d.F("  |  lost %llu (%.1f%%)  |  reordered %llu", static_cast<unsigned long long>(f.lostTotal), f.lossPct,
                static_cast<unsigned long long>(f.reorderedTotal));
            if (f.predictionErrPx >= 0) d.F("  |  guess off %.1f px", f.predictionErrPx);
            Set(lines_++, d.b, d.n, false);
        }
    }
} // namespace hud
