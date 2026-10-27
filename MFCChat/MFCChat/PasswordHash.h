#pragma once

#include <string>

bool HashPassword(
    const std::string& password,
    std::string& encoded
);

bool VerifyPassword(
    const std::string& password,
    const std::string& encoded
);

bool IsPasswordHash(
    const std::string& value
);