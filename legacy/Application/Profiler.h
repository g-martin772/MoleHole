#pragma once

#include <chrono>
#include <string_view>
#include <spdlog/spdlog.h>

class ScopeTimer {
public:
	ScopeTimer(const char* file, const char* func, std::string_view label = {})
		: file_(file), func_(func), label_(label), start_(std::chrono::steady_clock::now()) {}

	~ScopeTimer() {
		const auto end = std::chrono::steady_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(end - start_).count();
		if (label_.empty()) {
			spdlog::debug("[profile] {} :: {} took {:.3f} ms", file_, func_, ms);
		} else {
			spdlog::debug("[profile] {} :: {} [{}] took {:.3f} ms", file_, func_, label_, ms);
		}
	}

	ScopeTimer(const ScopeTimer&) = delete;
	ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
	const char* file_;
	const char* func_;
	std::string_view label_;
	std::chrono::steady_clock::time_point start_;
};

#define PROFILE_DETAIL_CONCAT_IMPL(a, b) a##b
#define PROFILE_DETAIL_CONCAT(a, b) PROFILE_DETAIL_CONCAT_IMPL(a, b)

#define PROFILE_FUNCTION() ::ScopeTimer PROFILE_DETAIL_CONCAT(_profile_fn_, __LINE__)(__FILE__, __func__)
#define PROFILE_SCOPE(label) ::ScopeTimer PROFILE_DETAIL_CONCAT(_profile_sc_, __LINE__)(__FILE__, __func__, (label))
