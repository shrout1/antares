// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2015-2017 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include <GLFW/glfw3.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pn/output>
#include <sfz/sfz.hpp>

#include "config/dirs.hpp"
#include "config/file-prefs-driver.hpp"
#include "config/ledger.hpp"
#include "config/plugins.hpp"
#include "config/preferences.hpp"
#include "game/sys.hpp"
#include "glfw/video-driver.hpp"
#include "lang/exception.hpp"
#include "sound/openal-driver.hpp"
#include "ui/flows/master.hpp"

using sfz::range;

namespace args = sfz::args;

namespace antares {
namespace {

pn::string_view default_config_path() {
    static pn::string path = pn::format("{0}/config.pn", dirs().root);
    return path;
}

void usage(pn::output_view out, pn::string_view progname, int retcode) {
    out.format(
            "usage: {0} [OPTIONS] [scenario]\n"
            "\n"
            "  Antares: a tactical space combat game\n"
            "\n"
            "  arguments:\n"
            "    scenario            path to plugin file (default: factory scenario)\n"
            "\n"
            "  options:\n"
            "    -a, --app-data      set path to application data\n"
            "                        (default: {1})\n"
            "    -c, --config        set path to config file\n"
            "                        (default: {2})\n"
            "    -f, --factory       set path to factory scenario\n"
            "                        (default: {3})\n"
            "    -p, --plugins       also list plugins found in this directory\n"
            "                        (may be repeated; {4} is always searched)\n"
            "    -h, --help          display this help screen\n",
            progname, default_application_path(), default_config_path(),
            default_factory_scenario_path(), dirs().scenarios);
    exit(retcode);
}

void main(int argc, char* const* argv) {
    pn::string_view progname = sfz::path::basename(argv[0]);

    args::callbacks callbacks;

    sfz::optional<pn::string_view> scenario;
    callbacks.argument = [&scenario](pn::string_view arg) {
        if (!scenario.has_value()) {
            scenario.emplace(arg);
        } else {
            return false;
        }
        return true;
    };

    pn::string_view config_path = default_config_path();
    // Options to pass on when restarting with another plugin.
    std::vector<pn::string> restart_options;
    callbacks.short_option = [&progname, &config_path, &restart_options](
                                     pn::rune opt, const args::callbacks::get_value_f& get_value) {
        pn::string_view value;
        switch (opt.value()) {
            case 'a': set_application_path(value = get_value()); break;
            case 'c': config_path = value = get_value(); break;
            case 'f': set_factory_scenario_path(value = get_value()); break;
            case 'p': add_plugin_directory(value = get_value()); break;
            case 'h': usage(pn::out, progname, 0); return true;
            default: return false;
        }
        restart_options.push_back(pn::format("-{0}", opt));
        restart_options.push_back(value.copy());
        return true;
    };

    callbacks.long_option =
            [&callbacks](pn::string_view opt, const args::callbacks::get_value_f& get_value) {
                if (opt == "app-data") {
                    return callbacks.short_option(pn::rune{'a'}, get_value);
                } else if (opt == "config") {
                    return callbacks.short_option(pn::rune{'c'}, get_value);
                } else if (opt == "factory-scenario") {
                    return callbacks.short_option(pn::rune{'f'}, get_value);
                } else if (opt == "plugins") {
                    return callbacks.short_option(pn::rune{'p'}, get_value);
                } else if (opt == "help") {
                    return callbacks.short_option(pn::rune{'h'}, get_value);
                } else {
                    return false;
                }
            };

    args::parse(argc - 1, argv + 1, callbacks);

    if (!sfz::path::isdir(application_path())) {
        if (application_path() == default_application_path()) {
            throw std::runtime_error(
                    "application data not installed\n"
                    "\n"
                    "Please install it, or specify a path with --app-data");
        } else {
            throw std::runtime_error(
                    pn::format("{0}: application data not found", application_path()).c_str());
        }
        exit(1);
    }

    {
        FilePrefsDriver prefs(config_path);

        DirectoryLedger   ledger;
        OpenAlSoundDriver sound;
        GLFWVideoDriver   video;
        enable_plugin_switching();
        video.loop(new Master(scenario, time(nullptr)));
    }

    if (plugin_requested()) {
        // Start over with the plugin chosen on the plugins screen, keeping the other options.
        std::vector<char*> new_argv{argv[0]};
        for (pn::string& option : restart_options) {
            new_argv.push_back(option.data());
        }
        if (requested_plugin().has_value()) {
            new_argv.push_back(const_cast<char*>(requested_plugin()->c_str()));
        }
        new_argv.push_back(nullptr);
        execvp(argv[0], new_argv.data());
        throw std::runtime_error(pn::format("{0}: {1}", argv[0], strerror(errno)).c_str());
    }
}

}  // namespace
}  // namespace antares

int main(int argc, char* const* argv) { return antares::wrap_main(antares::main, argc, argv); }
