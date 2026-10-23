#pragma once
#include <cstdio>
#include <string>
namespace sim { extern std::string log; void logf(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3))); }
#define ESP_LOGCONFIG(tag, ...) sim::logf("C", __VA_ARGS__)
#define ESP_LOGW(tag, ...) sim::logf("W", __VA_ARGS__)
#define ESP_LOGI(tag, ...) sim::logf("I", __VA_ARGS__)
#define ESP_LOGV(tag, ...) sim::logf("V", __VA_ARGS__)
#define LOG_UPDATE_INTERVAL(this_) (void) 0
#define LOG_PIN(prefix, pin) (void) 0
