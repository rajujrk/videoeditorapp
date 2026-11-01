// tve-pluginscan <bundle.vst3>: what a VST3 bundle holds, as one line of JSON on standard
// output. The app runs it in a process of its own for each new or changed bundle, so a
// plug-in that crashes or hangs while loading is marked, not fatal.

#include "audio/plugins/PluginFormats.h"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: tve-pluginscan <bundle.vst3>\n");
        return 2;
    }
    std::printf("%s\n", tve::plugins::describeVst3Bundle(argv[1]).c_str());
    return 0;
}
