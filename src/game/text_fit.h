// Authoritative text surfaces and exact production-font checks; see docs/TEXT_LIMITS.md.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sawgod::game {

struct FitIssue {
    std::string key, surface, problem, text;
    int width = 0, budget = 0; // pixels (0 when the problem isn't a width)
    bool error = true;         // false: a warning (over an original layout that will be redesigned)
};

struct FitBudget {
    std::string surface;
    int width = 0, lines = 1; // pixels per line; lines per message or box
    bool hard = true;         // over it is an error (false: a warning)
    bool scrolls = false;     // viewport width only; no length check
    std::string prefix = "",
                suffix = ""; // additional text sharing this surface (expanded with the same arguments)
};

// Every active layout and unused native layout for a key. fit_budget selects the tightest hard one.
std::vector<FitBudget> fit_surfaces(std::string_view key, std::string_view base_text);

// Summary plus per-surface limits and runtime wrap results, measured with the production font.
// The sample is English/TR chosen by the sheet tool, not a replacement for the translation.
double fit_sample_median(std::string_view sample);
std::string fit_budgets_report(std::string_view base, std::string_view sample, bool all_surfaces = false,
                               bool japanese = false);

// The budget for one key, given its base-language (Japanese) text.
FitBudget fit_budget(std::string_view key, std::string_view base_text);

// Checks `language` (key,text CSV; the rows it lists) against `base` (the Japanese resource, every key).
// Issues come in the language file's row order.
// Optional context supplies runtime fallback names/host parameters; only language rows produce issues.
std::vector<FitIssue> check_fit(std::string_view base, std::string_view language,
                                std::string_view context = {});

// The issues as a CSV report (key,severity,surface,width,budget,problem,text).
std::string fit_report(const std::vector<FitIssue> &issues);

} // namespace sawgod::game
