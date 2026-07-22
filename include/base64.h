#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

std::string base64_encode(const uint8_t* data, size_t len);

#include "Base64.tpp"