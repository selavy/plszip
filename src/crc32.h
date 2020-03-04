#pragma once

#include <cstdint>
#include <cstddef>

uint32_t calc_crc32(uint32_t crc, const uint8_t *buf, size_t len) noexcept;
