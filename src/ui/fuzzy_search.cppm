export module MoleHole:UI.FuzzySearch;

import std;

export namespace MoleHole
{
    namespace FuzzyDetail
    {
        inline char Lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        inline bool IsWordStart(const std::string& text, const std::size_t i)
        {
            if (i == 0) return true;
            const char prev = text[i - 1];
            return !std::isalnum(static_cast<unsigned char>(prev)) ||
                   (std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(text[i])));
        }

        inline std::optional<int> ScoreToken(const std::string& original, const std::string& text, const std::string& token)
        {
            if (const auto pos = text.find(token); pos != std::string::npos)
            {
                int score = 100 + static_cast<int>(token.size()) * 4 - static_cast<int>(std::min<std::size_t>(pos, 40));
                if (pos == 0) score += 60;
                else if (IsWordStart(original, pos)) score += 30;
                if (token.size() == text.size()) score += 40;
                return score;
            }

            int score = 0;
            std::size_t cursor = 0;
            bool previousMatched = false;
            for (const char c : token)
            {
                const auto found = text.find(c, cursor);
                if (found == std::string::npos) return std::nullopt;
                const bool consecutive = previousMatched && found == cursor;
                score += 10 + (consecutive ? 15 : 0) + (IsWordStart(original, found) ? 20 : 0);
                score -= static_cast<int>(std::min<std::size_t>(found - cursor, 8));
                previousMatched = true;
                cursor = found + 1;
            }
            return score;
        }
    }

    // Every whitespace-separated query token must match (substring or in-order subsequence), case-insensitive.
    [[nodiscard]] inline std::optional<int> FuzzyScore(const std::string_view query, const std::string_view target)
    {
        std::string text(target);
        std::ranges::transform(text, text.begin(), FuzzyDetail::Lower);

        int total = 0;
        bool any = false;
        std::size_t i = 0;
        while (i < query.size())
        {
            while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            const auto begin = i;
            while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            if (begin == i) break;

            std::string token(query.substr(begin, i - begin));
            std::ranges::transform(token, token.begin(), FuzzyDetail::Lower);
            const auto score = FuzzyDetail::ScoreToken(std::string(target), text, token);
            if (!score) return std::nullopt;
            total += *score;
            any = true;
        }
        return any ? std::optional<int>(total) : std::optional<int>(0);
    }
}
