#pragma once
void metrics_test_log(const char* tag, const char* format, ...);
#define ESP_LOGI(tag, ...) metrics_test_log(tag, __VA_ARGS__)
