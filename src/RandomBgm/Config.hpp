/* No se compila. El mod se construye con los archivos .c y build_native.bat. */
#pragma once

#include "PathRouter.hpp"

#include <fstream>
#include <string>

inline std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r')) ++begin;
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r')) --end;
    return value.substr(begin, end - begin);
}

inline std::wstring AsciiToWide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

inline bool LoadSettings(const std::wstring& path, Settings& settings, std::string& error) {
    std::ifstream input(path.c_str());
    if (!input) {
        error = "No se pudo abrir la configuracion.";
        return false;
    }

    std::string section;
    std::string line;
    bool first = true;
    while (std::getline(input, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line = line.substr(3);
            }
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        auto eq = line.find('=');
        if (section.empty() && eq != std::string::npos) {
            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));
            if (key == "enabled") settings.enabled = (value == "1" || value == "true" || value == "TRUE");
            else if (key == "avoid_repeat") settings.avoid_repeat = (value == "1" || value == "true" || value == "TRUE");
            continue;
        }

        std::wstring id = AsciiToWide(line);
        std::vector<std::wstring>* dest = nullptr;
        if (section == "arrange_source") dest = &settings.arrange_source;
        else if (section == "arrange_pool") dest = &settings.arrange_pool;
        else if (section == "arrange_boundary") dest = &settings.arrange_boundary;
        else if (section == "arrange_loop") dest = &settings.arrange_loop;
        else if (section == "original_source") dest = &settings.original_source;
        else if (section == "original_pool") dest = &settings.original_pool;
        else if (section == "original_boundary") dest = &settings.original_boundary;
        else if (section == "original_loop") dest = &settings.original_loop;
        if (dest) dest->push_back(id);
    }
    return true;
}
