#include "PasswordHash.h"

#include <Windows.h>
#include <bcrypt.h>

#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    constexpr ULONG ITERATIONS = 600000;
    constexpr ULONG SALT_SIZE = 16;
    constexpr ULONG HASH_SIZE = 32;

    std::string ToHex(
        const unsigned char* data,
        size_t size)
    {
        std::ostringstream output;

        output << std::hex << std::setfill('0');

        for (size_t i = 0; i < size; ++i)
        {
            output
                << std::setw(2)
                << static_cast<unsigned int>(data[i]);
        }

        return output.str();
    }

    bool FromHex(
        const std::string& input,
        std::vector<unsigned char>& output)
    {
        if (input.size() % 2 != 0)
            return false;

        output.clear();

        for (size_t i = 0; i < input.size(); i += 2)
        {
            unsigned int value = 0;

            std::istringstream stream(
                input.substr(i, 2));

            stream >> std::hex >> value;

            if (stream.fail() || value > 255)
                return false;

            output.push_back(
                static_cast<unsigned char>(value));
        }

        return true;
    }

    bool DeriveKey(
        const std::string& password,
        const unsigned char* salt,
        ULONG saltSize,
        ULONG iterations,
        unsigned char* output,
        ULONG outputSize)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;

        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            BCRYPT_ALG_HANDLE_HMAC_FLAG);

        if (status < 0)
            return false;

        status = BCryptDeriveKeyPBKDF2(
            algorithm,
            reinterpret_cast<PUCHAR>(
                const_cast<char*>(password.data())),
            static_cast<ULONG>(password.size()),
            const_cast<PUCHAR>(salt),
            saltSize,
            iterations,
            output,
            outputSize,
            0);

        BCryptCloseAlgorithmProvider(
            algorithm,
            0);

        return status >= 0;
    }

    bool ConstantTimeEqual(
        const std::vector<unsigned char>& a,
        const std::vector<unsigned char>& b)
    {
        if (a.size() != b.size())
            return false;

        unsigned char difference = 0;

        for (size_t i = 0; i < a.size(); ++i)
        {
            difference |= a[i] ^ b[i];
        }

        return difference == 0;
    }
}

bool IsPasswordHash(
    const std::string& value)
{
    return value.rfind(
        "pbkdf2_sha256$",
        0) == 0;
}

bool HashPassword(
    const std::string& password,
    std::string& encoded)
{
    unsigned char salt[SALT_SIZE]{};
    unsigned char hash[HASH_SIZE]{};

    NTSTATUS status = BCryptGenRandom(
        nullptr,
        salt,
        SALT_SIZE,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0)
        return false;

    if (!DeriveKey(
        password,
        salt,
        SALT_SIZE,
        ITERATIONS,
        hash,
        HASH_SIZE))
    {
        return false;
    }

    encoded =
        "pbkdf2_sha256$" +
        std::to_string(ITERATIONS) +
        "$" +
        ToHex(salt, SALT_SIZE) +
        "$" +
        ToHex(hash, HASH_SIZE);

    SecureZeroMemory(
        hash,
        sizeof(hash));

    return true;
}

bool VerifyPassword(
    const std::string& password,
    const std::string& encoded)
{
    if (!IsPasswordHash(encoded))
        return false;

    size_t p1 = encoded.find('$');

    size_t p2 = encoded.find(
        '$',
        p1 + 1);

    size_t p3 = encoded.find(
        '$',
        p2 + 1);

    if (p1 == std::string::npos ||
        p2 == std::string::npos ||
        p3 == std::string::npos)
    {
        return false;
    }

    ULONG iterations = 0;

    try
    {
        unsigned long value = std::stoul(
            encoded.substr(
                p1 + 1,
                p2 - p1 - 1));

        if (value < 100000 || value > 2000000)
            return false;

        iterations = static_cast<ULONG>(value);
    }
    catch (...)
    {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;

    if (!FromHex(
        encoded.substr(
            p2 + 1,
            p3 - p2 - 1),
        salt))
    {
        return false;
    }

    if (!FromHex(
        encoded.substr(p3 + 1),
        expected))
    {
        return false;
    }

    if (salt.size() != SALT_SIZE ||
        expected.size() != HASH_SIZE)
    {
        return false;
    }

    std::vector<unsigned char> actual(
        HASH_SIZE);

    if (!DeriveKey(
        password,
        salt.data(),
        static_cast<ULONG>(salt.size()),
        iterations,
        actual.data(),
        static_cast<ULONG>(actual.size())))
    {
        return false;
    }

    return ConstantTimeEqual(
        actual,
        expected);
}