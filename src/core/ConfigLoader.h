#pragma once

#include <QString>
#include <optional>
#include "Models.h"

class ConfigLoader
{
public:
    static std::optional<AppConfig> load(const QString& path);
};