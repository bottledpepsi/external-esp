// AdapterChoice.h - which GPU the overlay's D3D11 device is created on. Portable (no DXGI types): Gfx fills the list, tests/adapter_choice_test.cpp feeds it
// hand-made machines (laptops with an integrated and a discrete GPU, a VM with only the software adapter, ...).
//
// Why it matters: on a hybrid-graphics laptop the desktop is composed by the adapter that drives the display (normally the integrated GPU) while the game
// may render on the discrete one. The overlay is composed with the desktop, so its device should be on the adapter that owns the display - D3D's own default
// (adapter 0) is usually that, but not guaranteed. The chosen adapter, and why, is always logged.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace gpuselect
{
    struct Adapter
    {
        int index = 0;
        std::string name;
        unsigned vendor = 0, device = 0;
        unsigned long long vramBytes = 0;
        bool software = false;    // the Microsoft Basic Render Driver / WARP / a remote-session adapter: DXGI_ADAPTER_FLAG_SOFTWARE
        bool ownsOutput = false;  // one of its outputs is the monitor the overlay will be on (the primary monitor at start-up)
    };

    struct Preference
    {
        int index = -1;           // --adapter N
        std::string name;         // --adapter NAME (case-insensitive substring)
    };

    struct Decision
    {
        int index = -1;           // -1: pass nullptr to D3D11CreateDevice and let it choose its default
        std::string reason;
    };

    inline std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // "--adapter 1" or "--adapter nvidia": digits only = an index, anything else = a name fragment.
    inline Preference ParsePreference(const std::string& arg)
    {
        Preference p;
        if (arg.empty()) return p;
        if (std::all_of(arg.begin(), arg.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) p.index = std::atoi(arg.c_str());
        else p.name = arg;
        return p;
    }

    inline Decision Choose(const std::vector<Adapter>& list, const Preference& pref)
    {
        Decision d;
        if (list.empty())
        {
            d.reason = "no adapters could be listed: Direct3D picks its default";
            return d;
        }
        if (pref.index >= 0)
        {
            for (const auto& a : list)
                if (a.index == pref.index) { d.index = a.index; d.reason = "chosen with --adapter " + std::to_string(pref.index); return d; }
            d.reason = "--adapter " + std::to_string(pref.index) + " does not exist (" + std::to_string(list.size()) + " adapter(s)): ignored, ";
        }
        else if (!pref.name.empty())
        {
            for (const auto& a : list)
                if (Lower(a.name).find(Lower(pref.name)) != std::string::npos) { d.index = a.index; d.reason = "chosen with --adapter \"" + pref.name + "\""; return d; }
            d.reason = "no adapter matches --adapter \"" + pref.name + "\": ignored, ";
        }
        // Prefer hardware. Among hardware adapters, the one that owns the output the overlay is on; if that is ambiguous or unknown, Direct3D's default.
        std::vector<const Adapter*> hw, owners;
        for (const auto& a : list)
        {
            if (a.software) continue;
            hw.push_back(&a);
            if (a.ownsOutput) owners.push_back(&a);
        }
        if (hw.empty())
        {
            d.reason += "only software adapters exist (a VM or remote session): Direct3D picks its default";
            return d;
        }
        if (owners.size() == 1 && hw.size() > 1)
        {
            d.index = owners.front()->index;
            d.reason += "the adapter that drives the primary monitor";
            return d;
        }
        d.reason += hw.size() == 1 ? "the only hardware adapter: Direct3D picks its default" : "several candidates drive the monitor or none is known: Direct3D picks its default (adapter 0)";
        return d;
    }
} // namespace gpuselect
