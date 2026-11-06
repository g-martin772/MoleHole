export module MoleHole:Application.ScriptLibrary;

import std;
import :UI.FuzzySearch;

export namespace MoleHole
{
    struct ScriptLibraryProperty
    {
        std::string Name;
        std::string Type;
        std::string Default;
        std::string Unit;
        std::string Min;
        std::string Max;
    };

    struct ScriptLibraryEntry
    {
        std::string Name;
        std::filesystem::path Path;
        bool Library = false;
        std::vector<ScriptLibraryProperty> Properties;
        std::vector<std::string> Callbacks;
        std::vector<std::string> Emits;
        std::vector<std::string> Handles;
        std::vector<std::string> Requires;
        std::vector<std::string> RequiredBy;
        std::vector<std::string> Templates;
        std::string Source;
        std::size_t Lines = 0;
    };

    namespace ScriptLibraryDetail
    {
        inline bool IsIdentifier(const char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        inline std::string StripComments(const std::string_view source)
        {
            std::string out(source);
            char quote = 0;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                const char c = out[i];
                if (quote)
                {
                    if (c == '\\') ++i;
                    else if (c == quote || c == '\n') quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    quote = c;
                    continue;
                }
                if (c != '-' || i + 1 >= out.size() || out[i + 1] != '-') continue;
                std::size_t end = out.find('\n', i);
                if (end == std::string::npos) end = out.size();
                if (out.compare(i + 2, 2, "[[") == 0)
                {
                    const auto close = out.find("]]", i + 4);
                    end = close == std::string::npos ? out.size() : close + 2;
                }
                for (std::size_t k = i; k < end; ++k)
                {
                    if (out[k] != '\n') out[k] = ' ';
                }
                i = end - 1;
            }
            return out;
        }

        inline std::string Trim(const std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
        }

        inline std::string Unquote(const std::string& text)
        {
            if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
                return text.substr(1, text.size() - 2);
            return text;
        }

        inline std::size_t FindClosing(const std::string_view text, const std::size_t open)
        {
            int depth = 0;
            char quote = 0;
            for (std::size_t i = open; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quote)
                {
                    if (c == '\\') ++i;
                    else if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '{' || c == '(' || c == '[') ++depth;
                else if ((c == '}' || c == ')' || c == ']') && --depth == 0) return i;
            }
            return std::string_view::npos;
        }

        inline std::vector<std::string> SplitTopLevel(const std::string_view text)
        {
            std::vector<std::string> parts;
            int depth = 0;
            char quote = 0;
            std::size_t begin = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quote)
                {
                    if (c == '\\') ++i;
                    else if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '{' || c == '(' || c == '[') ++depth;
                else if (c == '}' || c == ')' || c == ']') --depth;
                else if (c == ',' && depth == 0)
                {
                    parts.push_back(Trim(text.substr(begin, i - begin)));
                    begin = i + 1;
                }
            }
            if (const auto last = Trim(text.substr(begin)); !last.empty()) parts.push_back(last);
            return parts;
        }

        inline void AddUnique(std::vector<std::string>& list, std::string value)
        {
            if (!value.empty() && !std::ranges::contains(list, value)) list.push_back(std::move(value));
        }

        inline std::vector<std::string> QuotedArguments(const std::string& text, const std::regex& pattern)
        {
            std::vector<std::string> found;
            for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
                AddUnique(found, (*it)[1].str());
            return found;
        }

        inline std::vector<ScriptLibraryProperty> ParseProperties(const std::string& text)
        {
            std::vector<ScriptLibraryProperty> properties;
            static const std::regex header(R"((^|[^\w.])properties\s*=\s*\{)");
            std::smatch match;
            if (!std::regex_search(text, match, header)) return properties;
            const auto open = static_cast<std::size_t>(match.position(0) + match.length(0)) - 1;
            const auto close = FindClosing(text, open);
            if (close == std::string::npos) return properties;
            const std::string_view body(text.data() + open + 1, close - open - 1);
            for (const auto& entry : SplitTopLevel(body))
            {
                if (entry.size() < 2 || entry.front() != '{' || entry.back() != '}') continue;
                ScriptLibraryProperty property;
                for (const auto& field : SplitTopLevel(std::string_view(entry).substr(1, entry.size() - 2)))
                {
                    const auto eq = field.find('=');
                    if (eq == std::string::npos) continue;
                    const auto key = Trim(field.substr(0, eq));
                    const auto value = Trim(field.substr(eq + 1));
                    if (key == "name") property.Name = Unquote(value);
                    else if (key == "type") property.Type = Unquote(value);
                    else if (key == "default") property.Default = value;
                    else if (key == "unit") property.Unit = Unquote(value);
                    else if (key == "min") property.Min = value;
                    else if (key == "max") property.Max = value;
                }
                if (!property.Name.empty()) properties.push_back(std::move(property));
            }
            return properties;
        }

        inline std::string ToLower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            return text;
        }

        inline std::string DetailText(const ScriptLibraryEntry& entry)
        {
            std::string text;
            const auto append = [&](const std::string& piece) { text += piece; text += ' '; };
            for (const auto& property : entry.Properties) { append(property.Name); append(property.Type); }
            for (const auto* list : {&entry.Emits, &entry.Handles, &entry.Requires, &entry.RequiredBy, &entry.Templates})
            {
                for (const auto& item : *list) append(item);
            }
            return ToLower(std::move(text));
        }
    }

    [[nodiscard]] inline ScriptLibraryEntry ParseScriptSource(std::string name, std::filesystem::path path, std::string source)
    {
        using namespace ScriptLibraryDetail;
        ScriptLibraryEntry entry;
        entry.Library = name.starts_with("lib/");
        entry.Name = std::move(name);
        entry.Path = std::move(path);
        entry.Lines = static_cast<std::size_t>(std::ranges::count(source, '\n')) + (!source.empty() && source.back() != '\n' ? 1 : 0);
        const std::string code = StripComments(source);
        entry.Source = std::move(source);

        entry.Properties = ParseProperties(code);

        static const std::regex callback(R"((^|[^\w.])(On[A-Z]\w*)\s*=\s*function)");
        for (auto it = std::sregex_iterator(code.begin(), code.end(), callback); it != std::sregex_iterator(); ++it)
            AddUnique(entry.Callbacks, (*it)[2].str());

        static const std::regex emit(R"(events\s*\.\s*emit\s*\(\s*["']([^"']+)["'])");
        static const std::regex listen(R"(events\s*\.\s*on\s*\(\s*["']([^"']+)["'])");
        static const std::regex compare(R"((^|[^\w.])name\s*==\s*["']([^"']+)["'])");
        static const std::regex require(R"((^|[^\w.])require\s*\(\s*["']([^"']+)["']\s*\))");
        entry.Emits = QuotedArguments(code, emit);
        entry.Handles = QuotedArguments(code, listen);
        if (std::ranges::contains(entry.Callbacks, "OnEvent"))
        {
            for (auto it = std::sregex_iterator(code.begin(), code.end(), compare); it != std::sregex_iterator(); ++it)
                AddUnique(entry.Handles, (*it)[2].str());
        }
        for (auto it = std::sregex_iterator(code.begin(), code.end(), require); it != std::sregex_iterator(); ++it)
            AddUnique(entry.Requires, (*it)[2].str());
        return entry;
    }

    [[nodiscard]] inline std::vector<std::string> ScriptNamesInTemplate(const std::string_view text)
    {
        using namespace ScriptLibraryDetail;
        std::vector<std::string> names;
        std::size_t scriptsIndent = std::string::npos;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            auto end = text.find('\n', begin);
            if (end == std::string_view::npos) end = text.size();
            const auto line = text.substr(begin, end - begin);
            begin = end + 1;
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string_view::npos) continue;
            const auto content = line.substr(first);
            if (scriptsIndent != std::string::npos && first <= scriptsIndent && !content.starts_with("- "))
                scriptsIndent = std::string::npos;
            if (content.starts_with("Scripts:"))
            {
                scriptsIndent = first;
                continue;
            }
            if (scriptsIndent == std::string::npos) continue;
            if (!content.starts_with("- Name:")) continue;
            auto value = Trim(content.substr(7));
            if (const auto hash = value.find(" #"); hash != std::string::npos) value = Trim(value.substr(0, hash));
            AddUnique(names, Unquote(value));
        }
        return names;
    }

    [[nodiscard]] inline std::vector<ScriptLibraryEntry> BuildScriptLibrary(const std::span<const std::filesystem::path> scriptDirs,
                                                                           const std::filesystem::path& templatesDir)
    {
        using namespace ScriptLibraryDetail;
        std::vector<ScriptLibraryEntry> entries;
        for (const auto& dir : scriptDirs)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            for (std::filesystem::recursive_directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec))
            {
                if (!it->is_regular_file()) continue;
                const auto extension = it->path().extension();
                if (extension != ".luau" && extension != ".lua") continue;
                auto name = (it->path().lexically_relative(dir)).replace_extension().generic_string();
                if (std::ranges::contains(entries, name, &ScriptLibraryEntry::Name)) continue;
                std::ifstream stream(it->path(), std::ios::binary);
                std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                entries.push_back(ParseScriptSource(std::move(name), it->path(), std::move(source)));
            }
        }

        std::ranges::sort(entries, [](const ScriptLibraryEntry& a, const ScriptLibraryEntry& b)
        {
            return a.Library != b.Library ? !a.Library : a.Name < b.Name;
        });

        std::error_code ec;
        std::vector<std::filesystem::path> templates;
        if (std::filesystem::is_directory(templatesDir, ec))
        {
            for (const auto& file : std::filesystem::directory_iterator(templatesDir, ec))
            {
                if (file.is_regular_file() && file.path().extension() == ".yaml") templates.push_back(file.path());
            }
            std::ranges::sort(templates);
        }
        for (const auto& file : templates)
        {
            std::ifstream stream(file, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            for (const auto& used : ScriptNamesInTemplate(text))
            {
                const auto found = std::ranges::find(entries, used, &ScriptLibraryEntry::Name);
                if (found != entries.end()) AddUnique(found->Templates, file.filename().string());
            }
        }

        const auto find = [&](const std::string& name) -> ScriptLibraryEntry*
        {
            const auto found = std::ranges::find(entries, name, &ScriptLibraryEntry::Name);
            return found == entries.end() ? nullptr : &*found;
        };
        for (const auto& entry : entries)
        {
            for (const auto& required : entry.Requires)
            {
                if (auto* target = find(required)) AddUnique(target->RequiredBy, entry.Name);
            }
        }

        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> inherited(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            std::vector<std::string> visited;
            std::vector<std::string> pending = entries[i].Requires;
            while (!pending.empty())
            {
                const auto next = std::move(pending.back());
                pending.pop_back();
                if (std::ranges::contains(visited, next)) continue;
                visited.push_back(next);
                const auto* lib = find(next);
                if (!lib) continue;
                for (const auto& event : lib->Emits) AddUnique(inherited[i].first, event);
                for (const auto& event : lib->Handles) AddUnique(inherited[i].second, event);
                for (const auto& more : lib->Requires) pending.push_back(more);
            }
        }
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            for (auto& event : inherited[i].first) AddUnique(entries[i].Emits, std::move(event));
            for (auto& event : inherited[i].second) AddUnique(entries[i].Handles, std::move(event));
        }
        return entries;
    }

    [[nodiscard]] inline std::optional<int> ScriptLibraryScore(const ScriptLibraryEntry& entry, const std::string_view query)
    {
        using namespace ScriptLibraryDetail;
        if (Trim(query).empty()) return 0;
        if (const auto score = FuzzyScore(query, entry.Name)) return *score + 1000;
        const auto detail = DetailText(entry);
        int total = 0;
        std::size_t i = 0;
        while (i < query.size())
        {
            while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            const auto begin = i;
            while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            if (begin == i) break;
            if (!detail.contains(ToLower(std::string(query.substr(begin, i - begin))))) return std::nullopt;
            total += 10;
        }
        return total;
    }

    [[nodiscard]] inline std::vector<std::size_t> FilterScriptLibrary(const std::span<const ScriptLibraryEntry> entries,
                                                                     const std::string_view query)
    {
        std::vector<std::pair<int, std::size_t>> scored;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (const auto score = ScriptLibraryScore(entries[i], query)) scored.emplace_back(*score, i);
        }
        std::ranges::stable_sort(scored, [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::size_t> order;
        for (const auto& [score, index] : scored) order.push_back(index);
        return order;
    }
}
