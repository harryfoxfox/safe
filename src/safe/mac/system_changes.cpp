/*
 Safe: Encrypted File System
 Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <safe/parse.hpp>
#include <safe/mac/system_changes.hpp>
#include <safe/util.hpp>

#include <cstdint>

#include <Security/Security.h>

namespace safe { namespace mac {

static
bool
hibernate_is_enabled() {
    // NB: we specify full path to pmset since we have to trust
    //     the information that is returned from it
    auto f = popen("/usr/bin/pmset -g", "r");
    if (!f) throw std::runtime_error("pmset fail");
    auto _close_open = safe::create_deferred(pclose, f);
    
    char *line = nullptr;
    size_t linecap = 0;
    auto _free_line = safe::create_deferred([&] () {
        // NB: we use a reference to line, since getline below changes the value
        free(line);
    });
    
    while (true) {
        // TODO: there should be a way to convert FILE * to std::fstream
        //       but let's worry about that another day
        auto bytes_read = getline(&line, &linecap, f);
        if (bytes_read < 0) {
            if (feof(f)) break;
            else throw std::system_error(errno, std::generic_category());
        }
        
        auto parser = safe::BufferParser((uint8_t *) line, linecap);
        try {
            // skip whitespace
            parser.skip_byte(' ');
            
            // read whitespace delimited string
            auto name = parser.parse_string_until_byte(' ');
            if (name != "hibernatemode") continue;
            
            // skip whitespace
            parser.skip_byte(' ');
            
            // read integer
            return parser.parse_ascii_integer<uint32_t>();
        }
        catch (const std::exception & err) {
            // parser error, this is normal since we're matching a specific line
            lbx_log_debug("error while parsing: %s", err.what());
        }
    }
    
    // no hibernatemode key that we could find
    // assume it's enable
    // NB: this is a safer assumption, although less convenient
    return true;
}

static
bool
enable_hibernate(ShellRun shell_run, bool enable) {
    // don't need to do anything
    if (enable == hibernate_is_enabled()) return false;
    
    if (enable) throw std::runtime_error("enabling hibernate is not supported");
    shell_run("/usr/bin/pmset",
              (const char *[]) {"-a", "hibernatemode", "0", nullptr});
    
    return false;
}

static
bool
encrypted_swap_is_enabled() {
    // NB: we specify full path to sysctl since we have to trust
    //     the information that is returned from it
    while (true) {
        auto f = popen("/usr/sbin/sysctl vm.swapusage", "r");
        if (!f) throw std::runtime_error("sysctl fail");
        auto _close_open = safe::create_deferred(pclose, f);
    
        // get first line
        char *line = nullptr;
        size_t linecap = 0;
        auto bytes_read = getline(&line, &linecap, f);
        if (bytes_read < 0) {
            // NB: this was happening often when starting under Xcode
            if (errno == EINTR) continue;
            else throw std::system_error(errno, std::generic_category());
        }
        auto _free_line = safe::create_deferred(free, line);
    
        return strnstr(line, "(encrypted)", linecap);
    }
}

static
bool
enable_encrypted_swap(ShellRun shell_run, bool enable) {
    const char *DEFAULTS_PATH = "/usr/bin/defaults";
    
    bool must_reboot = enable != encrypted_swap_is_enabled();
    
    if (enable) {
        shell_run(DEFAULTS_PATH, (const char *[]) {
            "write", "/Library/Preferences/com.apple.virtualMemory",
            "UseEncryptedSwap", "-boolean", "yes",
            nullptr,
        });
        
        try {
            shell_run(DEFAULTS_PATH, (const char *[]) {
                "delete", "/Library/Preferences/com.apple.virtualMemory",
                "DisableEncryptedSwap",
                nullptr,
            });
        }
        catch (const std::exception & err) {
            // the key DisableEncryptedSwap could have no existed,
            // we have no way to determine that condition so
            // assume that if the preceding command succeeded and this one failed,
            // it's because the key didn't exis
            lbx_log_debug("Assuming DisableEncryptedSwap didn't exist in /Library/Preferences/com.apple.virtualMemory: %s",
                          err.what());
        }
    }
    else {
        shell_run(DEFAULTS_PATH, (const char *[]) {
            "write", "/Library/Preferences/com.apple.virtualMemory",
            "UseEncryptedSwap", "-boolean", "no",
            nullptr,
        });
        
        shell_run(DEFAULTS_PATH, (const char *[]) {
            "write", "/Library/Preferences/com.apple.virtualMemory",
            "DisableEncryptedSwap", "-boolean", "yes",
            nullptr,
        });
    }
    
    return must_reboot;
}

bool
system_changes_are_required() {
    return (hibernate_is_enabled() || !encrypted_swap_is_enabled());
}

bool
make_required_system_changes_common(ShellRun shell_run) {
    bool reboot_required = false;
    
    if (enable_hibernate(shell_run, false)) {
        reboot_required = true;
    }
    
    if (enable_encrypted_swap(shell_run, true)) {
        reboot_required = true;
    }
    
    return reboot_required;
}

}}
